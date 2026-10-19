// USB-CDC (stdio) -> I2C master bridge.
//
// Line protocol (ASCII, \n or \r\n terminated, hex case-insensitive):
//   P                              -> PONG a2b-i2c-bridge v1 <baud>
//   W <addr> <hex>                 -> OK | NAK | ERR <text>     (write + STOP)
//   R <addr> <n> <hex|->           -> OK <hex> | NAK | ERR <text>
//                                     (hex: write w/o STOP, repeated START, read n)
//   B <kHz>                        -> OK <actual Hz>
//
// The transfer engine drives the DW_apb_i2c registers directly (rather than the SDK
// i2c_*_blocking helpers) so that the abort source is preserved: arbitration loss
// (another master such as an ADI USBi on the bus) is reported as "ERR ARB".

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#ifndef BRIDGE_I2C_INDEX
#define BRIDGE_I2C_INDEX 0
#endif
#ifndef BRIDGE_SDA_PIN
#define BRIDGE_SDA_PIN 4
#endif
#ifndef BRIDGE_SCL_PIN
#define BRIDGE_SCL_PIN 5
#endif
#ifndef BRIDGE_DEFAULT_KHZ
#define BRIDGE_DEFAULT_KHZ 400
#endif

#define BRIDGE_I2C I2C_INSTANCE(BRIDGE_I2C_INDEX)
#define MAX_DATA 300
#define LINE_MAX (16 + 2 * MAX_DATA + 64)

static uint32_t g_baud;

// ---------------------------------------------------------------- LED

#ifdef PICO_DEFAULT_LED_PIN
static absolute_time_t g_led_off;
static void led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    g_led_off = nil_time;
}
static void led_activity(void) {
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    g_led_off = make_timeout_time_ms(30);
}
static void led_poll(void) {
    if (!is_nil_time(g_led_off) && time_reached(g_led_off)) {
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        g_led_off = nil_time;
    }
}
#else
static void led_init(void) {}
static void led_activity(void) {}
static void led_poll(void) {}
#endif

// ---------------------------------------------------------------- I2C

