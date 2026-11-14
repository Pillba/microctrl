"""Deterministic tests for pi/tests/integration/bench_menu_frames.py (no serial port needed)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))
sys.path.insert(0, str(Path(__file__).parent / "integration"))

import bench_menu_frames as bench  # noqa: E402
from protocol.uart_protocol import Frame, encode_frame  # noqa: E402

RID = bytes.fromhex("00 00 00 2A")


def payload_for(line: str) -> bytes:
    return bench.parse_menu_line(line, RID).frame.payload


class MenuMappingTests(unittest.TestCase):
    """Every dispense.cpp menu command maps to one frame with the firmware's layout:
    request_id[4] command[1] arg[4, uint32 little-endian]."""

    def test_test_n_is_a_dispense_command_with_quantity_one(self):
        frame = bench.parse_menu_line("test 3", RID).frame
        self.assertEqual(frame.msg_type, 0x10)
        self.assertEqual(frame.payload, RID + bytes([3, 1]))

    def test_test_n_accepts_an_optional_quantity(self):
        self.assertEqual(payload_for("test 6 2"), RID + bytes([6, 2]))

    def test_digit_starts_the_stepper_loop(self):
        for slot in range(1, 7):
            frame = bench.parse_menu_line(str(slot), RID).frame
            self.assertEqual(frame.msg_type, bench.BENCH_CMD)
            self.assertEqual(frame.payload, RID + bytes([0x01, slot, 0, 0, 0]))

    def test_simple_commands(self):
        expected = {
            "stop": (0x02, 0),
            "9": (0x03, 1),
            "8": (0x03, 0),
            "pon": (0x04, 1),
            "poff": (0x04, 0),
            "t": (0x05, 0),
            "r": (0x06, 0),
            "w": (0x07, 0),
            "e": (0x0A, 0),
        }
        for line, (command, arg) in expected.items():
            with self.subTest(line=line):
                self.assertEqual(payload_for(line), RID + bytes([command]) + arg.to_bytes(4, "little"))

    def test_expected_weight_is_sent_in_micrograms(self):
        self.assertEqual(payload_for("e 0.146"), RID + bytes([0x0A]) + (146_000).to_bytes(4, "little"))
        self.assertEqual(payload_for("e 12.5"), RID + bytes([0x0A]) + (12_500_000).to_bytes(4, "little"))

    def test_full_frame_bytes(self):
        frame = bench.parse_menu_line("pon", RID).frame
        raw = encode_frame(frame.msg_type, frame.payload)
        self.assertEqual(raw[:4], bytes.fromhex("AA 55 80 09"))
        self.assertEqual(len(raw), 9 + 5)

    def test_invalid_lines_are_rejected(self):
        for line in ["", "test", "test 0", "test 7", "test 1 0", "test 1 256", "7", "e -1", "e abc", "xyz", "w 2"]:
            with self.subTest(line=line):
                with self.assertRaises(bench.MenuError):
                    bench.parse_menu_line(line, RID)

    def test_dispense_timeout_grows_with_quantity(self):
        one = bench.parse_menu_line("test 1", RID).timeout
        three = bench.parse_menu_line("test 1 3", RID).timeout
        self.assertGreater(three, one)
        self.assertGreaterEqual(one, 30.0)  # a one-pill dispense can take about 30 s


class StreamSplitterTests(unittest.TestCase):
    def frame(self, msg_type, payload):
        return encode_frame(msg_type, payload)

    def test_text_and_frames_are_separated_in_order(self):
        event = self.frame(bench.BENCH_EVENT, bytes(4) + bytes([1]) + (1).to_bytes(4, "little"))
        data = b"TEST: Taring the scale...\r\n" + event + b"VERIFY: Scale has tared.\r\n"
        items = bench.StreamSplitter().feed(data)
        self.assertEqual(items[0], "TEST: Taring the scale...")
        self.assertIsInstance(items[1], Frame)
        self.assertEqual(items[1].msg_type, bench.BENCH_EVENT)
        self.assertEqual(items[2], "VERIFY: Scale has tared.")

    def test_input_split_across_reads(self):
        result = self.frame(0x11, RID + bytes([1, 0, 1]))
        data = b"SETUP: ready\n" + result
        splitter = bench.StreamSplitter()
        items = []
        for byte in data:
            items.extend(splitter.feed(bytes([byte])))
        self.assertEqual(items[0], "SETUP: ready")
        self.assertEqual(items[1], Frame(0x11, RID + bytes([1, 0, 1])))

    def test_bad_crc_and_stray_sync_are_dropped(self):
        good = self.frame(0x11, RID + bytes([1, 0, 1]))
        bad = bytearray(good)
        bad[-1] ^= 0xFF
        items = bench.StreamSplitter().feed(b"\xaa" + bytes(bad) + good)
        frames = [item for item in items if isinstance(item, Frame)]
        self.assertEqual(frames, [Frame(0x11, RID + bytes([1, 0, 1]))])


class DescribeTests(unittest.TestCase):
    def result(self, command, status, value):
        return Frame(bench.BENCH_RESULT, RID + bytes([command, status]) + value.to_bytes(4, "little", signed=True))

    def test_weight_is_shown_in_grams_and_milligrams(self):
        text = bench.describe_frame(self.result(bench.CMD_READ_WEIGHT, 0, 146_000))
        self.assertIn("0.146 g (146 mg)", text)

    def test_negative_weight(self):
        text = bench.describe_frame(self.result(bench.CMD_READ_WEIGHT, 4, -2_000))
        self.assertIn("-0.002 g", text)
        self.assertIn("NOT_CALIBRATED", text)

    def test_calibration_factor(self):
        text = bench.describe_frame(self.result(bench.CMD_CALIBRATE_FINISH, 0, 42_012))
        self.assertIn("420.12", text)

    def test_events(self):
        hall = Frame(bench.BENCH_EVENT, bytes(4) + bytes([2]) + (1).to_bytes(4, "little"))
        ready = Frame(bench.BENCH_EVENT, bytes(4) + bytes([3]) + (3).to_bytes(4, "little"))
        self.assertEqual(bench.describe_frame(hall), "EVENT: hall closed")
        self.assertIn("scale calibrated", bench.describe_frame(ready))
        self.assertIn("reads HIGH", bench.describe_frame(ready))

    def test_success_rules(self):
        sent = bench.parse_menu_line("test 1 2", RID).frame
        self.assertTrue(bench.frame_succeeded(Frame(0x11, RID + bytes([1, 0, 2])), sent))
        self.assertFalse(bench.frame_succeeded(Frame(0x11, RID + bytes([1, 0, 1])), sent))
        self.assertFalse(bench.frame_succeeded(Frame(0x11, RID + bytes([1, 3, 0])), sent))
        w = bench.parse_menu_line("w", RID).frame
        self.assertTrue(bench.frame_succeeded(self.result(bench.CMD_READ_WEIGHT, 4, 0), w))
        self.assertFalse(bench.frame_succeeded(self.result(bench.CMD_READ_WEIGHT, 1, 0), w))

    def test_help_lists_the_menu(self):
        for word in ["test N", "stop", "pon", "e [grams]"]:
            self.assertIn(word, bench.HELP)


class FakeConnection:
    """Answers every BENCH_CMD with OK, echoing the request ID."""

    def __init__(self):
        self.written = []
        self.in_waiting = 0
        self._pending = b""

    def write(self, data):
        self.written.append(bytes(data))
        frame = bench.decode_frame(bytes(data))
        reply = Frame(bench.BENCH_RESULT, frame.payload[:5] + bytes([0]) + (420_00).to_bytes(4, "little"))
        self._pending += encode_frame(reply.msg_type, reply.payload)

    def flush(self):
        pass

    def read(self, size):
        data, self._pending = self._pending, b""
        return data


class SessionTests(unittest.TestCase):
    def test_calibration_sends_start_then_finish_with_micrograms(self):
        connection = FakeConnection()
        session = bench.BenchSession(connection, verbose=False)
        session.start()
        try:
            answers = iter(["", "10"])
            self.assertTrue(session.run_line("c", ask=lambda prompt: next(answers)))
        finally:
            session.close()
        start, finish = (bench.decode_frame(raw) for raw in connection.written)
        self.assertEqual(start.payload[4], bench.CMD_CALIBRATE_START)
        self.assertEqual(finish.payload[4], bench.CMD_CALIBRATE_FINISH)
        self.assertEqual(int.from_bytes(finish.payload[5:9], "little"), 10_000_000)
        self.assertNotEqual(start.payload[:4], finish.payload[:4])


if __name__ == "__main__":
    unittest.main()
