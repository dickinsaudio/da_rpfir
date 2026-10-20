# A2B I2C web tunnel (experimental)

Runs the debug web server over the A2B control bus, so each sub-node's pages
appear on the PC at `http://localhost:8080+node/`.

The tunnel lives in the `a2b_master` repo, pulled in as the submodule
`extern/a2b_master`:

| where | what |
|---|---|
| `extern/a2b_master/device/` | Device side compiled into this firmware: virtual W5500, I2C mailbox protocol, RP2350 I2C0 slave. Design notes and hardware risks are in its README. |
| `extern/a2b_master/docs/TUNNEL_PROTOCOL.md` | Wire protocol. |
| `extern/a2b_master/app/`, `firmware/` | PC tool and the Pico main-node bridge. |

What stays in this repo:

* The integration, guarded by `A2B_TUNNEL` (a CMake option, ON by default on
  this branch): `w5500_udp.cpp` (probe and start, skip DHCP), `peripherals.cpp`
  (weak `i2c_master_acquire/release` hooks around amp transactions),
  `CMakeLists.txt`.
* `test/`: the native Linux harness. It runs this repo's **real** HttpServer and
  the real ioLibrary behind the virtual W5500, with a TCP stand-in for the I2C
  link. `a2b_master` uses it for its end-to-end tests.

```
git submodule update --init extern/a2b_master
a2b_tunnel/test/build_native.sh /tmp/a2bsim
/tmp/a2bsim/native_sim --port 9500 --byte-us 50 --nack-rate 0.02
a2b_master -no-web -tunnel sim:127.0.0.1:9500 -nodes 0
# browse http://localhost:8080/big.html   (also index.html, chunks.txt)
```
