# A2B I2C web tunnel (experimental)

Runs the existing debug web server over the A2B control bus, so each sub-node's
pages appear on the PC at `http://localhost:8080+node/`. No W5500 is needed.

```
 browser ──TCP──► a2b_web_tunnel.py ──USB──► I2C master ──► AD242x main ══A2B══► AD242x sub-node
 (PC)            (listens 8080+node)  (Pico bridge / USBi)     (0x68/0x69)          │ local I2C (master)
                                                                                     ├── MAX98415 amp 0x39
                                                                                     └── RP2350 I2C0 slave 0x2A
                                                                                          │ tunnel_proto.c
                                                                                          │ vw5500.c (virtual W5500)
                                                                                          ▼
                                                                                     HttpServer (unchanged)
```

## Approach

**The intercept is a virtual W5500.** The web server (`daes67_lite/http_server.cpp`)
and `core_web.cpp` talk to the W5500 at register level through the ioLibrary
SPI/CS callbacks. `vw5500.c` replaces those callbacks. It decodes each SPI
frame header (address, block select, R/W) and does one of two things:

* sends the frame to the **real chip**: common registers, and sockets not owned
  by the tunnel; or
* serves it from a **virtual socket** whose RX/TX buffers the I2C tunnel fills
  and drains. Commands such as OPEN, LISTEN, SEND, RECV, DISCON and CLOSE and
  the pointer registers keep W5500 semantics.

So the web stack, its content and CGI do not change, and the same files should
move to da_squid unchanged.

**The switch-over is automatic.** `w5500_startup()` reads VERSIONR straight off
the SPI bus before the driver's PHY-link wait, which would otherwise hang:

| W5500 found? | Result |
|---|---|
| no  | Everything is virtual (link up, VERSIONR 4, DHCP skipped). Web sockets 1–3 all serve the tunnel. |
| yes | Normal Ethernet. **Socket 3** is routed to the tunnel, so Ethernet gets 2 sockets and A2B gets 1, both at the same time (`A2B_TUNNEL_SOCKETS_WITH_W5500`). |

**The I2C bus is shared with the amplifier.** The RP2350 cannot be master and
slave at once on one DW_apb_i2c instance. I2C0 is therefore a **slave by
default**, and `peripherals.cpp` switches it to master only for the length of
each amp transaction. It does this through the weak hooks
`i2c_master_acquire()` / `i2c_master_release()`: wait for any slave activity to
finish, swap mode (a few µs), and swap back afterwards. While the RP2350 is
master, a remote access gets an address NACK, and the PC retries. Every
protocol op is idempotent and CRC-protected, so retries are always safe.

**The protocol is a small mailbox.** It is defined in [PROTOCOL.md](PROTOCOL.md):
SUMMARY and SOCK_STATUS polls, CONNECT, DATA with a stream offset, READ with a
cumulative ack, and CLOSE. When the web server closes a socket, the unread TX
data "lingers" and the PC drains it, even though the server has already
re-listened on that socket.

## Files

| file | what |
|---|---|
| `vw5500.c/h` | Virtual W5500 and SPI frame mux. Portable C. |
| `tunnel_proto.c/h` | Byte-level I2C mailbox protocol (`tp_rx/tp_tx/tp_stop`). Portable C. |
| `a2b_tunnel_rp2350.c`, `a2b_tunnel.h` | I2C0 slave ISR, master/slave switching, W5500 probe, start-up. |
| `test/native_sim.cpp`, `test/build_native.sh` | Native Linux harness. Runs the **real** HttpServer, the real ioLibrary and the code above behind a TCP stand-in for the I2C link. |
| `host/a2b_web_tunnel.py` | PC app (Windows/Linux, Python 3, stdlib + optional pyserial). |
| `host/transports.py` | `sim:`, `serial:` (Pico bridge) and `usbi` (stub), plus the AD242x NODEADR/CHIP addressing. |
| `host/pico_i2c_bridge/` | Pico USB-CDC→I2C master firmware, used in place of the USBi. |
| `host/sim_device.py` | Pure-Python device model, for testing the PC app on its own. |

