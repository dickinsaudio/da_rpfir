# pico_i2c_bridge — USB-CDC to I2C master for an AD242x A2B main node

Tiny Raspberry Pi Pico (RP2040) / Pico 2 (RP2350) firmware that lets a PC drive the
**host I2C port** of an Analog Devices AD242x A2B main node over a USB serial port, in
place of (or alongside) an ADI USBi.

## Wiring

| Pico pin            | A2B main-node eval board I2C header |
|---------------------|-------------------------------------|
| GP4 (pin 6) — SDA   | SDA                                 |
| GP5 (pin 7) — SCL   | SCL                                 |
| GND (e.g. pin 8)    | GND                                 |

- Logic level is 3.3 V. Do not connect the Pico 3V3 or VBUS to the board; power each side
  separately and share only GND.
- The Pico's internal pull-ups (~50 kΩ) are enabled but are far too weak for 400 kHz.
  **External 2k2–4k7 pull-ups to 3.3 V on SDA/SCL are expected** — the A2B eval board
  normally fits them on its host I2C bus. If you use a bare AD242x, add them.
- **USBi / multi-master:** best to leave the USBi unplugged (or SigmaStudio idle) while
  using the bridge. Both can share the bus — the controller arbitrates and the bridge reports
  `ERR ARB` if it loses arbitration (just retry) — but neither side knows about the
  other's multi-transaction sequences (e.g. a register-pointer write followed by a read),
  so interleaving can still corrupt a sequence. A transaction blocked by a bus held by the
  other master ends in `ERR TIMEOUT`.
- Pins/instance can be changed at configure time: `-DBRIDGE_I2C_INDEX=1 -DBRIDGE_SDA_PIN=2
  -DBRIDGE_SCL_PIN=3` (must be a valid I2Cn SDA/SCL pin pair), and the power-on speed with
  `-DBRIDGE_DEFAULT_KHZ=100`.

AD242x 7-bit addresses: the main node's own registers are at `BASE_ADDR` (typically `0x68`)
and the bus/sub-node window at `BUS_ADDR` (typically `0x69`), depending on its ADR pins.

## Build

```sh
export PICO_SDK_PATH=/path/to/pico-sdk          # SDK 2.x
cmake -S . -B /tmp/bridge_build -DPICO_BOARD=pico     # or pico2
make -C /tmp/bridge_build -j
```

SDK 2.x needs `picotool` to make the `.uf2`. If none is installed, the SDK downloads and
builds it (needs network); to reuse an existing one pass
`-Dpicotool_DIR=<dir containing picotoolConfig.cmake>`.

## Flashing

Hold BOOTSEL while plugging the Pico in; it mounts as `RPI-RP2` (`RP2350` for Pico 2).
Copy `pico_i2c_bridge.uf2` onto it. Or: `picotool load -x pico_i2c_bridge.uf2`.
The device then enumerates as a USB CDC serial port (`/dev/ttyACM*`, or `COMx` on
Windows). The serial baud rate setting is irrelevant.

## Protocol

ASCII lines terminated by `\n` or `\r\n`; hex is case-insensitive. Replies end in `\r\n`
and hex in replies is uppercase. At most 300 data bytes per command.

| Command                     | Action                                                     | Reply                     |
|-----------------------------|------------------------------------------------------------|---------------------------|
| `P`                         | ping                                                       | `PONG a2b-i2c-bridge v1 <baud Hz>` |
| `W <addr> <hex>`            | write bytes, then STOP                                     | `OK` / `NAK` / `ERR …`    |
| `R <addr> <n> <hex>`        | write `<hex>` (no STOP), repeated START, read `n` bytes, STOP | `OK <hex>` / `NAK` / `ERR …` |
| `R <addr> <n> -`            | plain read of `n` bytes                                    | `OK <hex>` / `NAK` / `ERR …` |
| `B <kHz>`                   | set bus speed (10–1000, default 400)                       | `OK <actual Hz>`          |

`<addr>` is the 7-bit address in hex (`00`–`7F`), `<n>` is decimal. Spaces inside the hex
data are tolerated. Errors: `NAK` (address or data byte not acknowledged), `ERR ARB`
(arbitration lost), `ERR TIMEOUT` (bus stuck/busy; the bridge then attempts a 9-clock bus
recovery and re-initialises the controller), `ERR ABRT 0x<abort source>` (other controller
abort), `ERR ADDR`, `ERR LEN`, `ERR DATA`, `ERR ARG`, `ERR CMD` (parse errors).
The LED blinks on each command.

## Quick manual test

```sh
picocom -c --omap crlf /dev/ttyACM0      # or: screen /dev/ttyACM0, PuTTY, minicom
```

```
P
PONG a2b-i2c-bridge v1 400000
R 68 1 02              <- AD242x VENDOR register (0x02): expect OK AD
OK AD
R 68 3 02              <- VENDOR, PRODUCT, VERSION
OK AD2...
R 50 1 00              <- nothing at 0x50
NAK
B 100
OK 100000
```

(`-c` enables local echo.) From Python, `pyserial` with `readline()` and `.strip()` is all
that's needed.
