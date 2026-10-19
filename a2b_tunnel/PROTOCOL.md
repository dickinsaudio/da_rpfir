# A2B I2C web tunnel — wire protocol v1

The RP2350 on an A2B sub-node is an I2C **slave** (default 7-bit address `0x2A`) on the
node's local I2C bus. The AD242x on that node is the I2C **master** of that bus and forwards
host transactions arriving over A2B ("remote peripheral access"). The host (PC) therefore
sees the RP2350 as a register-style I2C device.

The device exposes up to 8 *virtual TCP sockets* (the W5500 socket numbers 0..7 that the
web server already uses). The PC accepts real TCP connections (e.g. browser → `localhost:8080+node`)
and pairs each one with a device socket that is in LISTEN.

All multi-byte fields are **little-endian**. `crc8` = CRC-8, poly 0x07, init 0x00, no
reflection, no xor-out, computed over every preceding byte of the same transaction phase
(for writes this includes the opcode byte).

## Transactions

* **Write op**: `START addr+W  [op] [args...] [crc8]  STOP`
* **Read op**:  `START addr+W  [op] [args...] [crc8]  RESTART addr+R  [response...]  STOP`
  (a STOP + separate START before the read is also accepted).
* A read with no preceding read-op returns the `SUMMARY` response.
* Every op is **idempotent** — the PC may (and should) blindly retry on NACK, bus error or
  bad CRC. The device may NACK its address at any time (it is briefly an I2C master while it
  talks to the amplifier); the PC retries after ~1 ms.
* Writes with a bad CRC or unknown op are silently ignored (PC notices via status and retries).

## Ops

| op   | name        | write args (after op, before crc)              | response (read)                                  |
|------|-------------|------------------------------------------------|--------------------------------------------------|
| 0x01 | SUMMARY     | –                                              | 6 bytes, see below                               |
| 0x02 | SOCK_STATUS | `sn`                                           | 14 bytes, see below                              |
| 0x10 | CONNECT     | `sn cid ip0 ip1 ip2 ip3 port_lo port_hi`       | – (write only)                                   |
| 0x11 | DATA        | `sn cid off_lo off_hi data...` (≤ 240 data)    | – (write only)                                   |
| 0x12 | READ        | `sn cid ack_lo ack_hi maxlen` (maxlen ≤ 240)   | `sn cid off_lo off_hi len data[len] crc8`        |
| 0x13 | CLOSE       | `sn cid`                                       | – (write only)                                   |
| 0x1F | RESET_ALL   | `0xA5`                                         | – (write only)                                   |

### SUMMARY response (6 bytes)
```
[0] 0xA2 magic
[1] 0x01 protocol version
[2] listen_mask   bit n = socket n is LISTEN and free (a CONNECT will succeed)
[3] active_mask   bit n = socket n has a tunnel connection (conn_active)
[4] attn_mask     bit n = socket n has tx data for the PC, or local_fin set
[5] crc8
```

### SOCK_STATUS response (14 bytes)
```
[0]  sn
[1]  sr          W5500 Sn_SR value as the web server sees it
                 (0x00 CLOSED, 0x13 INIT, 0x14 LISTEN, 0x17 ESTABLISHED, 0x1C CLOSE_WAIT ...)
[2]  flags       bit0 conn_active   a tunnel connection owns this socket
                 bit1 local_fin     device side has disconnected/closed (drain tx, then CLOSE)
                 bit2 peer_fin      PC has sent CLOSE for this connection
                 bit3 listen_ready  same as SUMMARY.listen_mask bit
                 bit7 virtual       socket is routed to the tunnel at all
[3]  cid         connection id of the current/lingering connection (0 = none)
[4-5]  port      local listening port (e.g. 80)
[6-7]  rx_next   stream offset of the next byte the device expects from the PC
[8-9]  rx_free   bytes of space in the device receive buffer
[10-11] tx_base  stream offset of the first byte not yet acknowledged by the PC
[12]   tx_avail  min(255, bytes available for the PC to read from tx_base)
[13]   crc8
```

### Connection life cycle (one socket)

Stream offsets are 16-bit and start at 0 for each connection (both directions).

1. PC sees `listen_mask` bit n. It picks a `cid` (1..255, change it every connection) and
   sends `CONNECT n cid ip port` (ip/port of the browser, informational only).
   Device: if listen_ready → `sr=ESTABLISHED`, `conn_active`, pointers reset. If already
   active with the same `cid` → no-op. Otherwise ignored. PC confirms with SOCK_STATUS (`cid` matches).
2. PC → device data: `DATA n cid off bytes`. Device accepts the bytes at stream offsets
   ≥ `rx_next` that fit in `rx_free`; duplicates are dropped. PC advances its send pointer
   from the `rx_next` reported by SOCK_STATUS (so it re-sends anything not taken).
3. Device → PC data: `READ n cid ack maxlen`. `ack` = PC has everything before stream offset
   `ack`; device frees it. Response returns up to `maxlen` bytes starting at `off`
   (= new tx_base). PC sets `ack = off + len` for its next READ. If `cid` does not match the
   response has `len=0` and `cid` = the device's current cid (0 if none).
4. Device closes (web server finished): `local_fin` is set. The tx data stays readable
   ("lingers") even though the web server sees the socket CLOSED and re-opens/listens. The
   socket is not `listen_ready` until the connection is released.
   PC drains tx (tx_avail == 0 after final ack), closes the browser TCP connection, then
   sends `CLOSE n cid` → connection released.
5. Browser closes first: PC sends `CLOSE n cid` → `peer_fin`, `sr=CLOSE_WAIT`; the web
   server will disconnect; any unread tx data is discarded and the connection is released.

`RESET_ALL 0xA5` aborts every tunnel connection (PC app start-up).

## Host-side A2B addressing (AD242x)

Reaching node N's local I2C bus from the host I2C port of the main AD242x
(BASE = 0x68 default, BUS = BASE+1):

1. `BASE: NODEADR(0x01) = N`            (select node N's registers)
2. `BUS:  CHIP(0x00)    = 0x2A`         (that node's remote-peripheral address; once per node)
3. `BUS:  I2CCFG(0x3F)` DATARATE bit0=1 → 400 kHz on the sub-node bus (optional, once)
4. `BASE: NODEADR(0x01) = 0x20 | N`     (PERI=1: BUS address now reaches the peripheral)
5. Tunnel transactions go to BUS address.

Cache the NODEADR value and only rewrite it when the node changes.
Note: CHIP is a single register per node, so this re-points any SigmaStudio peripheral
access on that node (e.g. to the amp at 0x39) to the tunnel.
