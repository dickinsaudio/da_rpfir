"""I2C transports for the A2B web tunnel host app.

Two levels of interface:

* Node level (what the tunnel app uses)::

      write(node, data)                 -> None
      write_read(node, data, n)         -> bytes (exactly n)

  `data` is a complete tunnel transaction write phase (op, args, crc).
  `write_read` is write + (repeated-start) read of `n` bytes.

* Raw I2C level (a plain host I2C master)::

      i2c_write(addr7, data)            -> None
      i2c_write_read(addr7, data, n)    -> bytes (exactly n)

  `A2BNodeTransport` turns a raw I2C master connected to the main AD242x into a
  node-level transport by doing the NODEADR/CHIP dance from PROTOCOL.md.

All methods are blocking and raise `I2CNack` (address/data NACK - just retry) or
`I2CError` (anything else: timeout, bridge error, lost connection) on failure.
None of the transports is thread safe on its own; the app serialises access.

URL forms accepted by `open_transport()`:
    sim:HOST:PORT        TCP framing to sim_device.py / the native C harness
    serial:COM5          USB-CDC I2C bridge (pyserial) -> A2BNodeTransport
    serial:/dev/ttyACM0
    usbi                 ADI USBi (not implemented yet)
"""

import socket
import struct


class I2CError(Exception):
    """Transaction failed (timeout, bus error, bridge error, ...)."""


class I2CNack(I2CError):
    """Address or data byte was NACKed. Normal for the tunnel device; retry."""


# ---------------------------------------------------------------------------
# Node-level transports
# ---------------------------------------------------------------------------
class NodeTransport:
    def write(self, node: int, data: bytes) -> None:
        raise NotImplementedError

    def write_read(self, node: int, data: bytes, n: int) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def describe(self) -> str:
        return self.__class__.__name__


class SimTransport(NodeTransport):
    """Talks to a device simulator over TCP.

    Request : [kind u8 'W'(0x57)|'R'(0x52)][node u8][wlen u16 LE][rlen u16 LE][wdata]
    Response: [status u8 0=ACK 1=NACK][rdata: rlen bytes, only if ACK]

    For 'R' with wlen > 0 the simulator performs write, repeated start, read.
    """

    KIND_W = 0x57
    KIND_R = 0x52

    def __init__(self, host: str = "127.0.0.1", port: int = 9500, timeout: float = 1.0):
        self._addr = (host, port)
        self._timeout = timeout
        self._sock = None

    def describe(self):
        return "sim:%s:%d" % self._addr

    def _connect(self):
        try:
            s = socket.create_connection(self._addr, timeout=self._timeout)
        except OSError as e:
            raise I2CError("sim connect %s:%d: %s" % (self._addr + (e,)))
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.settimeout(self._timeout)
        self._sock = s

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise OSError("simulator closed connection")
            buf += chunk
        return bytes(buf)

    def _xfer(self, kind: int, node: int, wdata: bytes, rlen: int) -> bytes:
        if self._sock is None:
            self._connect()
        try:
            self._sock.sendall(struct.pack("<BBHH", kind, node, len(wdata), rlen) + wdata)
            status = self._recv_exact(1)[0]
            if status == 1:
                raise I2CNack("NACK")
            if status != 0:
                raise OSError("bad sim status byte 0x%02x" % status)
            return self._recv_exact(rlen) if rlen else b""
        except I2CNack:
            raise
        except OSError as e:          # includes socket.timeout
            self.close()
            raise I2CError("sim transport: %s" % e)

    def write(self, node, data):
        self._xfer(self.KIND_W, node, bytes(data), 0)

    def write_read(self, node, data, n):
        return self._xfer(self.KIND_R, node, bytes(data), n)

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None


class A2BNodeTransport(NodeTransport):
    """Reach the tunnel device on sub-node N through the main AD242x.

    BASE (0x68 default) is the AD242x register address, BUS = BASE+1 is the
    address that is forwarded to the selected node (its registers when PERI=0,
    its remote peripheral `CHIP` when PERI=1). Sequence (PROTOCOL.md):

      1. BASE: NODEADR(0x01) = N
      2. BUS : CHIP(0x00)    = tunnel_addr          (once per node)
      3. BUS : I2CCFG(0x3F) |= 0x01 (400 kHz)       (optional, once per node)
      4. BASE: NODEADR(0x01) = 0x20 | N             (PERI=1)
      5. tunnel transactions -> BUS

    The current NODEADR is cached so steady-state traffic to one node costs
    nothing extra; switching nodes costs one BASE write.
    """

    REG_CHIP = 0x00
    REG_NODEADR = 0x01
    REG_I2CCFG = 0x3F
    NODEADR_PERI = 0x20

    def __init__(self, raw, base_addr: int = 0x68, tunnel_addr: int = 0x2A, set_400k: bool = True):
        self.raw = raw
        self.base = base_addr
        self.bus = base_addr + 1
        self.tunnel_addr = tunnel_addr
        self.set_400k = set_400k
        self._nodeadr = None      # cached NODEADR value, None = unknown
        self._setup_done = set()  # nodes whose CHIP (and I2CCFG) are programmed

    def describe(self):
        return "a2b(%s, base=0x%02X, chip=0x%02X)" % (self.raw.describe(), self.base, self.tunnel_addr)

    def _set_nodeadr(self, value):
        if self._nodeadr != value:
            self._nodeadr = None  # unknown until the write is known to have worked
            self.raw.i2c_write(self.base, bytes([self.REG_NODEADR, value]))
            self._nodeadr = value

    def _select(self, node: int):
        if not 0 <= node <= 15:
            raise ValueError("A2B node number must be 0..15")
        if node not in self._setup_done:
            self._set_nodeadr(node)                       # node registers
            self.raw.i2c_write(self.bus, bytes([self.REG_CHIP, self.tunnel_addr]))
            if self.set_400k:
                cfg = self.raw.i2c_write_read(self.bus, bytes([self.REG_I2CCFG]), 1)[0]
                if not cfg & 0x01:
                    self.raw.i2c_write(self.bus, bytes([self.REG_I2CCFG, cfg | 0x01]))
            self._setup_done.add(node)
        self._set_nodeadr(self.NODEADR_PERI | node)       # peripheral access

    def _guard(self, fn):
        try:
            return fn()
        except I2CNack:
            raise                    # NACK from the tunnel device: state is fine
        except I2CError:
            self._nodeadr = None     # be safe: re-write NODEADR next time
            raise

    def write(self, node, data):
        def go():
            self._select(node)
            self.raw.i2c_write(self.bus, bytes(data))
        self._guard(go)

    def write_read(self, node, data, n):
        def go():
            self._select(node)
            return self.raw.i2c_write_read(self.bus, bytes(data), n)
        return self._guard(go)

    def close(self):
        self.raw.close()


