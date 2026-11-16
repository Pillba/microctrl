"""Deterministic tests for pi/tests/integration/bench_menu_frames.py (no serial port needed)."""

import sys
import time
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

    def test_unit_weight_makes_an_eight_byte_command(self):
        # protocol/test_vectors/frame_vectors.json "dispense command with unit weight, slot 1, 2 pills, 650 mg each"
        frame = bench.parse_menu_line("test 1 2 0.65", bytes.fromhex("01020304")).frame
        self.assertEqual(encode_frame(frame.msg_type, frame.payload).hex(), "aa5510080102030401028a02da")
        liquid = bench.parse_menu_line("test 7 5 1.03", bytes.fromhex("01020304")).frame
        self.assertEqual(encode_frame(liquid.msg_type, liquid.payload).hex(), "aa551008010203040705060416")

    def test_without_unit_weight_the_command_stays_six_bytes(self):
        for line in ("test 1", "test 1 2", "raw 0 1", "busy 1 2", "noise 1"):
            self.assertEqual(len(bench.parse_menu_line(line, RID).frame.payload), 6, line)
        self.assertEqual(len(bench.parse_menu_line("raw 1 1 0.4", RID).frame.payload), 8)

    def test_unit_weight_must_fit_sixteen_bits(self):
        for line in ("test 1 1 0", "test 1 1 66", "test 1 1 abc", "test 1 1 -1", "test 1 1 0.4 9"):
            with self.subTest(line=line), self.assertRaises(bench.MenuError):
                bench.parse_menu_line(line, RID)

    def test_liquid_test_is_slot_seven_in_ml(self):
        command = bench.parse_menu_line("test 7 5", RID)
        self.assertEqual(command.frame.payload, RID + bytes([7, 5]))
        self.assertEqual(command.timeout, 50.0 + 2.0 * 5)
        self.assertIn("mL 5", command.label)

    def test_raw_sends_any_slot_and_quantity(self):
        for line, slot, quantity in [("raw 0 1", 0, 1), ("raw 8 1", 8, 1), ("raw 1 0", 1, 0), ("raw 7 31", 7, 31)]:
            with self.subTest(line=line):
                command = bench.parse_menu_line(line, RID)
                self.assertEqual(command.frame.msg_type, 0x10)
                self.assertEqual(command.frame.payload, RID + bytes([slot, quantity]))
        self.assertEqual(bench.parse_menu_line("raw 0 1", RID).timeout, 10.0)

    def test_busy_and_noise_are_dispense_commands(self):
        for word in ("busy", "noise"):
            with self.subTest(word=word):
                command = bench.parse_menu_line(f"{word} 2 3", RID)
                self.assertEqual(command.frame.payload, RID + bytes([2, 3]))
                self.assertEqual(command.timeout, 45.0 + 15.0 * 3)

    def test_invalid_lines_are_rejected(self):
        for line in [
            "", "test", "test 0", "test 8", "test 1 0", "test 1 256", "test 1 x", "7", "e -1", "e abc",
            "xyz", "w 2", "raw", "raw 1", "raw 256 1", "raw -1 1", "raw 1 1 1 1", "raw 1 1 0", "busy", "noise 8",
        ]:
            with self.subTest(line=line):
                with self.assertRaises(bench.MenuError):
                    bench.parse_menu_line(line, RID)

    def test_dispense_timeout_grows_with_quantity(self):
        one = bench.parse_menu_line("test 1", RID).timeout
        three = bench.parse_menu_line("test 1 3", RID).timeout
        self.assertGreater(three, one)
        self.assertGreaterEqual(one, 30.0)  # a one-pill dispense can take about 30 s


class PortAndNoiseTests(unittest.TestCase):
    def test_port_env_wins_then_usb_then_gpio(self):
        def exists(*present):
            return lambda path: path in present

        self.assertEqual(
            bench.default_port({"PILLBA_UART_PORT": "/dev/serial/by-id/x"}, exists("/dev/ttyUSB0")),
            "/dev/serial/by-id/x",
        )
        self.assertEqual(bench.default_port({}, exists("/dev/ttyUSB0", "/dev/serial0")), "/dev/ttyUSB0")
        self.assertEqual(bench.default_port({}, exists("/dev/serial0")), "/dev/serial0")
        self.assertEqual(bench.default_port({"PILLBA_UART_PORT": "  "}, exists()), "/dev/serial0")

    def test_noise_never_starts_a_frame(self):
        for _ in range(200):
            self.assertNotIn(0xAA, bench.noise_bytes(64))


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

    def test_expected_code_replaces_the_success_rule(self):
        sent = bench.parse_menu_line("raw 0 1", RID).frame
        invalid = Frame(0x11, RID + bytes([0, 2, 0]))
        self.assertFalse(bench.frame_succeeded(invalid, sent))
        self.assertTrue(bench.frame_succeeded(invalid, sent, expect_code=2))
        self.assertFalse(bench.frame_succeeded(invalid, sent, expect_code=6))

    def test_no_answer_hint_names_bench_only_commands(self):
        for line in ["1", "9", "w", "pon", "stop"]:
            with self.subTest(line=line):
                hint = bench.no_answer_hint(bench.parse_menu_line(line, RID).frame)
                self.assertIn("production firmware ignores", hint)
                self.assertIn("dispense_UI", hint)
        self.assertIn("STEPPER_LOOP", bench.no_answer_hint(bench.parse_menu_line("1", RID).frame))

    def test_no_answer_hint_stays_quiet_for_production_commands(self):
        for line in ["test 1", "raw 0 1"]:
            self.assertIsNone(bench.no_answer_hint(bench.parse_menu_line(line, RID).frame))
        for command in (bench.CMD_CALIBRATE_START, bench.CMD_CALIBRATE_FINISH):
            self.assertIsNone(bench.no_answer_hint(bench.bench_frame(RID, command)))

    def test_production_debug_lines_are_text_not_frames(self):
        """A DEBUG=1 production build writes DBG lines between frames."""
        result = Frame(bench.DISPENSE_RESULT, RID + bytes([1, 0, 1]))
        stream = (
            b"DBG DISPENSE_CMD id 01020304 slot 1 qty 1: accepted\r\n"
            b"DBG dispense passed\r\n"
            + encode_frame(result.msg_type, result.payload)
            + b"DBG DISPENSE_RESULT id 01020304 slot 1: 0 OK, confirmed 1\r\n"
        )
        items = bench.StreamSplitter().feed(stream)
        self.assertEqual(
            items,
            [
                "DBG DISPENSE_CMD id 01020304 slot 1 qty 1: accepted",
                "DBG dispense passed",
                result,
                "DBG DISPENSE_RESULT id 01020304 slot 1: 0 OK, confirmed 1",
            ],
        )

    def test_help_lists_the_menu(self):
        for word in ["test N", "raw S Q", "busy N", "noise N", "stop", "pon", "e [grams]"]:
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


