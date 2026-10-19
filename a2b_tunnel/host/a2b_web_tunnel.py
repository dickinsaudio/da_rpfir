#!/usr/bin/env python3
"""PC side of the A2B I2C web tunnel (PROTOCOL.md v1).

For every A2B node in --nodes a local TCP listener is opened on
--base-port + node. Each browser connection is paired with a device virtual
socket that is in LISTEN and bytes are pumped both ways over the tunnel.

Design: one thread. A selector multiplexes the (non-blocking) browser sockets;
between select() calls a polling scheduler runs tunnel transactions on the
blocking transport. Because only this loop touches the transport, every I2C
transaction is naturally serialised (the Bus lock makes that explicit should
anyone add a second thread).

Polling: a node with "hot" connections (recent traffic, pending upload,
closing ...) is serviced every loop iteration (as fast as the bus allows);
otherwise it gets one SUMMARY every --idle-poll-ms. Per cycle only sockets
flagged by SUMMARY (attn) or with pending work get further transactions, and
while a socket streams full READ chunks the SUMMARY is skipped altogether.
"""

import argparse
import collections
import selectors
import socket
import struct
import sys
import threading
import time

from tunnel_proto import (
    OP_SUMMARY, OP_SOCK_STATUS, OP_CONNECT, OP_DATA, OP_READ, OP_CLOSE, OP_RESET_ALL,
    RESET_ALL_KEY, SUMMARY_MAGIC, PROTO_VERSION, SUMMARY_LEN, SOCK_STATUS_LEN,
    READ_HDR_LEN, MAX_PAYLOAD, F_CONN_ACTIVE, F_LOCAL_FIN, F_PEER_FIN, F_LISTEN_READY,
    crc8, with_crc, u16,
)
from transports import I2CError, I2CNack, open_transport

UP_LIMIT = 16384        # max browser->device bytes buffered on the PC per connection
DOWN_LIMIT = 32768      # stop READing when this much is waiting to go to the browser
HOT_TIME = 0.25         # s a connection stays "hot" (fast polled) after activity
NODE_DOWN_BACKOFF = 0.25
NODE_DEAD_TIME = 5.0    # abort a node's connections after this long without a good transaction
CONNECT_TRIES = 5
CLOSE_TRIES = 10
PROTO_ERR_LIMIT = 20    # consecutive inconsistent READ responses before aborting a connection


class ProtocolError(Exception):
    """Response failed CRC / sanity checks (treated like a bus error: retried)."""


class TxnFailed(Exception):
    """A transaction failed after all retries."""


# ---------------------------------------------------------------------------
# Protocol layer: one method per op, CRC + validation + retries.
# ---------------------------------------------------------------------------
Summary = collections.namedtuple("Summary", "listen active attn")


class SockStatus(collections.namedtuple(
        "SockStatus", "sn sr flags cid port rx_next rx_free tx_base tx_avail")):
    active = property(lambda s: bool(s.flags & F_CONN_ACTIVE))
    local_fin = property(lambda s: bool(s.flags & F_LOCAL_FIN))
    peer_fin = property(lambda s: bool(s.flags & F_PEER_FIN))
    listen_ready = property(lambda s: bool(s.flags & F_LISTEN_READY))


class NodeStats:
    def __init__(self):
        self.txn = self.retries = self.nacks = self.crc_errs = self.io_errs = self.failed = 0
        self.up = self.down = 0
        self.conns = 0
        self.snap = (0, 0, 0, 0, time.monotonic())   # txn, up, down, retries, t at last print


