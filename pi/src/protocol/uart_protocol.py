from __future__ import annotations

from dataclasses import dataclass

FRAME_SYNC = b"\xaa\x55"
DISPENSE_CMD = 0x10
DISPENSE_RESULT = 0x11
STATUS_REQUEST = 0x01
STATUS_RESPONSE = 0x02
MAX_PAYLOAD_LENGTH = 64


@dataclass(frozen=True)
class Frame:
    msg_type: int
    payload: bytes


def crc8_maxim(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0x8C) if (crc & 0x01) else (crc >> 1)
    return crc & 0xFF


def encode_frame(msg_type: int, payload: bytes = b"") -> bytes:
    length = len(payload)
    if length > MAX_PAYLOAD_LENGTH:
        raise ValueError("payload is too large")
    header = bytes([msg_type & 0xFF, length & 0xFF]) + payload
    return FRAME_SYNC + header + bytes([crc8_maxim(header)])


def decode_frame(raw: bytes) -> Frame:
    if len(raw) < 5 or raw[:2] != FRAME_SYNC:
        raise ValueError("invalid frame sync")
    msg_type = raw[2]
    length = raw[3]
    payload = raw[4:-1]
    if len(payload) != length:
        raise ValueError("invalid frame length")
    expected_crc = raw[-1]
    actual_crc = crc8_maxim(raw[2:-1])
    if expected_crc != actual_crc:
        raise ValueError("invalid frame crc")
    return Frame(msg_type=msg_type, payload=payload)


class FrameParser:
    """Incrementally extract validated frames from arbitrary serial chunks."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []

        while True:
            sync_index = self._buffer.find(FRAME_SYNC)
            if sync_index < 0:
                if self._buffer[-1:] == FRAME_SYNC[:1]:
                    self._buffer[:] = FRAME_SYNC[:1]
                else:
                    self._buffer.clear()
                break
            if sync_index:
                del self._buffer[:sync_index]
            if len(self._buffer) < 4:
                break

            payload_length = self._buffer[3]
            if payload_length > MAX_PAYLOAD_LENGTH:
                del self._buffer[:2]
                continue

            frame_length = 5 + payload_length
            if len(self._buffer) < frame_length:
                break

            raw_frame = bytes(self._buffer[:frame_length])
            try:
                frames.append(decode_frame(raw_frame))
            except ValueError:
                del self._buffer[:2]  # Only consume the false sync bytes on CRC failure
                continue
            del self._buffer[:frame_length]

        return frames