static void bus_setup(void) {
    g_baud = i2c_init(BRIDGE_I2C, g_baud ? g_baud : BRIDGE_DEFAULT_KHZ * 1000u);
    gpio_set_function(BRIDGE_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(BRIDGE_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(BRIDGE_SDA_PIN);
    gpio_pull_up(BRIDGE_SCL_PIN);
}

// After a timeout: if a slave is holding SDA low, clock SCL up to 9 times (open-drain
// emulation via pull-up / drive-low) and issue a STOP, then re-init the controller.
static void bus_recover(void) {
    i2c_deinit(BRIDGE_I2C);
    gpio_init(BRIDGE_SDA_PIN);
    gpio_init(BRIDGE_SCL_PIN);
    gpio_pull_up(BRIDGE_SDA_PIN);
    gpio_pull_up(BRIDGE_SCL_PIN);
    gpio_set_dir(BRIDGE_SDA_PIN, GPIO_IN);
    gpio_set_dir(BRIDGE_SCL_PIN, GPIO_IN);
    gpio_put(BRIDGE_SDA_PIN, 0);
    gpio_put(BRIDGE_SCL_PIN, 0);
    sleep_us(10);
    if (gpio_get(BRIDGE_SCL_PIN) && !gpio_get(BRIDGE_SDA_PIN)) {
        for (int i = 0; i < 9 && !gpio_get(BRIDGE_SDA_PIN); i++) {
            gpio_set_dir(BRIDGE_SCL_PIN, GPIO_OUT);  // SCL low
            sleep_us(5);
            gpio_set_dir(BRIDGE_SCL_PIN, GPIO_IN);   // SCL released
            sleep_us(5);
        }
        // STOP: SDA low -> SCL high -> SDA high
        gpio_set_dir(BRIDGE_SDA_PIN, GPIO_OUT);
        sleep_us(5);
        gpio_set_dir(BRIDGE_SDA_PIN, GPIO_IN);
        sleep_us(5);
    }
    bus_setup();
}

enum { X_OK = 0, X_NAK, X_ARB, X_TIMEOUT, X_ABRT };

// Write wlen bytes (optional), then repeated-START read rlen bytes (optional); STOP at end.
static int i2c_xfer(uint8_t addr, const uint8_t *w, size_t wlen, uint8_t *r, size_t rlen,
                    uint32_t *abrt_out) {
    i2c_hw_t *hw = i2c_get_hw(BRIDGE_I2C);
    const size_t total = wlen + rlen;

    hw->enable = 0;
    hw->tar = addr;
    hw->enable = 1;
    (void)hw->clr_intr;  // clear stale STOP_DET / TX_ABRT etc.

    // Generous timeout: 4x nominal byte time per byte plus 20 ms slack (covers waiting
    // for a busy bus held by another master, and clock stretching).
    uint32_t byte_us = (uint32_t)(9ull * 1000000ull / (g_baud ? g_baud : 100000));
    absolute_time_t deadline = make_timeout_time_us(20000 + (uint64_t)(total + 1) * byte_us * 4);

    size_t issued = 0, received = 0;
    uint32_t abrt = 0;

    for (;;) {
        uint32_t raw = hw->raw_intr_stat;
        if (raw & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) {
            abrt = hw->tx_abrt_source;
            break;
        }
        // Queue commands while there is TX FIFO room (and RX room for outstanding reads).
        while (issued < total && i2c_get_write_available(BRIDGE_I2C) > 0) {
            bool is_read = issued >= wlen;
            if (is_read && (issued - wlen) - received >= 8) break;
            uint32_t cmd = 0;
            if (is_read) {
                cmd |= I2C_IC_DATA_CMD_CMD_BITS;
                if (issued == wlen && wlen > 0) cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
            } else {
                cmd |= w[issued];
            }
            if (issued == total - 1) cmd |= I2C_IC_DATA_CMD_STOP_BITS;
            hw->data_cmd = cmd;
            issued++;
        }
        while (received < rlen && i2c_get_read_available(BRIDGE_I2C) > 0)
            r[received++] = (uint8_t)hw->data_cmd;

        // Done once everything is queued/received, our STOP has gone out (STOP_DET) and the
        // controller itself is idle (guards against a STOP from another master).
        if (issued == total && received == rlen && (raw & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) &&
            (hw->status & I2C_IC_STATUS_TFE_BITS) && !(hw->status & I2C_IC_STATUS_MST_ACTIVITY_BITS))
            break;
        if (time_reached(deadline)) {
            *abrt_out = 0;
            return X_TIMEOUT;
        }
        tight_loop_contents();
    }

    if (abrt) {
        (void)hw->clr_tx_abrt;
        // Hardware issues STOP on abort (if it owned the bus); wait briefly for it.
        absolute_time_t t = make_timeout_time_us(2000);
        while (!(hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) && !time_reached(t))
            tight_loop_contents();
    }
    (void)hw->clr_stop_det;
    *abrt_out = abrt;

    if (!abrt) return X_OK;
    if (abrt & I2C_IC_TX_ABRT_SOURCE_ARB_LOST_BITS) return X_ARB;
    if (abrt & (I2C_IC_TX_ABRT_SOURCE_ABRT_7B_ADDR_NOACK_BITS |
                I2C_IC_TX_ABRT_SOURCE_ABRT_TXDATA_NOACK_BITS |
                I2C_IC_TX_ABRT_SOURCE_ABRT_GCALL_NOACK_BITS))
        return X_NAK;
    return X_ABRT;
}

// ---------------------------------------------------------------- protocol

static void reply_status(int st, uint32_t abrt) {
    switch (st) {
    case X_NAK: puts("NAK"); break;
    case X_ARB: puts("ERR ARB"); break;
    case X_TIMEOUT: puts("ERR TIMEOUT"); break;
    default: printf("ERR ABRT 0x%08lx\n", (unsigned long)abrt); break;
    }
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse hex digits (whitespace between bytes tolerated) from s into buf. Returns byte count,
// or -1 on bad hex / odd digit count, -2 if too long.
static int parse_hex(const char *s, uint8_t *buf, size_t max) {
    size_t n = 0;
    int hi = -1;
    for (; *s; s++) {
        if (isspace((unsigned char)*s)) continue;
        int v = hexval((unsigned char)*s);
        if (v < 0) return -1;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= max) return -2;
            buf[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    return hi < 0 ? (int)n : -1;
}

static bool parse_addr(const char *tok, uint8_t *addr) {
    if (!tok || !*tok || strlen(tok) > 2) return false;
    char *end;
    unsigned long a = strtoul(tok, &end, 16);
    if (*end || a > 0x7f) return false;
    *addr = (uint8_t)a;
    return true;
}

static uint8_t g_wbuf[MAX_DATA], g_rbuf[MAX_DATA];

static void handle_line(char *line) {
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;
    led_activity();
    if (strlen(cmd) != 1) { puts("ERR CMD"); return; }

    switch (toupper((unsigned char)cmd[0])) {
    case 'P':
        printf("PONG a2b-i2c-bridge v1 %lu\n", (unsigned long)g_baud);
        break;

    case 'B': {
        char *tok = strtok_r(NULL, " \t", &save);
        char *end;
        unsigned long khz = tok ? strtoul(tok, &end, 10) : 0;
        if (!tok || *end || khz < 10 || khz > 1000) { puts("ERR ARG"); break; }
        g_baud = i2c_set_baudrate(BRIDGE_I2C, (uint)(khz * 1000));
        printf("OK %lu\n", (unsigned long)g_baud);
        break;
    }

    case 'W': {
        uint8_t addr;
        if (!parse_addr(strtok_r(NULL, " \t", &save), &addr)) { puts("ERR ADDR"); break; }
        int n = parse_hex(save ? save : "", g_wbuf, MAX_DATA);
        if (n == -2) { puts("ERR LEN"); break; }
        if (n <= 0) { puts("ERR DATA"); break; }
        uint32_t abrt;
        int st = i2c_xfer(addr, g_wbuf, (size_t)n, NULL, 0, &abrt);
        if (st == X_OK) puts("OK");
        else reply_status(st, abrt);
        if (st == X_TIMEOUT) bus_recover();
        break;
    }

    case 'R': {
        uint8_t addr;
        if (!parse_addr(strtok_r(NULL, " \t", &save), &addr)) { puts("ERR ADDR"); break; }
        char *tok = strtok_r(NULL, " \t", &save);
        char *end;
        unsigned long nread = tok ? strtoul(tok, &end, 10) : 0;
        if (!tok || *end || nread < 1 || nread > MAX_DATA) { puts("ERR LEN"); break; }
        const char *rest = save ? save : "";
        while (isspace((unsigned char)*rest)) rest++;
        int nw = 0;
        if (strcmp(rest, "-") != 0) {
            nw = parse_hex(rest, g_wbuf, MAX_DATA);
            if (nw == -2) { puts("ERR LEN"); break; }
            if (nw <= 0) { puts("ERR DATA"); break; }  // use '-' for a plain read
        }
        uint32_t abrt;
        int st = i2c_xfer(addr, g_wbuf, (size_t)nw, g_rbuf, nread, &abrt);
        if (st == X_OK) {
            static char out[3 + 2 * MAX_DATA + 2];
            char *p = out;
            *p++ = 'O'; *p++ = 'K'; *p++ = ' ';
            static const char hx[] = "0123456789ABCDEF";
            for (unsigned long i = 0; i < nread; i++) {
                *p++ = hx[g_rbuf[i] >> 4];
                *p++ = hx[g_rbuf[i] & 15];
            }
            *p = 0;
            puts(out);
        } else {
            reply_status(st, abrt);
            if (st == X_TIMEOUT) bus_recover();
        }
        break;
    }

    default:
        puts("ERR CMD");
        break;
    }
    fflush(stdout);
}

int main(void) {
    stdio_init_all();
    led_init();
    g_baud = 0;
    bus_setup();

    static char line[LINE_MAX];
    size_t len = 0;
    bool overflow = false;

    for (;;) {
        led_poll();
        int c = getchar_timeout_us(1000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') {
            if (overflow) {
                puts("ERR LEN");
                fflush(stdout);
            } else if (len > 0) {
                line[len] = 0;
                handle_line(line);
            }
            len = 0;
            overflow = false;
            continue;
        }
        if (len < LINE_MAX - 1) line[len++] = (char)c;
        else overflow = true;
    }
}
