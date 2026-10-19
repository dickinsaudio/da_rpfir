#!/usr/bin/env python3
"""End-to-end test: sim_device.py + a2b_web_tunnel.py + parallel HTTP fetches.

    python3 test_e2e.py                          # clean bus
    python3 test_e2e.py --nack-rate 0.05 --corrupt-rate 0.01 --byte-us 25

Checks every page byte for byte against sim_device.make_response() and prints
throughput. Exit status 0 = all good.
"""

import argparse
import concurrent.futures
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sim_device import make_response  # noqa: E402


def fetch(port, path, timeout=60):
    t0 = time.monotonic()
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.sendall(("GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n" % path).encode())
    buf = bytearray()
    while True:
        d = s.recv(65536)
        if not d:
            break
        buf += d
    s.close()
    return bytes(buf), time.monotonic() - t0


def wait_port(port, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("port %d did not open" % port)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nodes", default="0,1")
    ap.add_argument("--sim-port", type=int, default=9511)
    ap.add_argument("--base-port", type=int, default=18080)
    ap.add_argument("--parallel", type=int, default=6)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--chunk", type=int, default=64)
    ap.add_argument("--nack-rate", type=float, default=0.0)
    ap.add_argument("--corrupt-rate", type=float, default=0.0)
    ap.add_argument("--byte-us", type=float, default=0.0)
    ap.add_argument("--txn-us", type=float, default=0.0)
    a = ap.parse_args()
    nodes = [int(x) for x in a.nodes.split(",")]

    sim = subprocess.Popen([sys.executable, os.path.join(HERE, "sim_device.py"), "--port", str(a.sim_port),
                            "--nack-rate", str(a.nack_rate), "--corrupt-rate", str(a.corrupt_rate),
                            "--byte-us", str(a.byte_us), "--txn-us", str(a.txn_us), "--seed", "1"])
    time.sleep(0.5)
    tun = subprocess.Popen([sys.executable, os.path.join(HERE, "a2b_web_tunnel.py"),
                            "--transport", "sim:127.0.0.1:%d" % a.sim_port, "--nodes", a.nodes,
                            "--base-port", str(a.base_port), "--chunk", str(a.chunk), "--stats-s", "2"])
    ok = True
    try:
        for n in nodes:
            wait_port(a.base_port + n)
        # single fetch per node
        for n in nodes:
            body, dt = fetch(a.base_port + n, "/single")
            good = body == make_response(n, "/single")
            ok &= good
            print("node %d single: %d bytes in %.3f s (%.1f kB/s) %s" % (
                n, len(body), dt, len(body) / dt / 1000, "OK" if good else "MISMATCH"))
        # parallel fetches spread over the nodes
        for r in range(a.rounds):
            jobs = [(nodes[i % len(nodes)], "/p%d_%d" % (r, i)) for i in range(a.parallel)]
            t0 = time.monotonic()
            with concurrent.futures.ThreadPoolExecutor(a.parallel) as ex:
                res = list(ex.map(lambda j: fetch(a.base_port + j[0], j[1]), jobs))
            dt = time.monotonic() - t0
            total = sum(len(b) for b, _ in res)
            bad = [j for j, (b, _) in zip(jobs, res) if b != make_response(*j)]
            ok &= not bad
            print("round %d: %d parallel, %d bytes in %.3f s = %.1f kB/s aggregate, %s" % (
                r, a.parallel, total, dt, total / dt / 1000, "OK" if not bad else "MISMATCH %s" % bad))
    finally:
        time.sleep(2.2)          # let one more stats line through
        tun.terminate()
        sim.terminate()
        tun.wait()
        sim.wait()
    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
