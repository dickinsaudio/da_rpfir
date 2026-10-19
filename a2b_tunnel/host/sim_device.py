#!/usr/bin/env python3
"""Pure-Python simulator of the RP2350 side of the A2B I2C web tunnel.

Implements the device side of PROTOCOL.md v1 for any number of nodes (each
node number gets independent state on first use) and serves it with the
SimTransport TCP framing:

    request : [kind 'W'|'R'][node u8][wlen u16 LE][rlen u16 LE][wdata]
    response: [status 0=ACK 1=NACK][rdata (rlen bytes, ACK only)]

Each node has virtual sockets 1,2,3 listening on port 80 with 2048-byte rx and
tx buffers, attached to a toy web server that answers any GET with a ~5 kB
deterministic HTML page (see make_response()), written in 128-byte pieces, and
then disconnects the way the firmware does: local_fin is set and the socket is
re-opened/listening immediately while the tx data lingers until the PC has
drained it and sent CLOSE.

Fault injection: --nack-rate (NACK, with or without the op having been
applied), --corrupt-rate (bit flip in write or read data -> CRC errors),
--byte-us / --txn-us (bus timing).
"""

import argparse
import random
import socket
import struct
import threading
import time

from tunnel_proto import (
    OP_SUMMARY, OP_SOCK_STATUS, OP_CONNECT, OP_DATA, OP_READ, OP_CLOSE, OP_RESET_ALL,
    OP_NAMES, RESET_ALL_KEY, SUMMARY_MAGIC, PROTO_VERSION, MAX_PAYLOAD,
    F_CONN_ACTIVE, F_LOCAL_FIN, F_PEER_FIN, F_LISTEN_READY, F_VIRTUAL,
    SR_LISTEN, SR_ESTABLISHED, SR_CLOSE_WAIT, crc8, with_crc, u16,
)

BUF_SIZE = 2048
VIRTUAL_SOCKETS = (1, 2, 3)
HTTP_PORT = 80
PIECE = 128            # web server writes its response in pieces of this size
PAGE_SIZE = 5000       # approx body size


def make_response(node: int, path: str) -> bytes:
    """The exact bytes the toy web server sends for GET <path> on <node>.

    Imported by the test so it can check content byte for byte.
    """
    lines = ["<!DOCTYPE html>\n<html><head><title>A2B tunnel node %d</title></head><body>\n" % node,
             "<h1>Node %d: %s</h1>\n" % (node, path)]
    i = 0
    size = sum(len(x) for x in lines)
    while size < PAGE_SIZE:
        line = "<p>node %d path %s line %04d %s</p>\n" % (node, path, i, "abcdefghij"[i % 10] * (i % 23))
        lines.append(line)
        size += len(line)
        i += 1
    lines.append("</body></html>\n")
    body = "".join(lines).encode()
    head = ("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n"
            "Connection: close\r\n\r\n" % len(body)).encode()
    return head + body


class HttpApp:
    """Minimal HTTP/1.x responder bound to one accepted connection."""

    def __init__(self, node):
        self.node = node
        self.req = bytearray()
        self.resp = None
        self.sent = 0

    def feed(self, data: bytes):
        if self.resp is not None:
            return                          # ignore anything after the request
        self.req += data
        end = self.req.find(b"\r\n\r\n")
        if end < 0:
            if len(self.req) > 1500:
                self.resp = b"HTTP/1.1 431 Too Large\r\nConnection: close\r\n\r\n"
            return
        parts = bytes(self.req[:end]).split(b"\r\n")[0].split()
        if len(parts) >= 2 and parts[0] == b"GET":
            self.resp = make_response(self.node, parts[1].decode("latin-1"))
        else:
            self.resp = b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"


class VSock:
    """One virtual socket: W5500-like state plus tunnel connection state."""

    def __init__(self, sn, port=HTTP_PORT):
        self.sn = sn
        self.port = port
        self.release()

    def release(self):
        """Tunnel connection released; web server has (re)opened and listens."""
        self.sr = SR_LISTEN
        self.active = False
        self.local_fin = False
        self.peer_fin = False
        self.cid = 0
        self.rx = bytearray()     # bytes received from the PC, not yet consumed by the app
        self.rx_next = 0          # 16-bit stream offset of next byte expected from PC
        self.tx = bytearray()     # bytes for the PC, starting at tx_base
        self.tx_base = 0
        self.app = None

    @property
    def listen_ready(self):
        return self.sr == SR_LISTEN and not self.active

    def flags(self):
        return (F_VIRTUAL | (F_CONN_ACTIVE if self.active else 0) | (F_LOCAL_FIN if self.local_fin else 0)
                | (F_PEER_FIN if self.peer_fin else 0) | (F_LISTEN_READY if self.listen_ready else 0))


