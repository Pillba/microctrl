import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))

from protocol.uart_protocol import Frame, decode_frame, encode_frame


class UartProtocolTests(unittest.TestCase):
    def test_known_dispense_frame_matches_supplied_bytes(self):
        frame = Frame(
            0x10,
            bytes.fromhex("99 AC A9 BB 01 01"),
        )
        raw = bytes.fromhex("AA 55 10 06 99 AC A9 BB 01 01 32")

        self.assertEqual(encode_frame(frame.msg_type, frame.payload), raw)
        self.assertEqual(decode_frame(raw), frame)

    def test_round_trip_preserves_message_and_payload(self):
        frame = Frame(0x10, bytes.fromhex("010203040203"))

        encoded = encode_frame(frame.msg_type, frame.payload)

        self.assertEqual(decode_frame(encoded), frame)

    def test_crc_failure_is_rejected(self):
        encoded = bytearray(encode_frame(0x10, b"\x01\x02"))
        encoded[-1] ^= 0xFF

        with self.assertRaises(ValueError, msg="invalid frame crc"):
            decode_frame(bytes(encoded))


if __name__ == "__main__":
    unittest.main()
