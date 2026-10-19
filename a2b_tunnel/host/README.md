# A2B I2C web tunnel - host side

This lets a browser on the PC reach the HTTP server running on the RP2350 of an A2B
sub-node. The RP2350's W5500 sockets are tunnelled over I2C, as virtual sockets
reached through the main AD242x. Wire protocol: [`../PROTOCOL.md`](../PROTOCOL.md).

| file | purpose |
|---|---|
| `a2b_web_tunnel.py` | the app: one TCP listener per node, polling scheduler, protocol |
| `transports.py` | `sim:` / `serial:` / `usbi` transports, AD242x NODEADR/CHIP handling |
| `tunnel_proto.py` | op codes, flags, CRC-8 (shared with the simulator) |
| `sim_device.py` | pure-Python device simulator (3 sockets/node, toy web server) |
| `test_e2e.py` | end-to-end test: simulator + tunnel + parallel fetches, byte-exact check |

Requires Python 3.10+ and the standard library only. You need `pyserial` (`pip install pyserial`)
only for the `serial:` transport.

## Usage (Windows)

```
py a2b_web_tunnel.py --transport serial:COM5 --nodes 0,1,2
```
Then browse to `http://127.0.0.1:8080/` for node 0, `:8081` for node 1, and so on. Ctrl-C quits.
At start-up the app sends `RESET_ALL`, which aborts any tunnel connections left over from a
previous run.

Useful options:

* `--base-port 8080`, `--bind 127.0.0.1`. Use `--bind 0.0.0.0` to expose the ports on the LAN.
* `--chunk 64`: DATA/READ payload per transaction, up to 240. A larger chunk is more efficient
  on the bus, but each transaction then holds the bus longer.
* `--idle-poll-ms 50`: SUMMARY rate for idle nodes. Nodes with traffic are polled back to back.
* `--retries 8 --backoff-ms 1`: retries per transaction on a NACK, a CRC error or a timeout.
* `--a2b-base 0x68 --tunnel-addr 0x2A --no-400k`: AD242x addressing for `serial:` / `usbi`.
* `-v`: logs every transaction. `--stats-s 5`: sets the stats line interval.

Stats line (one block per node):
```
[stats] n0 conn 11 (act 1 q 0) up 392B down 51.2kB (12.2 kB/s) 226 txn/s retry 50 [nack 50 crc 0 io 0 fail 0]
```
The fields are: connections total (`act` = currently paired, `q` = waiting for a device
socket in LISTEN), bytes in each direction, download rate and transaction rate over the last
interval, and retries split by cause (`fail` = transactions that failed after every retry).

## Transports

* `sim:HOST:PORT` connects over TCP to `sim_device.py` or to the native C harness (default port
  9500). It works at node level, with no AD242x layer.
* `serial:COM5` / `serial:/dev/ttyACM0` uses a USB-CDC I2C bridge (Pico) with the line protocol
  `W aa hex` / `R aa n hex|-` / `P`. This transport is wrapped in `A2BNodeTransport`, which writes
  `NODEADR=N`, then `CHIP=0x2A` and (optionally) `I2CCFG|=1` once per node, then `NODEADR=0x20|N`.
  The current NODEADR value is cached. Note that writing CHIP re-points SigmaStudio peripheral
  access on that node to the tunnel.
* `usbi` is a stub. The USBi API is still under investigation.

## Testing without hardware (Linux or Windows)

```
python3 sim_device.py --nack-rate 0.05 --byte-us 25          # terminal 1
python3 a2b_web_tunnel.py --transport sim:127.0.0.1:9500 --nodes 0,1   # terminal 2
curl http://127.0.0.1:8080/index.html                         # terminal 3
```
To run the scripted test:
```
python3 test_e2e.py --nack-rate 0.05 --corrupt-rate 0.02 --byte-us 25
```
Simulator fault injection: `--nack-rate` (half of the NACKs come *after* the op took effect,
which exercises idempotency), `--corrupt-rate` (bit flips in either phase, which exercises the
CRC checks and resync), and `--byte-us` / `--txn-us` (bus timing). On Windows, `time.sleep`
granularity makes `--byte-us` timing coarse.

## Expected throughput

These figures are from the simulator. The page is about 5 kB, served in 128-byte pieces.

| setup | single fetch | 6 parallel (2 nodes) |
|---|---|---|
| no bus delay (protocol/CPU bound) | ~0.5 MB/s | ~0.75 MB/s |
| `--byte-us 25` (400 kHz-ish), 5 % NACK, chunk 64 | ~26 kB/s | ~27 kB/s |
| `--byte-us 25`, chunk 240 | ~31 kB/s | ~32 kB/s |
| `--byte-us 25 --txn-us 1000` (1 ms per-transaction latency) | ~17 kB/s | ~19 kB/s |

The bus is shared, so the aggregate rate is bounded by the I2C rate, not by the number of
nodes. With 64-byte chunks a download costs about 1.1 transactions per chunk. The bus is close
to 80 % payload: about 79 bytes on the wire per 64 data bytes.

On real hardware every transaction also pays the A2B remote-access latency and the USB-CDC
round trip of the bridge (roughly 0.2-1 ms per command). Expect something like
**10-20 kB/s** per tunnel, shared between all nodes. A simple status page should load in well
under a second. Raise `--chunk` if per-transaction latency dominates.

## Limitations

* The protocol has no TCP half-close. When the browser sends FIN, the app first delivers the
  pending upload and then sends `CLOSE`. Any response that has not been read yet is discarded,
  as PROTOCOL.md specifies. Browsers and curl never half-close, so this does not affect normal
  use.
* Only IPv4 browser addresses are passed to CONNECT. Other addresses are sent as 0.0.0.0, which
  is informational only.
