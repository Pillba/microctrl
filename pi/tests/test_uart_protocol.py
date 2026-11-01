import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))

from protocol.uart_protocol import Frame, decode_frame, encode_frame


class UartProtocolTests(unittest.TestCase):
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