class Bus:
    def __init__(self, transport, retries, backoff, verbose):
        self.t = transport
        self.retries = retries
        self.backoff = backoff
        self.verbose = verbose
        self.lock = threading.Lock()
        self.stats = collections.defaultdict(NodeStats)

    def _run(self, node, desc, fn):
        """Run fn() (one I2C transaction + parsing), retrying per PROTOCOL.md."""
        st = self.stats[node]
        last = None
        for attempt in range(self.retries + 1):
            if attempt:
                st.retries += 1
                time.sleep(self.backoff)
            st.txn += 1
            try:
                with self.lock:
                    r = fn()
                if self.verbose:
                    print("  n%d %-34s -> %s%s" % (node, desc, _fmt(r),
                                                   "  (try %d)" % (attempt + 1) if attempt else ""))
                return r
            except I2CNack as e:
                st.nacks += 1
                last = e
            except ProtocolError as e:
                st.crc_errs += 1
                last = e
            except I2CError as e:
                st.io_errs += 1
                last = e
            if self.verbose:
                print("  n%d %-34s !! %s" % (node, desc, last))
        st.failed += 1
        raise TxnFailed("node %d %s: %s" % (node, desc, last))

    def _write(self, node, desc, payload):
        frame = with_crc(payload)
        self._run(node, desc, lambda: self.t.write(node, frame))

    def _read(self, node, desc, payload, n, parse):
        frame = with_crc(payload)
        return self._run(node, desc, lambda: parse(self.t.write_read(node, frame, n)))

    # ---- ops ----------------------------------------------------------------
    def summary(self, node) -> Summary:
        def parse(r):
            if crc8(r[:5]) != r[5] or r[0] != SUMMARY_MAGIC:
                raise ProtocolError("SUMMARY bad crc/magic %s" % r.hex())
            if r[1] != PROTO_VERSION:
                raise ProtocolError("SUMMARY protocol version %d (want %d)" % (r[1], PROTO_VERSION))
            return Summary(r[2], r[3], r[4])
        return self._read(node, "SUMMARY", [OP_SUMMARY], SUMMARY_LEN, parse)

    def sock_status(self, node, sn) -> SockStatus:
        def parse(r):
            if crc8(r[:13]) != r[13] or r[0] != sn:
                raise ProtocolError("SOCK_STATUS bad crc/sn %s" % r.hex())
            return SockStatus(*struct.unpack("<BBBBHHHHB", r[:13]))
        return self._read(node, "SOCK_STATUS s%d" % sn, [OP_SOCK_STATUS, sn], SOCK_STATUS_LEN, parse)

    def connect(self, node, sn, cid, ip, port):
        self._write(node, "CONNECT s%d cid%d" % (sn, cid),
                    bytes([OP_CONNECT, sn, cid]) + bytes(ip) + struct.pack("<H", port))

    def data(self, node, sn, cid, off, payload):
        self._write(node, "DATA s%d cid%d off%d len%d" % (sn, cid, off, len(payload)),
                    bytes([OP_DATA, sn, cid]) + struct.pack("<H", off) + bytes(payload))

    def read(self, node, sn, cid, ack, maxlen):
        """Returns (cid, off, data)."""
        def parse(r):
            ln = r[4]
            if r[0] != sn or ln > maxlen or crc8(r[:READ_HDR_LEN + ln]) != r[READ_HDR_LEN + ln]:
                raise ProtocolError("READ bad crc/sn/len %s" % r[:READ_HDR_LEN + 1].hex())
            return r[1], u16(r, 2), bytes(r[READ_HDR_LEN:READ_HDR_LEN + ln])
        # Fixed-length I2C read: header + maxlen + crc; the crc sits right after
        # the `len` valid data bytes, anything after it is padding.
        return self._read(node, "READ s%d cid%d ack%d max%d" % (sn, cid, ack, maxlen),
                          bytes([OP_READ, sn, cid]) + struct.pack("<HB", ack, maxlen),
                          READ_HDR_LEN + maxlen + 1, parse)

    def close(self, node, sn, cid):
        self._write(node, "CLOSE s%d cid%d" % (sn, cid), [OP_CLOSE, sn, cid])

    def reset_all(self, node):
        self._write(node, "RESET_ALL", [OP_RESET_ALL, RESET_ALL_KEY])


def _fmt(r):
    if r is None:
        return "ok"
    if isinstance(r, Summary):
        return "listen %02x active %02x attn %02x" % r
    if isinstance(r, SockStatus):
        return "sr %02x fl %02x cid %d rx_next %d rx_free %d tx_base %d tx_avail %d" % (
            r.sr, r.flags, r.cid, r.rx_next, r.rx_free, r.tx_base, r.tx_avail)
    if isinstance(r, tuple) and len(r) == 3:
        return "cid %d off %d len %d" % (r[0], r[1], len(r[2]))
    return repr(r)


# ---------------------------------------------------------------------------
# Browser connections and nodes
# ---------------------------------------------------------------------------
QUEUED, CONNECTING, OPEN, FIN_DRAIN, CLOSING, DONE = "queued connecting open fin_drain closing done".split()


