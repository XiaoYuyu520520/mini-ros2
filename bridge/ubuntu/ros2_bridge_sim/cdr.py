"""Minimal ROS2 DDS-CDR (CDR1, little endian) writer/reader.

Only what the bridge needs: the 4 byte encapsulation header plus the encoding
rules used by eProsima Fast-CDR and by the ``rosbags`` library.

Contract
--------
* 4 byte encapsulation header ``00 01 00 00`` (CDR1, little endian).
* the body is at least 4 bytes long: a field-less message is padded out to
  that, the way a real ROS2 peer writes it - see :data:`MIN_CDR_BODY`.
* All alignment is computed relative to the first byte **after** that header
  (Fast-CDR ``Cdr::reset_alignment()``).
* Sizes/alignments: bool/byte/char/uint8/int8 = 1, int16/uint16 = 2,
  int32/uint32/float32 = 4, int64/uint64/float64 = 8.
* ``string``: 4 byte aligned u32 length **including** the trailing NUL, then
  the UTF-8 bytes, then ``\\0``.
* ``sequence<T>``: 4 byte aligned u32 count, then the elements (each aligned
  to its own requirement).
* fixed array ``T[N]``: no count, elements back to back, each aligned.
* nested messages: their fields in declaration order.

Note that CDR1 puts *no* padding in front of a nested struct: a struct member
is aligned exactly like its first field.  That is what ``rosbags`` implements
(``align(NAME) == align(fields[0])``) and it is what the byte-for-byte test
against ``rosbags`` pins down.
"""

from __future__ import annotations

import struct
from typing import Any, Iterable

#: Encapsulation header for CDR1 little endian (representation id 0x0001).
ENCAPSULATION_HEADER = b"\x00\x01\x00\x00"

#: Smallest body a real ROS2 peer puts on the wire.
#:
#: ``rmw_fastrtps_cpp`` never emits a bare encapsulation header: a body shorter
#: than four bytes is padded out to four. Measured on ROS2 Jazzy through
#: ``rclpy.serialization.serialize_message`` - ``std_msgs/msg/Empty`` (no
#: fields), ``std_msgs/msg/Bool`` (one CDR byte) and ``std_msgs/msg/Int16``
#: (two) all come back as ``00 01 00 00 00 00 00 00``, i.e. eight bytes;
#: ``std_msgs/msg/Int32`` (four bytes) stays at eight, and a ``string`` is not
#: padded at all (``String(data="ab")`` is eleven bytes of body plus header).
#: ``bridge/windows/include/ros2_bridge/transcode.hpp`` pads the same way for
#: the same reason, so a field-less request built here is byte for byte what
#: the bridge sends - and ``test_cdr_matches_rosbags.py`` cannot see this,
#: because ``rosbags`` emits the unpadded form while the DDS wire does not.
MIN_CDR_BODY = 4

#: ROS2 primitive type name -> (alignment, struct format character).
PRIMITIVES: dict[str, tuple[int, str]] = {
    "bool": (1, "?"),
    "byte": (1, "B"),
    "char": (1, "B"),
    "int8": (1, "b"),
    "uint8": (1, "B"),
    "int16": (2, "h"),
    "uint16": (2, "H"),
    "int32": (4, "i"),
    "uint32": (4, "I"),
    "int64": (8, "q"),
    "uint64": (8, "Q"),
    "float32": (4, "f"),
    "float64": (8, "d"),
}

#: A ``string`` is aligned like a u32 (its length prefix).
STRING_ALIGNMENT = 4

#: A ``sequence`` is aligned like a u32 (its count prefix).
SEQUENCE_ALIGNMENT = 4



class CdrError(ValueError):
    """Malformed, truncated or unsupported CDR payload."""