# ---------------------------------------------------------------------------
# Raw I2C masters
# ---------------------------------------------------------------------------
class RawI2C:
    def i2c_write(self, addr7: int, data: bytes) -> None:
        raise NotImplementedError

    def i2c_write_read(self, addr7: int, data: bytes, n: int) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass

    def describe(self) -> str:
        return self.__class__.__name__


class SerialBridgeRaw(RawI2C):
    """I2C master behind a USB-CDC line protocol (e.g. a Pico).

      W <addr hex2> <data hex>\\n                -> OK | NAK | ERR <text>
      R <addr hex2> <nread dec> <wdata hex|->\\n  -> OK <hex> | NAK | ERR <text>
      P\\n                                        -> PONG ...
    """

    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.5):
        try:
            import serial  # pyserial, imported lazily so other transports work without it
        except ImportError:
            raise RuntimeError("serial transport needs pyserial:  pip install pyserial")
        self.port = port
        try:
            self._ser = serial.Serial(port, baud, timeout=timeout, write_timeout=timeout)
        except serial.SerialException as e:
            raise RuntimeError("cannot open %s: %s" % (port, e))
        self._ser.reset_input_buffer()
        pong = self._cmd("P")
        if not pong.startswith("PONG"):
            raise RuntimeError("%s: no I2C bridge detected (got %r)" % (port, pong))
        self.banner = pong

    def describe(self):
        return "serial:%s" % self.port

    def _cmd(self, line: str) -> str:
        try:
            self._ser.write(line.encode("ascii") + b"\n")
            resp = self._ser.readline()
        except Exception as e:  # SerialException / OSError / timeout
            raise I2CError("serial: %s" % e)
        if not resp.endswith(b"\n"):
            self._ser.reset_input_buffer()     # resync after a timeout
            raise I2CError("serial bridge timeout (%r)" % resp)
        return resp.decode("ascii", "replace").strip()

    @staticmethod
    def _check(resp: str) -> str:
        if resp == "NAK":
            raise I2CNack("NAK")
        if resp.startswith("ERR"):
            raise I2CError("bridge: " + resp)
        if resp == "OK" or resp.startswith("OK "):
            return resp[3:].strip()
        raise I2CError("bridge: unexpected reply %r" % resp)

    def i2c_write(self, addr7, data):
        self._check(self._cmd("W %02X %s" % (addr7, bytes(data).hex().upper())))

    def i2c_write_read(self, addr7, data, n):
        w = bytes(data).hex().upper() if data else "-"
        hexdata = self._check(self._cmd("R %02X %d %s" % (addr7, n, w)))
        try:
            rd = bytes.fromhex(hexdata)
        except ValueError:
            raise I2CError("bridge: bad hex %r" % hexdata)
        if len(rd) != n:
            raise I2CError("bridge: asked %d bytes, got %d" % (n, len(rd)))
        return rd

    def close(self):
        try:
            self._ser.close()
        except Exception:
            pass


class USBiRaw(RawI2C):
    """Placeholder for the ADI USBi (EVAL-ADUSB2EBZ)."""

    def __init__(self, *args, **kwargs):
        raise NotImplementedError(
            "USBi transport is not implemented yet: the USBi host API is still under "
            "investigation. Use 'serial:<port>' with the Pico I2C bridge, or 'sim:host:port'.")


# ---------------------------------------------------------------------------
def open_transport(url: str, base_addr: int = 0x68, tunnel_addr: int = 0x2A,
                   set_400k: bool = True, baud: int = 115200) -> NodeTransport:
    """Create a node-level transport from a URL (see module docstring)."""
    if url.startswith("sim:") or url == "sim":
        rest = url[4:]
        host, _, port = rest.rpartition(":") if ":" in rest else (rest, "", "")
        return SimTransport(host or "127.0.0.1", int(port) if port else 9500)
    if url.startswith("serial:"):
        raw = SerialBridgeRaw(url[len("serial:"):], baud=baud)
        return A2BNodeTransport(raw, base_addr, tunnel_addr, set_400k)
    if url == "usbi" or url.startswith("usbi:"):
        return A2BNodeTransport(USBiRaw(url), base_addr, tunnel_addr, set_400k)
    raise ValueError("unknown transport URL %r (use sim:HOST:PORT, serial:PORT or usbi)" % url)