class Conn:
    def __init__(self, node, sock, addr):
        self.node = node
        self.sock = sock
        self.addr = addr
        self.state = QUEUED
        self.sn = None
        self.cid = 0
        self.up = bytearray()     # browser -> device, not yet taken by the device
        self.up_off = 0           # stream offset of up[0] (unbounded int, sent mod 2^16)
        self.down = bytearray()   # device -> browser, not yet sent to the browser
        self.ack = 0              # 16-bit: next tx stream offset wanted from the device
        self.eof = False          # browser sent FIN
        self.dead = False         # browser socket errored / was closed by us
        self.streaming = False    # last READ returned a full chunk: read again, skip SUMMARY
        self.tries = 0
        self.proto_errs = 0
        self.hot_until = 0.0
        self.events = 0           # selector mask currently registered

    def __repr__(self):
        return "n%d s%s cid%d %s:%d" % (self.node.num, self.sn, self.cid, self.addr[0], self.addr[1])


class Node:
    def __init__(self, num, listener):
        self.num = num
        self.listener = listener
        self.pending = collections.deque()   # QUEUED Conns
        self.conns = {}                      # sn -> Conn
        self.next_cid = 1
        self.next_poll = 0.0
        self.online = False
        self.down_until = 0.0
        self.last_ok = time.monotonic()
        self.warned = False

    def alloc_cid(self):
        used = {c.cid for c in self.conns.values()}
        while True:
            cid = self.next_cid
            self.next_cid = cid % 255 + 1
            if cid not in used:
                return cid

    def hot(self, now):
        return any(c.hot_until > now or c.up or c.state in (CONNECTING, FIN_DRAIN, CLOSING) for c in self.conns.values())