class FakeDispenser:
    """A one-dispense-at-a-time MCU: holds a DISPENSE_CMD until finish() (or, with
    auto_finish, until noise_needed noise bursts arrived), answers a second one BUSY."""

    def __init__(self, code=0, auto_finish=False, noise_needed=0):
        self.written = []
        self.noise = []
        self.in_waiting = 0
        self.code = code
        self.auto_finish = auto_finish
        self.noise_needed = noise_needed
        self.active = None
        self._pending = b""

    def _reply(self, payload, code, confirmed):
        frame = Frame(0x11, payload[:4] + bytes([payload[4], code, confirmed]))
        self._pending += encode_frame(frame.msg_type, frame.payload)

    def finish(self):
        if self.active is not None:
            ok = self.code == 0
            self._reply(self.active, self.code, self.active[5] if ok else 0)
            self.active = None

    def write(self, data):
        data = bytes(data)
        try:
            frame = bench.decode_frame(data)
        except ValueError:
            self.noise.append(data)
            if self.auto_finish and len(self.noise) >= self.noise_needed:
                self.finish()
            return
        self.written.append(frame)
        if self.active is None:
            self.active = frame.payload
            if self.auto_finish and self.noise_needed == 0:
                self.finish()
        else:
            self._reply(frame.payload, 3, 0)  # BUSY
            self.finish()  # then the first one completes

    def flush(self):
        pass

    def read(self, size):
        if not self._pending:
            time.sleep(0.005)
        data, self._pending = self._pending, b""
        return data


def make_session(connection, expect_code=None):
    session = bench.BenchSession(connection, verbose=False, expect_code=expect_code)
    session.busy_delay = 0.05
    session.noise_interval = 0.01
    session.extra_reply_wait = 0.1
    session.start()
    return session


class DispenseSessionTests(unittest.TestCase):
    def test_expect_code_makes_a_failure_pass(self):
        for expect, passes in [(None, False), (6, True), (7, False)]:
            with self.subTest(expect=expect):
                session = make_session(FakeDispenser(code=6, auto_finish=True), expect_code=expect)
                try:
                    self.assertEqual(session.run_line("test 7 5"), passes)
                finally:
                    session.close()

    def test_busy_expects_busy_for_the_second_then_the_first_result(self):
        connection = FakeDispenser(code=0)
        session = make_session(connection)
        try:
            self.assertTrue(session.run_line("busy 1"))
        finally:
            session.close()
        first, second = connection.written
        self.assertEqual(first.payload[4:], bytes([1, 1]))
        self.assertEqual(second.payload[4:], bytes([1, 1]))
        self.assertNotEqual(first.payload[:4], second.payload[:4])

    def test_busy_fails_when_the_second_command_is_not_refused(self):
        class NoBusy(FakeDispenser):
            def write(self, data):
                frame = bench.decode_frame(bytes(data))
                self.written.append(frame)
                self._reply(frame.payload, 0, frame.payload[5])  # answers everything OK

        session = make_session(NoBusy())
        try:
            self.assertFalse(session.run_line("busy 1"))
        finally:
            session.close()

    def test_noise_passes_with_exactly_one_reply(self):
        connection = FakeDispenser(code=0, auto_finish=True, noise_needed=3)
        session = make_session(connection)
        try:
            self.assertTrue(session.run_line("noise 2"))
        finally:
            session.close()
        self.assertGreaterEqual(len(connection.noise), 3)
        self.assertTrue(all(0xAA not in chunk for chunk in connection.noise))

    def test_noise_fails_on_an_extra_reply(self):
        class Chatty(FakeDispenser):
            def finish(self):
                if self.active is not None:
                    payload = self.active
                    super().finish()
                    self._reply(payload, 3, 0)  # a spurious second reply

        session = make_session(Chatty(code=0, auto_finish=True, noise_needed=2))
        try:
            self.assertFalse(session.run_line("noise 2"))
        finally:
            session.close()


if __name__ == "__main__":
    unittest.main()