Project changes outside this folder are small, and each is guarded by `A2B_TUNNEL`
(a CMake option, ON by default on this branch):
`w5500_udp.cpp` (probe and start, skip DHCP), `peripherals.cpp` (weak master
hooks), `CMakeLists.txt`.

Also fixed: `ready_out()` in `http_server.cpp` byte-swapped TX_FSR without
masking, which inflated the free size by `low_byte×65536`. It overwrote unsent
TX data whenever the TX buffer was partly full. It was hidden on Ethernet
because the W5500 drains instantly, but it would bite under load.

## Throughput

* **Bus.** The sub-node I2C runs at 100 or 400 kHz (I2CCFG.DATARATE; the PC app
  sets 400k). ADI's latency table gives about **45 µs/byte** for remote writes
  at 400 kHz (`(N−1)×45 + 70 µs`), so the ceiling is about 22 kB/s. At 100 kHz
  it is about 113 µs/byte, or ~9 kB/s.
* **Measured end to end in the native harness** (real server code, bus modelled
  per byte, 2–5% random NACKs, all content byte-exact):

  | µs/byte | single 12.9 kB page | 6 parallel | small CGI page |
  |---|---|---|---|
  | 25  | 33–35 kB/s | 32–35 kB/s | 20 ms |
  | 50 (≈ real 400 kHz A2B) | 17 kB/s | 17 kB/s | 32 ms |
  | 100 (≈ 100 kHz) | 8.6 kB/s | 8.5 kB/s | 66 ms |

* **Expectation on hardware: roughly 10–17 kB/s**, shared across all nodes,
  before USB-bridge overhead. A cold first page here is about 20–25 kB (9.4 kB
  banner, 7 kB amp JS), so about 1.5–2 s; after that the banner, CSS and
  scripts are cached. Polling stats pages are small.
* **Idle cost.** A 6-byte SUMMARY about every 50 ms per node.

## Risks / to check on hardware

1. **Clock stretching.** The AD242x aborts a remote access if the far side
   stretches for more than about 0.6 ms. The ISR answers RD_REQ in µs, but long
   interrupt-off periods on core 0 (flash writes) will cause retries.
2. **Repeated start over A2B.** It is not documented whether the sub-node
   reproduces write+repeated-start+read. The firmware also accepts write, STOP,
   then read, so the PC side can split them if needed.
3. **How a remote NACK appears on the host side.** It may be a NACK, or it may
   only be an I2C error interrupt on the main node. The bridge may need to map
   it.
4. **CHIP register.** There is one per node, so pointing it at 0x2A re-points
   any SigmaStudio peripheral access on that node (e.g. to the amp).
5. **Amp bus robustness.** Mode switching adds no traffic, but the AD242x is now
   a second master on a bus that is already marginal. Arbitration loss shows up
   as a failed amp transaction (the existing retries cover reads).
6. **USBi.** It has no public generic I2C API. The candidates are the SigmaStudio
   Server scripting API (`REGISTER_WRITE_BYTES`/`REGISTER_READ_BYTES`, needs
   SigmaStudio running) or SigmaStudio+ Thrift `PeripheralReadWrite`. Both are
   unmeasured. A Pico or FT232H on the main-node I2C header is simpler and
   faster. Do A2B discovery with SigmaStudio first, then disconnect the USBi or
   leave it idle (multi-master).

## Faster paths later

* AD2428 has only I2C (plus 4-byte mailboxes). AD243x has SPI-over-distance up
  to ~10 Mbit/s.
* A spare 32-bit TDM slot each way would give ~1.5 Mbit/s, but only if the main
  node's audio reaches the PC.
* A sub-node interrupt to the main node would avoid the idle polling.

## Try it (no hardware)

```
test/build_native.sh /tmp/a2bsim
/tmp/a2bsim/native_sim --port 9500 --byte-us 50 --nack-rate 0.02
python3 host/a2b_web_tunnel.py --transport sim:127.0.0.1:9500 --nodes 0
# browse http://localhost:8080/big.html   (also index.html, chunks.txt)
```

On hardware: flash `host/pico_i2c_bridge`, wire it to the main-node I2C
header, then run `python a2b_web_tunnel.py --transport serial:COM5 --nodes 1,2`
and browse `localhost:8081` / `8082`.