class Tunnel:
    def __init__(self, args, bus):
        self.a = args
        self.bus = bus
        self.sel = selectors.DefaultSelector()
        self.nodes = []
        self.chunk = max(1, min(args.chunk, MAX_PAYLOAD))
        self.idle = args.idle_poll_ms / 1000.0
        for n in args.nodes:
            ls = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            if sys.platform != "win32":
                ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            ls.bind((args.bind, args.base_port + n))
            ls.listen(16)
            ls.setblocking(False)
            node = Node(n, ls)
            self.sel.register(ls, selectors.EVENT_READ, node)
            self.nodes.append(node)
            print("node %d: http://%s:%d/" % (n, args.bind, args.base_port + n))

    def log(self, msg):
        if self.a.verbose:
            print(msg)

    # ---- browser side (non-blocking) ---------------------------------------
    def on_accept(self, node):
        try:
            s, addr = node.listener.accept()
        except OSError:
            return
        s.setblocking(False)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        c = Conn(node, s, addr[:2])
        node.pending.append(c)
        node.next_poll = 0.0           # look for a listening socket right away
        self.update_events(c)
        self.log("accept %s:%d -> node %d (queued %d)" % (addr[0], addr[1], node.num, len(node.pending)))

    def on_readable(self, c):
        try:
            data = c.sock.recv(min(4096, UP_LIMIT - len(c.up)))
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            self.browser_gone(c)
            return
        if data:
            c.up += data
            c.hot_until = time.monotonic() + HOT_TIME
        else:
            c.eof = True
            c.hot_until = time.monotonic() + HOT_TIME

    def on_writable(self, c):
        try:
            n = c.sock.send(c.down)
        except (BlockingIOError, InterruptedError):
            return
        except OSError:
            self.browser_gone(c)
            return
        del c.down[:n]

    def browser_gone(self, c):
        c.dead = True
        c.eof = True
        c.up.clear()
        c.down.clear()
        self.close_browser(c)

    def close_browser(self, c):
        if c.sock is None:
            return
        if c.events:
            self.sel.unregister(c.sock)
            c.events = 0
        try:
            c.sock.close()
        except OSError:
            pass
        c.sock = None

    def update_events(self, c):
        if c.sock is None:
            return
        ev = 0
        if not c.eof and len(c.up) < UP_LIMIT and c.state in (QUEUED, CONNECTING, OPEN):
            ev |= selectors.EVENT_READ
        if c.down:
            ev |= selectors.EVENT_WRITE
        if ev == c.events:
            return
        if c.events == 0:
            self.sel.register(c.sock, ev, c)
        elif ev == 0:
            self.sel.unregister(c.sock)
        else:
            self.sel.modify(c.sock, ev, c)
        c.events = ev

    # ---- tunnel side ---------------------------------------------------------
    def bring_up(self, node):
        """RESET_ALL + SUMMARY check. Raises TxnFailed if the node does not answer."""
        b = self.bus
        for _ in range(3):
            b.reset_all(node.num)
            s = b.summary(node.num)
            if s.active == 0:
                break
        print("node %d: online (listen %02x active %02x)" % (node.num, s.listen, s.active))
        node.online = True
        node.warned = False

    def service(self, node, now):
        """One polling cycle for a node."""
        b, n = self.bus, node.num
        if not node.online:
            self.bring_up(node)

        # Drop queued browsers that went away before getting a socket.
        for c in [c for c in node.pending if c.eof and not c.up or c.dead]:
            node.pending.remove(c)
            self.close_browser(c)

        streaming_only = node.conns and all(
            c.state == OPEN and c.streaming and not c.up and not c.eof for c in node.conns.values())
        s = None
        if not (streaming_only and not node.pending):
            s = b.summary(n)

        for sn, c in list(node.conns.items()):
            if c.state == CONNECTING:
                self.do_connect(c, s)
            elif c.state == OPEN:
                self.do_open(c, s, now)
            if c.state == FIN_DRAIN and (not c.down or c.dead):
                self.close_browser(c)       # browser sees EOF only after all data
                c.state = CLOSING
            if c.state == CLOSING:
                self.do_close(c)
            if c.state == DONE:
                self.close_browser(c)
                del node.conns[sn]
            self.update_events(c)

        # Pair queued browsers with free listening sockets.
        if s is not None and node.pending:
            free = s.listen & ~sum(1 << sn for sn in node.conns)
            sn = 0
            while free and node.pending:
                if free & 1:
                    c = node.pending.popleft()
                    c.sn, c.cid, c.state, c.tries = sn, node.alloc_cid(), CONNECTING, 0
                    node.conns[sn] = c
                    self.do_connect(c, s)
                    if c.state == DONE:
                        del node.conns[sn]
                    self.update_events(c)
                free >>= 1
                sn += 1

    def do_connect(self, c, s):
        b, n = self.bus, c.node.num
        if c.dead or (c.eof and not c.up):
            c.state = DONE                 # browser gave up meanwhile; nothing sent yet
            return
        ip = bytes(int(x) for x in c.addr[0].split(".")) if c.addr[0].count(".") == 3 else bytes(4)
        b.connect(n, c.sn, c.cid, ip, c.addr[1])
        st = b.sock_status(n, c.sn)
        if st.active and st.cid == c.cid:
            c.state = OPEN
            c.hot_until = time.monotonic() + HOT_TIME
            c.node.node_stats.conns += 1
            self.log("open %r" % c)
            return
        c.tries += 1
        if not st.listen_ready or c.tries >= CONNECT_TRIES:
            # Lost the race (or device busy): back to the head of the queue.
            self.log("connect %r failed (sr %02x fl %02x cid %d), requeue" % (c, st.sr, st.flags, st.cid))
            del c.node.conns[c.sn]
            c.sn, c.cid, c.state = None, 0, QUEUED
            c.node.pending.appendleft(c)

    def lost(self, c, why):
        print("node %d: connection %r lost (%s)" % (c.node.num, c, why))
        self.close_browser(c)
        c.state = DONE

    def do_open(self, c, s, now):
        b, n, sn = self.bus, c.node.num, c.sn
        st = None
        attn = False
        if s is not None:
            if not s.active >> sn & 1:
                return self.lost(c, "device released socket")
            attn = bool(s.attn >> sn & 1)

        # -- upload: send from the device's rx_next, as much as fits ----------
        if c.up:
            st = b.sock_status(n, sn)
            if not st.active or st.cid != c.cid:
                return self.lost(c, "status cid %d" % st.cid)
            if not self.sync_up(c, st):
                return
            if c.up and not st.local_fin and not st.peer_fin and st.rx_free:
                ln = min(len(c.up), st.rx_free, self.chunk)
                b.data(n, sn, c.cid, c.up_off & 0xFFFF, c.up[:ln])
                c.hot_until = now + HOT_TIME
            if st.local_fin:
                c.up.clear()               # server is gone; nobody will take it

        # -- download -----------------------------------------------------------
        want_read = attn or c.streaming or (st is not None and st.tx_avail)
        if want_read and len(c.down) < DOWN_LIMIT and not c.dead:
            cid, off, data = b.read(n, sn, c.cid, c.ack, self.chunk)
            if cid != c.cid:
                return self.lost(c, "READ cid %d" % cid)
            if off != c.ack:
                # Should not happen: tx_base is exactly what we acked.
                c.proto_errs += 1
                c.node.node_stats.crc_errs += 1
                if c.proto_errs > PROTO_ERR_LIMIT:
                    return self.lost(c, "READ off %d != ack %d" % (off, c.ack))
                return
            c.proto_errs = 0
            if data:
                c.down += data
                c.ack = (off + len(data)) & 0xFFFF
                c.node.node_stats.down += len(data)
                c.hot_until = now + HOT_TIME
            c.streaming = len(data) == self.chunk
            st = None                      # the ack moved tx_base: any status is stale
            if not data:
                # Everything acked and nothing left, yet we were flagged: this is
                # usually local_fin. One status read tells.
                st = b.sock_status(n, sn)
                if not st.active or st.cid != c.cid:
                    return self.lost(c, "status cid %d" % st.cid)

        # -- device finished: drain, then close browser, then CLOSE -------------
        if st is not None and st.local_fin and st.tx_avail == 0 and st.tx_base == c.ack and not c.up:
            self.log("local_fin %r" % c)
            c.state = FIN_DRAIN
            return

        # -- browser finished (or vanished): CLOSE once upload is delivered -----
        if c.dead or (c.eof and not c.up):
            self.log("browser closed %r" % c)
            c.state = CLOSING

    def sync_up(self, c, st):
        """Drop upload bytes the device has taken (rx_next). False if inconsistent."""
        d = (st.rx_next - c.up_off) & 0xFFFF
        if d > len(c.up):
            self.lost(c, "rx_next %d beyond sent data" % st.rx_next)
            return False
        if d:
            del c.up[:d]
            c.up_off += d
            c.node.node_stats.up += d
        return True

    def do_close(self, c):
        b, n = self.bus, c.node.num
        b.close(n, c.sn, c.cid)
        st = b.sock_status(n, c.sn)
        if st.active and st.cid == c.cid and not st.peer_fin:
            c.tries += 1                   # CLOSE was lost (e.g. bad crc): resend next cycle
            if c.tries < CLOSE_TRIES:
                return
            print("node %d: CLOSE %r not confirmed, giving up" % (n, c))
        self.log("closed %r" % c)
        c.state = DONE

    def node_failed(self, node, e, now):
        node.down_until = now + NODE_DOWN_BACKOFF
        if not node.warned:
            print("node %d: %s" % (node.num, e))
            node.warned = True
        if now - node.last_ok > NODE_DEAD_TIME and (node.conns or node.online):
            print("node %d: not responding, dropping %d connection(s)" % (node.num, len(node.conns)))
            for c in node.conns.values():
                self.close_browser(c)
            node.conns.clear()
            node.online = False

    # ---- main loop ------------------------------------------------------------
    def run(self):
        stats_every = self.a.stats_s
        next_stats = time.monotonic() + stats_every
        for node in self.nodes:
            node.node_stats = self.bus.stats[node.num]
            try:
                self.bring_up(node)
                node.last_ok = time.monotonic()
            except TxnFailed as e:
                print("node %d: not responding at start-up (%s); will keep trying" % (node.num, e))
                node.warned = True
                node.down_until = time.monotonic() + NODE_DOWN_BACKOFF
        while True:
            now = time.monotonic()
            hot = [nd for nd in self.nodes if nd.online and nd.hot(now) and nd.down_until <= now]
            if hot:
                timeout = 0
            else:
                wake = min(max(nd.next_poll, nd.down_until) for nd in self.nodes)
                timeout = min(max(0.0, wake - now), 0.05)   # <= 50 ms so Ctrl-C works on Windows
            for key, ev in self.sel.select(timeout):
                obj = key.data
                if isinstance(obj, Node):
                    self.on_accept(obj)
                    continue
                if ev & selectors.EVENT_READ and obj.sock is not None:
                    self.on_readable(obj)
                if ev & selectors.EVENT_WRITE and obj.sock is not None:
                    self.on_writable(obj)
                self.update_events(obj)

            now = time.monotonic()
            for node in self.nodes:
                if node.down_until > now:
                    continue
                if not (node.hot(now) and node.online) and now < node.next_poll:
                    continue
                node.next_poll = now + self.idle
                try:
                    self.service(node, now)
                    node.last_ok = time.monotonic()
                    node.warned = False
                except TxnFailed as e:
                    self.node_failed(node, e, now)

            if now >= next_stats:
                self.print_stats(now)
                next_stats = now + stats_every

    def print_stats(self, now):
        parts = []
        for node in self.nodes:
            st = node.node_stats
            t0, u0, d0, r0, ts = st.snap
            dt = max(now - ts, 1e-6)
            parts.append("n%d%s conn %d (act %d q %d) up %s down %s (%.1f kB/s) %.0f txn/s retry %d "
                         "[nack %d crc %d io %d fail %d]" % (
                             node.num, "" if node.online else "(off)", st.conns, len(node.conns),
                             len(node.pending), _kb(st.up), _kb(st.down), (st.down - d0) / dt / 1000,
                             (st.txn - t0) / dt, st.retries, st.nacks, st.crc_errs, st.io_errs, st.failed))
            st.snap = (st.txn, st.up, st.down, st.retries, now)
        print("[stats] " + " | ".join(parts), flush=True)

    def shutdown(self):
        """Best effort: release device sockets so the web server is not left hanging."""
        for node in self.nodes:
            for c in node.conns.values():
                if c.state in (OPEN, FIN_DRAIN, CLOSING):
                    try:
                        self.bus.close(node.num, c.sn, c.cid)
                    except TxnFailed:
                        pass
                self.close_browser(c)