class Device:
    """Device side of one node."""

    def __init__(self, node, log=None):
        self.node = node
        self.socks = {sn: VSock(sn) for sn in VIRTUAL_SOCKETS}
        self.pending = None       # prepared response of the last read-op
        self.log = log
        self.conns = 0

    # ---- masks / responses ------------------------------------------------
    def summary(self) -> bytes:
        listen = active = attn = 0
        for sn, s in self.socks.items():
            if s.listen_ready:
                listen |= 1 << sn
            if s.active:
                active |= 1 << sn
            if s.active and (s.tx or s.local_fin):
                attn |= 1 << sn
        return with_crc([SUMMARY_MAGIC, PROTO_VERSION, listen, active, attn])

    def sock_status(self, sn) -> bytes:
        s = self.socks.get(sn)
        if s is None:   # not a virtual socket
            return with_crc(bytes([sn & 0xFF]) + bytes(12))
        return with_crc(struct.pack("<BBBBHHHHB", sn, s.sr, s.flags(), s.cid, s.port, s.rx_next,
                                    BUF_SIZE - len(s.rx), s.tx_base, min(255, len(s.tx))))

    # ---- transaction entry points -----------------------------------------
    def write(self, data: bytes):
        """Write phase of any transaction."""
        self.pending = None
        if len(data) < 2 or crc8(data[:-1]) != data[-1]:
            return                                  # bad crc: silently ignored
        op, a = data[0], data[1:-1]
        if op == OP_SUMMARY and not a:
            self.pending = self.summary()
        elif op == OP_SOCK_STATUS and len(a) == 1:
            self.pending = self.sock_status(a[0])
        elif op == OP_CONNECT and len(a) == 8:
            self.op_connect(a[0], a[1])
        elif op == OP_DATA and 4 <= len(a) <= 4 + MAX_PAYLOAD:
            self.op_data(a[0], a[1], u16(a, 2), a[4:])
        elif op == OP_READ and len(a) == 5:
            self.pending = self.op_read(a[0], a[1], u16(a, 2), min(a[4], MAX_PAYLOAD))
        elif op == OP_CLOSE and len(a) == 2:
            self.op_close(a[0], a[1])
        elif op == OP_RESET_ALL and len(a) == 1 and a[0] == RESET_ALL_KEY:
            for s in self.socks.values():
                s.release()
        # unknown op / bad length: ignored

    def read(self, n: int) -> bytes:
        """Read phase. Without a preceding read-op this returns SUMMARY."""
        resp = self.pending if self.pending is not None else self.summary()
        self.pending = None
        return (resp + b"\xff" * n)[:n]           # slave pads with 0xFF

    # ---- ops ----------------------------------------------------------------
    def _owned(self, sn, cid):
        s = self.socks.get(sn)
        return s if s is not None and s.active and cid != 0 and s.cid == cid else None

    def op_connect(self, sn, cid):
        s = self.socks.get(sn)
        if s is None or cid == 0:
            return
        if s.active and s.cid == cid:
            return                                  # retry of a CONNECT that worked
        if s.listen_ready:
            s.release()
            s.active, s.cid, s.sr = True, cid, SR_ESTABLISHED
            s.app = HttpApp(self.node)
            self.conns += 1

    def op_data(self, sn, cid, off, data):
        s = self._owned(sn, cid)
        if s is None or s.peer_fin or s.local_fin or s.sr != SR_ESTABLISHED:
            return
        skip = (s.rx_next - off) & 0xFFFF          # bytes of this write we already have
        if skip > len(data):
            return                                  # gap (off beyond rx_next) or stale
        take = data[skip:skip + (BUF_SIZE - len(s.rx))]
        s.rx += take
        s.rx_next = (s.rx_next + len(take)) & 0xFFFF

    def op_read(self, sn, cid, ack, maxlen):
        s = self._owned(sn, cid)
        if s is None:
            cur = self.socks[sn].cid if sn in self.socks and self.socks[sn].active else 0
            base = self.socks[sn].tx_base if sn in self.socks else 0
            return with_crc(struct.pack("<BBHB", sn, cur, base, 0))
        d = (ack - s.tx_base) & 0xFFFF
        if d <= len(s.tx):                          # ignore an ack beyond what we sent
            del s.tx[:d]
            s.tx_base = (s.tx_base + d) & 0xFFFF
        data = bytes(s.tx[:maxlen])
        return with_crc(struct.pack("<BBHB", sn, cid, s.tx_base, len(data)) + data)

    def op_close(self, sn, cid):
        s = self._owned(sn, cid)
        if s is None:
            return
        if s.local_fin:
            s.release()                             # PC drained the lingering data
        elif not s.peer_fin:
            s.peer_fin = True                       # web server will notice and disconnect
            s.sr = SR_CLOSE_WAIT

    # ---- the "web server" main loop ----------------------------------------
    def tick(self):
        for s in self.socks.values():
            if not s.active:
                continue
            if s.sr == SR_CLOSE_WAIT:
                s.release()                         # disconnect; unread tx discarded
                continue
            if s.sr != SR_ESTABLISHED or s.local_fin:
                continue
            app = s.app
            if s.rx:
                app.feed(bytes(s.rx))
                s.rx.clear()
            if app.resp is None:
                continue
            while app.sent < len(app.resp):
                piece = app.resp[app.sent:app.sent + PIECE]
                if BUF_SIZE - len(s.tx) < len(piece):
                    break
                s.tx += piece
                app.sent += len(piece)
            if app.sent >= len(app.resp):
                # disconnect: W5500 socket closes and the server re-listens at once,
                # the tunnel connection lingers until the PC drained tx and sent CLOSE.
                s.local_fin = True
                s.sr = SR_LISTEN
                s.app = None