def align_up(offset: int, alignment: int) -> int:
    """Smallest multiple of ``alignment`` that is >= ``offset``."""
    return -(-offset // alignment) * alignment


class CdrWriter:
    """Appends CDR encoded fields to an internal buffer.

    The buffer holds the *body* only; :meth:`to_cdr` prepends the
    encapsulation header and alignment is always relative to the body start.
    """

    __slots__ = ("_buf",)

    def __init__(self) -> None:
        self._buf = bytearray()

    def __len__(self) -> int:
        return len(self._buf)

    @property
    def offset(self) -> int:
        return len(self._buf)

    def align(self, alignment: int) -> None:
        """Pad with zero bytes until the body offset is ``alignment`` aligned."""
        pad = -len(self._buf) % alignment
        if pad:
            self._buf += bytes(pad)

    def prim(self, type_name: str, value: Any) -> None:
        """Write one primitive scalar."""
        alignment, code = PRIMITIVES[type_name]
        self.align(alignment)
        if type_name == "bool":
            self._buf.append(1 if value else 0)
        else:
            self._buf += struct.pack("<" + code, _coerce(code, value))

    def prim_array(self, type_name: str, values: Iterable[Any]) -> None:
        """Write a run of primitive scalars, e.g. a sequence body or an array."""
        items = list(values)
        alignment, code = PRIMITIVES[type_name]
        self.align(alignment)
        if not items:
            return
        if type_name == "bool":
            self._buf += bytes(1 if v else 0 for v in items)
        else:
            self._buf += struct.pack("<%d%s" % (len(items), code), *(_coerce(code, v) for v in items))

    def string(self, value: str) -> None:
        """Write a length prefixed, NUL terminated UTF-8 string."""
        raw = value.encode("utf-8")
        self.align(STRING_ALIGNMENT)
        self._buf += struct.pack("<I", len(raw) + 1)
        self._buf += raw
        self._buf.append(0)

    def sequence_length(self, count: int) -> None:
        """Write the 4 byte count that starts a ``sequence<T>``."""
        self.align(SEQUENCE_ALIGNMENT)
        self._buf += struct.pack("<I", count)

    def raw(self, data: bytes) -> None:
        """Append already encoded bytes (no alignment, no length prefix)."""
        self._buf += data

    def body(self) -> bytes:
        """The encoded body, without the encapsulation header."""
        return bytes(self._buf)

    def to_cdr(self) -> bytes:
        """The full payload, including the 4 byte encapsulation header.

        A body shorter than :data:`MIN_CDR_BODY` is padded with zeros, which is
        what a real ROS2 peer does and therefore what this one has to do too:
        the unpadded form is accepted on read, but it is not what DDS puts on
        the wire, and a caller that compares bytes (or a peer that reads the
        padding back) would disagree with every real publisher.
        """
        body = bytes(self._buf)
        if len(body) < MIN_CDR_BODY:
            body += bytes(MIN_CDR_BODY - len(body))
        return ENCAPSULATION_HEADER + body


class CdrReader:
    """Reads CDR encoded fields from a payload or a bare body."""

    __slots__ = ("_buf", "_pos")

    def __init__(self, data: bytes | bytearray | memoryview, *, encapsulation: bool = True) -> None:
        data = bytes(data)
        if encapsulation:
            if len(data) < len(ENCAPSULATION_HEADER):
                raise CdrError(
                    "CDR payload is shorter than the 4 byte encapsulation header (%d byte(s))" % len(data)
                )
            if data[:2] == b"\x00\x00":
                raise CdrError("big endian CDR is not supported")
            if data[:2] != b"\x00\x01":
                raise CdrError("bad CDR encapsulation header: %s" % data[:4].hex())
            data = data[4:]
        self._buf = data
        self._pos = 0

    def __len__(self) -> int:
        return len(self._buf)

    @property
    def offset(self) -> int:
        return self._pos

    @property
    def remaining(self) -> int:
        return len(self._buf) - self._pos

    @property
    def at_end(self) -> bool:
        return self._pos == len(self._buf)

    def align(self, alignment: int) -> None:
        self._pos = align_up(self._pos, alignment)

    def _need(self, size: int) -> None:
        if self._pos + size > len(self._buf):
            raise CdrError(
                "truncated CDR payload: need %d byte(s) at offset %d, %d left"
                % (size, self._pos, self.remaining)
            )

    def prim(self, type_name: str) -> Any:
        """Read one primitive scalar."""
        alignment, code = PRIMITIVES[type_name]
        self.align(alignment)
        size = struct.calcsize(code)
        self._need(size)
        value = struct.unpack_from("<" + code, self._buf, self._pos)[0]
        self._pos += size
        return bool(value) if type_name == "bool" else value

    def prim_array(self, type_name: str, count: int) -> list[Any]:
        """Read ``count`` primitive scalars into a list."""
        alignment, code = PRIMITIVES[type_name]
        self.align(alignment)
        if count < 0:
            raise CdrError("negative element count: %d" % count)
        if count == 0:
            return []
        if type_name == "bool":
            self._need(count)
            values = [b != 0 for b in self._buf[self._pos : self._pos + count]]
            self._pos += count
            return values
        size = struct.calcsize(code)
        self._need(count * size)
        values = list(struct.unpack_from("<%d%s" % (count, code), self._buf, self._pos))
        self._pos += count * size
        return values

    def string(self) -> str:
        """Read a length prefixed, NUL terminated UTF-8 string."""
        self.align(STRING_ALIGNMENT)
        length = self.prim("uint32")
        if length == 0:
            return ""
        self._need(length)
        raw = self._buf[self._pos : self._pos + length]
        self._pos += length
        if raw[-1] != 0:
            raise CdrError("CDR string is not NUL terminated")
        try:
            return raw[:-1].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise CdrError("CDR string is not valid UTF-8: %s" % exc) from exc

    def sequence_length(self, *, limit: int = 1 << 22) -> int:
        """Read the 4 byte count that starts a ``sequence<T>``."""
        self.align(SEQUENCE_ALIGNMENT)
        count = self.prim("uint32")
        if count > limit:
            raise CdrError("implausible sequence count: %d" % count)
        return count

    def raw(self, size: int) -> bytes:
        """Read ``size`` bytes verbatim."""
        self._need(size)
        out = self._buf[self._pos : self._pos + size]
        self._pos += size
        return out


def _coerce(code: str, value: Any) -> Any:
    """Accept numpy scalars / bools for integer and float fields."""
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, float) and code in "bBhHiIqQ":
        value = int(value)
    return value