def _kb(n):
    return "%dB" % n if n < 10000 else "%.1fkB" % (n / 1000) if n < 10_000_000 else "%.1fMB" % (n / 1e6)


def parse_nodes(s):
    nodes = sorted({int(x) for x in s.replace(" ", "").split(",") if x != ""})
    if not nodes or any(not 0 <= x <= 15 for x in nodes):
        raise argparse.ArgumentTypeError("nodes must be a comma list of 0..15")
    return nodes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--transport", default="sim:127.0.0.1:9500",
                    help="sim:HOST:PORT | serial:COM5 | serial:/dev/ttyACM0 | usbi (default %(default)s)")
    ap.add_argument("--nodes", type=parse_nodes, default=[0], help="comma list of A2B sub-nodes, e.g. 0,1,2")
    ap.add_argument("--base-port", type=int, default=8080, help="TCP port for node 0 (node N -> base+N)")
    ap.add_argument("--bind", default="127.0.0.1", help="listen address (0.0.0.0 to expose on the LAN)")
    ap.add_argument("--chunk", type=int, default=64, help="max DATA/READ payload per transaction (<= 240)")
    ap.add_argument("--idle-poll-ms", type=float, default=50.0, help="SUMMARY interval for idle nodes")
    ap.add_argument("--retries", type=int, default=8, help="retries per transaction")
    ap.add_argument("--backoff-ms", type=float, default=1.0, help="delay before each retry")
    ap.add_argument("--stats-s", type=float, default=5.0, help="stats line interval")
    ap.add_argument("--a2b-base", type=lambda x: int(x, 0), default=0x68, help="AD242x base address")
    ap.add_argument("--tunnel-addr", type=lambda x: int(x, 0), default=0x2A, help="RP2350 slave address")
    ap.add_argument("--no-400k", action="store_true", help="do not switch sub-node I2C to 400 kHz")
    ap.add_argument("--baud", type=int, default=115200, help="serial bridge baud rate")
    ap.add_argument("-v", "--verbose", action="store_true", help="log every transaction")
    args = ap.parse_args()

    try:
        transport = open_transport(args.transport, args.a2b_base, args.tunnel_addr, not args.no_400k, args.baud)
    except (RuntimeError, NotImplementedError, ValueError) as e:
        sys.exit("transport: %s" % e)
    print("a2b_web_tunnel: transport %s, nodes %s, chunk %d" % (
        transport.describe(), ",".join(map(str, args.nodes)), min(args.chunk, MAX_PAYLOAD)))
    bus = Bus(transport, args.retries, args.backoff_ms / 1000.0, args.verbose)
    tunnel = Tunnel(args, bus)
    try:
        tunnel.run()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        tunnel.shutdown()
        transport.close()


if __name__ == "__main__":
    main()