class Simulator:
    def __init__(self, args):
        self.args = args
        self.devices = {}
        self.lock = threading.Lock()
        self.rng = random.Random(args.seed)
        self.stats = {"txn": 0, "nack": 0, "corrupt": 0}

    def device(self, node):
        d = self.devices.get(node)
        if d is None:
            d = self.devices[node] = Device(node)
        return d

    def _corrupt(self, data: bytes) -> bytes:
        if not data:
            return data
        b = bytearray(data)
        b[self.rng.randrange(len(b))] ^= 1 << self.rng.randrange(8)
        self.stats["corrupt"] += 1
        return bytes(b)

    def transaction(self, kind, node, wdata, rlen):
        """Returns None for NACK, else the rlen read bytes."""
        a = self.args
        with self.lock:
            self.stats["txn"] += 1
            dev = self.device(node)
            dev.tick()
            r = self.rng.random()
            nack = r < a.nack_rate
            applied_then_nack = nack and r < a.nack_rate / 2   # half the NACKs hit after the write took effect
            if nack and not applied_then_nack:
                self.stats["nack"] += 1
                return None
            if wdata:
                if self.rng.random() < a.corrupt_rate:
                    wdata = self._corrupt(wdata)
                dev.write(wdata)
            if applied_then_nack:
                self.stats["nack"] += 1
                return None
            rdata = b""
            if kind == 0x52:
                rdata = dev.read(rlen)
                if self.rng.random() < a.corrupt_rate:
                    rdata = self._corrupt(rdata)
            if a.verbose:
                op = OP_NAMES.get(wdata[0], "?") if wdata else "(read)"
                print("n%d %s %s w=%s r=%s" % (node, chr(kind), op, wdata.hex(), rdata.hex()))
        delay = (len(wdata) + rlen + 2) * a.byte_us + a.txn_us
        if delay > 0:
            time.sleep(delay / 1e6)
        return rdata

    def ticker(self):
        """Background web-server main loop at ~1 kHz."""
        while True:
            with self.lock:
                for d in self.devices.values():
                    d.tick()
            time.sleep(0.001)

    def serve_client(self, conn, addr):
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def recv_exact(n):
            buf = bytearray()
            while len(buf) < n:
                c = conn.recv(n - len(buf))
                if not c:
                    raise EOFError
                buf += c
            return bytes(buf)

        try:
            while True:
                kind, node, wlen, rlen = struct.unpack("<BBHH", recv_exact(6))
                wdata = recv_exact(wlen) if wlen else b""
                if kind not in (0x57, 0x52):
                    print("bad kind 0x%02x from %s, dropping client" % (kind, addr))
                    return
                rd = self.transaction(kind, node, wdata, rlen if kind == 0x52 else 0)
                conn.sendall(b"\x01" if rd is None else b"\x00" + rd)
        except (EOFError, OSError):
            pass
        finally:
            conn.close()
            print("client %s:%d disconnected" % addr)

    def stats_loop(self):
        while True:
            time.sleep(5)
            with self.lock:
                conns = " ".join("n%d:%d" % (n, d.conns) for n, d in sorted(self.devices.items()))
                print("[sim] txn %d nack %d corrupt %d conns %s" % (
                    self.stats["txn"], self.stats["nack"], self.stats["corrupt"], conns), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9500)
    ap.add_argument("--nack-rate", type=float, default=0.0, help="probability a transaction is NACKed")
    ap.add_argument("--corrupt-rate", type=float, default=0.0, help="probability of a bit flip per phase")
    ap.add_argument("--byte-us", type=float, default=0.0, help="simulated bus time per byte (25 ~ 400 kHz)")
    ap.add_argument("--txn-us", type=float, default=0.0, help="simulated fixed overhead per transaction")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    sim = Simulator(args)
    threading.Thread(target=sim.ticker, daemon=True).start()
    threading.Thread(target=sim.stats_loop, daemon=True).start()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(4)
    print("sim_device listening on %s:%d (nack %.3f corrupt %.3f byte_us %g txn_us %g)" % (
        args.host, args.port, args.nack_rate, args.corrupt_rate, args.byte_us, args.txn_us), flush=True)
    try:
        while True:
            c, addr = srv.accept()
            print("client %s:%d connected" % addr, flush=True)
            threading.Thread(target=sim.serve_client, args=(c, addr), daemon=True).start()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
