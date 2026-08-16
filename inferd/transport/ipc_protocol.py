"""Wire protocol for the IPC fabric — the Python half of shared/ipc/IpcProtocol.h.

This is a mirror, not a source of truth. The C++ header owns the numbering; if the
two disagree, frames are silently misrouted rather than rejected, so any change
there has to be made here in the same commit. `assert_header_size()` catches the
one class of drift a mirror can catch on its own.
"""

import struct
from enum import IntEnum

# Must equal IPC_PROTOCOL_VERSION in shared/ipc/IpcProtocol.h. ipcd rejects any other
# value outright — the whole frame is dropped as an invalid header, with no hint that
# the version is what it disagreed with.
IPC_VERSION = 2
IPC_MAX_FRAME_SIZE = 1024 * 1024

# version, src, dst, flags, cmd, reserved, seqNo, payloadLen — network byte order,
# packed (no padding), matching #pragma pack(push, 1) on the C++ side.
_HEADER_FMT = ">BBBBHHII"
HEADER_SIZE = struct.calcsize(_HEADER_FMT)


class Daemon(IntEnum):
    UNKNOWN = 0
    IPCD = 1
    ENGINED = 2
    AUTHD = 3
    PROBED = 4
    COLLECTORD = 5
    TOPOLOGYD = 6
    MGMTD = 7
    APID = 8
    INFERD = 9
    RAGD = 10
    BROADCAST = 255


class Cmd(IntEnum):
    UNKNOWN = 0
    CLIENT_HELLO = 1
    SERVER_HELLO = 2
    CHAT_REQUEST = 141
    CHAT_RESPONSE = 142
    RETRIEVE_REQUEST = 143
    RETRIEVE_RESPONSE = 144


class Flag(IntEnum):
    NONE = 0x00
    REQUEST = 0x01
    RESPONSE = 0x02
    ERROR = 0x04
    BROADCAST = 0x08


class Header:
    __slots__ = ("version", "src", "dst", "flags", "cmd", "reserved", "seq_no", "payload_len")

    def __init__(self, src, dst, cmd, seq_no=0, flags=Flag.REQUEST, payload_len=0):
        self.version = IPC_VERSION
        self.src = int(src)
        self.dst = int(dst)
        self.flags = int(flags)
        self.cmd = int(cmd)
        self.reserved = 0
        self.seq_no = int(seq_no)
        self.payload_len = int(payload_len)

    def pack(self):
        return struct.pack(_HEADER_FMT, self.version, self.src, self.dst, self.flags,
                           self.cmd, self.reserved, self.seq_no, self.payload_len)

    @classmethod
    def unpack(cls, raw):
        version, src, dst, flags, cmd, reserved, seq_no, payload_len = struct.unpack(_HEADER_FMT, raw)
        h = cls(src, dst, cmd, seq_no, flags, payload_len)
        h.version = version
        h.reserved = reserved
        return h

    def __repr__(self):
        return (f"Header(src={_name(Daemon, self.src)}, dst={_name(Daemon, self.dst)}, "
                f"cmd={_name(Cmd, self.cmd)}, seq={self.seq_no}, len={self.payload_len})")


class Message:
    __slots__ = ("header", "payload")

    def __init__(self, header, payload=b""):
        self.header = header
        self.payload = payload
        header.payload_len = len(payload)

    def to_bytes(self):
        self.header.payload_len = len(self.payload)
        return self.header.pack() + self.payload


def _name(enum_cls, value):
    """Unknown values are printed numerically rather than raising: a frame carrying
    a command this build has never heard of is a thing to log, not to crash on."""
    try:
        return enum_cls(value).name
    except ValueError:
        return str(value)


def assert_header_size():
    if HEADER_SIZE != 16:
        raise RuntimeError(f"IPC header must be 16 bytes, got {HEADER_SIZE}")
