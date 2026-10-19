"""Constants and helpers shared by the host app and the device simulator.

Implements the wire-level details of PROTOCOL.md v1 (A2B I2C web tunnel).
All multi-byte fields are little-endian.
"""

# --- ops -------------------------------------------------------------------
OP_SUMMARY = 0x01
OP_SOCK_STATUS = 0x02
OP_CONNECT = 0x10
OP_DATA = 0x11
OP_READ = 0x12
OP_CLOSE = 0x13
OP_RESET_ALL = 0x1F

OP_NAMES = {
    OP_SUMMARY: "SUMMARY", OP_SOCK_STATUS: "SOCK_STATUS", OP_CONNECT: "CONNECT",
    OP_DATA: "DATA", OP_READ: "READ", OP_CLOSE: "CLOSE", OP_RESET_ALL: "RESET_ALL",
}

RESET_ALL_KEY = 0xA5
SUMMARY_MAGIC = 0xA2
PROTO_VERSION = 0x01

SUMMARY_LEN = 6          # magic ver listen active attn crc
SOCK_STATUS_LEN = 14     # see PROTOCOL.md
READ_HDR_LEN = 5         # sn cid off_lo off_hi len   (then data[len], crc)
MAX_PAYLOAD = 240        # max DATA bytes / READ maxlen

# SOCK_STATUS.flags
F_CONN_ACTIVE = 0x01
F_LOCAL_FIN = 0x02
F_PEER_FIN = 0x04
F_LISTEN_READY = 0x08
F_VIRTUAL = 0x80

# W5500 Sn_SR values of interest
SR_CLOSED = 0x00
SR_INIT = 0x13
SR_LISTEN = 0x14
SR_ESTABLISHED = 0x17
SR_CLOSE_WAIT = 0x1C


# --- CRC-8, poly 0x07, init 0x00, no reflection, no xor-out ----------------
def _make_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
        table.append(c)
    return bytes(table)


_CRC_TABLE = _make_table()


def crc8(data) -> int:
    c = 0
    for b in data:
        c = _CRC_TABLE[c ^ b]
    return c


def with_crc(payload) -> bytes:
    """Append crc8 over the whole payload (the opcode byte included)."""
    payload = bytes(payload)
    return payload + bytes([crc8(payload)])


def u16(lo_hi: bytes, i: int) -> int:
    return lo_hi[i] | (lo_hi[i + 1] << 8)


assert crc8(b"123456789") == 0xF4  # standard CRC-8 (SMBus) check value
