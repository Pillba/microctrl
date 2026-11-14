"""Interactive bench menu for firmware/bench/dispense_UI over the framed protocol.

Same menu as the plain-text firmware/bench/dispense/src/dispense.cpp, but every
command is sent as a CRC-checked frame at 115200 baud instead of an ASCII line:

    test N [Q]   DISPENSE_CMD slot N, quantity Q (default 1)  -> DISPENSE_RESULT
    1 ... 6      BENCH_CMD STEPPER_LOOP N    (run that stepper again and again)
    stop         BENCH_CMD STOP
    9 / 8        BENCH_CMD SHIELD open / closed
    pon / poff   BENCH_CMD PUMP on / off
    t            BENCH_CMD TARE
    r            BENCH_CMD NOISE_TEST
    w            BENCH_CMD READ_WEIGHT
    c            BENCH_CMD CALIBRATE_START, then CALIBRATE_FINISH with your known weight
    e [grams]    BENCH_CMD EXPECTED_WEIGHT (no grams: show it)
    help, quit

The MCU answers with BENCH_RESULT / DISPENSE_RESULT frames, sends BENCH_EVENT
frames (PLAY sound, HALL sensor, READY), and still logs dispense progress as
plain text lines. Both are shown as they arrive.

Interactive:
    PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py --port /dev/cu.usbserial-1140

Scripted (exit code 0 only if every command succeeds):
    PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py \\
        --port /dev/serial0 -c 8 -c "test 1" -c w
"""

from __future__ import annotations

import argparse
import itertools
import queue
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))

from protocol.uart_protocol import (  # noqa: E402
    DISPENSE_CMD,
    DISPENSE_RESULT,
    MAX_PAYLOAD_LENGTH,
    Frame,
    decode_frame,
    encode_frame,
)

DEFAULT_PORT = "/dev/serial0"
BAUD = 115200

FRAME_SYNC_0 = 0xAA
FRAME_SYNC_1 = 0x55

BENCH_CMD = 0x80
BENCH_RESULT = 0x81
BENCH_EVENT = 0x82

CMD_STEPPER_LOOP = 0x01
CMD_STOP = 0x02
CMD_SHIELD = 0x03
CMD_PUMP = 0x04
CMD_TARE = 0x05
CMD_NOISE_TEST = 0x06
CMD_READ_WEIGHT = 0x07
CMD_CALIBRATE_START = 0x08
CMD_CALIBRATE_FINISH = 0x09
CMD_EXPECTED_WEIGHT = 0x0A

COMMAND_NAMES = {
    CMD_STEPPER_LOOP: "STEPPER_LOOP",
    CMD_STOP: "STOP",
    CMD_SHIELD: "SHIELD",
    CMD_PUMP: "PUMP",
    CMD_TARE: "TARE",
    CMD_NOISE_TEST: "NOISE_TEST",
    CMD_READ_WEIGHT: "READ_WEIGHT",
    CMD_CALIBRATE_START: "CALIBRATE_START",
    CMD_CALIBRATE_FINISH: "CALIBRATE_FINISH",
    CMD_EXPECTED_WEIGHT: "EXPECTED_WEIGHT",
}

STATUS_OK = 0
STATUS_NAMES = {
    0: "OK",
    1: "FAILED (load cell does not answer)",
    2: "INVALID",
    3: "BUSY (a dispense is running)",
    4: "NOT_CALIBRATED",
    5: "UNSTEADY (saved, but readings were not steady)",
    6: "CANCELLED",
    7: "NO_CHANGE (is the weight on the scale? nothing saved)",
}
# Statuses that still count as success for the scripted exit code.
SUCCESS_STATUSES = {0, 4, 5}

DISPENSE_RESULT_NAMES = {
    0: "OK",
    1: "FAILED",
    2: "INVALID_REQUEST",
    3: "BUSY",
    4: "SCALE_NOT_RESPONDING",
    5: "IR_NOT_TRIPPED",
    6: "NO_WEIGHT",
    7: "WEIGHT_MISMATCH",
}

EVENT_PLAY_SOUND = 0x01
EVENT_HALL = 0x02
EVENT_READY = 0x03
HALL_NAMES = {0: "between", 1: "closed", 2: "open", 3: "error, both sensors detect"}

MICROGRAMS_PER_GRAM = 1_000_000

HELP = __doc__.split("\n\n")[2]


class MenuError(ValueError):
    """The typed line is not a valid menu command."""


@dataclass(frozen=True)
class MenuCommand:
    """One frame to send, and how long to wait for its answer."""

    frame: Frame
    timeout: float
    label: str


# ---------------------------------------------------------------- encoding


def bench_frame(request_id: bytes, command: int, arg: int = 0) -> Frame:
    if len(request_id) != 4:
        raise ValueError("request ID must be 4 bytes")
    return Frame(BENCH_CMD, request_id + bytes([command]) + arg.to_bytes(4, "little"))


def dispense_frame(request_id: bytes, slot: int, quantity: int) -> Frame:
    if len(request_id) != 4:
        raise ValueError("request ID must be 4 bytes")
    return Frame(DISPENSE_CMD, request_id + bytes([slot, quantity]))


def grams_to_micrograms(text: str) -> int:
    try:
        grams = float(text)
    except ValueError as error:
        raise MenuError(f"not a number of grams: {text!r}") from error
    if grams <= 0:
        raise MenuError("grams must be more than 0")
    return round(grams * MICROGRAMS_PER_GRAM)


def parse_menu_line(line: str, request_id: bytes) -> MenuCommand:
    """Map one dispense.cpp menu line to its frame. 'c' is two steps, see run_calibration()."""
    words = line.strip().split()
    if not words:
        raise MenuError("empty command")
    word = words[0].lower()

    if word == "test":
        if len(words) not in (2, 3) or not words[1].isdigit() or not 1 <= int(words[1]) <= 6:
            raise MenuError("use test 1 to test 6, optionally followed by a quantity")
        quantity = int(words[2]) if len(words) == 3 else 1
        if not 1 <= quantity <= 255:
            raise MenuError("quantity must be 1 to 255")
        return MenuCommand(
            dispense_frame(request_id, int(words[1]), quantity),
            timeout=45.0 + 15.0 * quantity,
            label=f"test {words[1]} (quantity {quantity})",
        )

    if len(words) == 1 and word in {"1", "2", "3", "4", "5", "6"}:
        return MenuCommand(bench_frame(request_id, CMD_STEPPER_LOOP, int(word)), 3.0, f"stepper {word} loop")

    simple = {
        "stop": (CMD_STOP, 0, 3.0),
        "9": (CMD_SHIELD, 1, 3.0),
        "8": (CMD_SHIELD, 0, 3.0),
        "pon": (CMD_PUMP, 1, 3.0),
        "poff": (CMD_PUMP, 0, 3.0),
        "t": (CMD_TARE, 0, 10.0),
        "r": (CMD_NOISE_TEST, 0, 15.0),
        "w": (CMD_READ_WEIGHT, 0, 10.0),
    }
    if word in simple and len(words) == 1:
        command, arg, timeout = simple[word]
        return MenuCommand(bench_frame(request_id, command, arg), timeout, word)

    if word == "e" and len(words) <= 2:
        arg = grams_to_micrograms(words[1]) if len(words) == 2 else 0
        return MenuCommand(bench_frame(request_id, CMD_EXPECTED_WEIGHT, arg), 3.0, line.strip())

    raise MenuError(f"unknown command {line.strip()!r} (type help)")


# ---------------------------------------------------------------- decoding


def _value(payload: bytes, offset: int) -> int:
    return int.from_bytes(payload[offset : offset + 4], "little", signed=True)


def format_grams(micrograms: int) -> str:
    grams = micrograms / MICROGRAMS_PER_GRAM
    return f"{grams:.3f} g ({grams * 1000:.0f} mg)"


def describe_frame(frame: Frame) -> str:
    """Human-readable line for a frame received from the MCU."""
    p = frame.payload
    if frame.msg_type == BENCH_RESULT and len(p) == 10:
        command, status, value = p[4], p[5], _value(p, 6)
        name = COMMAND_NAMES.get(command, f"0x{command:02X}")
        text = f"{name}: {STATUS_NAMES.get(status, f'status {status}')}"
        if status in (STATUS_OK, 4, 5):
            if command in (CMD_READ_WEIGHT, CMD_EXPECTED_WEIGHT):
                text += f", {format_grams(value)}"
            elif command == CMD_NOISE_TEST:
                text += f", raw spread over 50 readings {value}"
            elif command == CMD_CALIBRATE_FINISH:
                text += f", new factor {value / 100:.2f} (saved on this board)"
            elif command == CMD_SHIELD:
                text += ", open" if value else ", closed"
            elif command == CMD_PUMP:
                text += ", on" if value else ", off"
            elif command == CMD_STEPPER_LOOP:
                text += f", stepper {value} started"
        return text
    if frame.msg_type == DISPENSE_RESULT and len(p) == 7:
        slot, code, confirmed = p[4], p[5], p[6]
        name = DISPENSE_RESULT_NAMES.get(code, f"code {code}")
        return f"DISPENSE_RESULT: slot {slot}, {name}, confirmed quantity {confirmed}"
    if frame.msg_type == BENCH_EVENT and len(p) == 9:
        event, value = p[4], _value(p, 5)
        if event == EVENT_PLAY_SOUND:
            return f"EVENT: play sound {value}"
        if event == EVENT_HALL:
            return f"EVENT: hall {HALL_NAMES.get(value, value)}"
        if event == EVENT_READY:
            calibrated = "calibrated" if value & 1 else "NOT calibrated (run c)"
            beam = "HIGH" if value & 2 else "LOW"
            return f"EVENT: MCU ready, scale {calibrated}, clear beam reads {beam}"
        return f"EVENT: 0x{event:02X} value {value}"
    return f"FRAME 0x{frame.msg_type:02X}: {p.hex(' ').upper()}"


def frame_succeeded(frame: Frame, sent: Frame) -> bool:
    if frame.msg_type == DISPENSE_RESULT:
        return frame.payload[5] == 0 and frame.payload[6] == sent.payload[5]
    return frame.payload[5] in SUCCESS_STATUSES


class StreamSplitter:
    """Splits the UART byte stream into frames and plain-text log lines.

    The MCU writes whole frames and whole text lines, never interleaved, and its
    text is ASCII, so 0xAA always starts a frame candidate.
    """

    def __init__(self) -> None:
        self._buffer = bytearray()
        self._text = bytearray()

    def feed(self, data: bytes) -> list[Frame | str]:
        self._buffer.extend(data)
        out: list[Frame | str] = []
        while self._buffer:
            if self._buffer[0] != FRAME_SYNC_0:
                sync = self._buffer.find(FRAME_SYNC_0)
                end = sync if sync >= 0 else len(self._buffer)
                self._text.extend(self._buffer[:end])
                del self._buffer[:end]
                out.extend(self._take_lines())
                continue
            if len(self._buffer) < 2:
                break
            if self._buffer[1] != FRAME_SYNC_1:
                del self._buffer[:1]  # a stray 0xAA, not ASCII text: drop it
                continue
            if len(self._buffer) < 4:
                break
            length = self._buffer[3]
            if length > MAX_PAYLOAD_LENGTH:
                del self._buffer[:2]
                continue
            if len(self._buffer) < length + 5:
                break
            try:
                frame = decode_frame(bytes(self._buffer[: length + 5]))
            except ValueError:
                del self._buffer[:2]
                continue
            del self._buffer[: length + 5]
            out.append(frame)
        return out

    def _take_lines(self) -> list[str]:
        lines = []
        while b"\n" in self._text:
            line, _, rest = bytes(self._text).partition(b"\n")
            self._text = bytearray(rest)
            text = line.decode("ascii", errors="replace").strip("\r")
            if text:
                lines.append(text)
        return lines


# ---------------------------------------------------------------- serial session


class BenchSession:
    def __init__(self, connection, verbose: bool) -> None:
        self._connection = connection
        self._verbose = verbose
        self._splitter = StreamSplitter()
        self._responses: queue.Queue[Frame] = queue.Queue()
        self._ids = itertools.count(1)
        self._stop = threading.Event()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)

    def start(self) -> None:
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        self._reader.join(timeout=1.0)

    def next_request_id(self) -> bytes:
        return next(self._ids).to_bytes(4, "big")

    def _read_loop(self) -> None:
        while not self._stop.is_set():
            data = self._connection.read(self._connection.in_waiting or 1)
            if not data:
                continue
            for item in self._splitter.feed(data):
                if isinstance(item, str):
                    print(f"  MCU> {item}", flush=True)
                    continue
                if self._verbose:
                    raw = encode_frame(item.msg_type, item.payload)
                    print(f"  RX   {raw.hex(' ').upper()}", flush=True)
                if item.msg_type == BENCH_EVENT:
                    print(f"  {describe_frame(item)}", flush=True)
                else:
                    self._responses.put(item)

    def send(self, command: MenuCommand) -> bool:
        raw = encode_frame(command.frame.msg_type, command.frame.payload)
        print(f"  TX   {raw.hex(' ').upper()}   ({command.label})", flush=True)
        self._connection.write(raw)
        self._connection.flush()

        request_id = command.frame.payload[:4]
        deadline = time.monotonic() + command.timeout
        while time.monotonic() < deadline:
            try:
                frame = self._responses.get(timeout=0.1)
            except queue.Empty:
                continue
            if frame.payload[:4] != request_id:
                print(f"  (late answer to another request) {describe_frame(frame)}", flush=True)
                continue
            ok = frame_succeeded(frame, command.frame)
            print(f"  {'PASS' if ok else 'FAIL'}: {describe_frame(frame)}", flush=True)
            return ok
        print(f"  FAIL: no answer within {command.timeout:.0f} s (outcome UNKNOWN)", flush=True)
        return False

    def run_calibration(self, ask) -> bool:
        """The two-step 'c' flow. ask(prompt) returns the user's answer."""
        ask("Remove everything from the scale (the empty cup may stay), then press Enter.")
        start = MenuCommand(
            bench_frame(self.next_request_id(), CMD_CALIBRATE_START), 30.0, "c, zero the empty scale"
        )
        if not self.send(start):
            return False

        answer = ask("Place the known weight in the CENTER of the scale, type its weight in GRAMS "
                     "and press Enter (empty cancels):").strip()
        try:
            micrograms = grams_to_micrograms(answer) if answer else 0
        except MenuError as error:
            print(f"  {error}; cancelling", flush=True)
            micrograms = 0
        finish = MenuCommand(
            bench_frame(self.next_request_id(), CMD_CALIBRATE_FINISH, micrograms),
            60.0,
            "c, measure the known weight" if micrograms else "c, cancel",
        )
        ok = self.send(finish)
        if ok and micrograms:
            print("  Send w to read the weight now, it should show the known weight. "
                  "Then remove it and send t.", flush=True)
        return ok and micrograms > 0

    def run_line(self, line: str, ask=input) -> bool:
        if line.strip().lower() == "c":
            return self.run_calibration(ask)
        return self.send(parse_menu_line(line, self.next_request_id()))


# ---------------------------------------------------------------- main


def interactive(session: BenchSession) -> int:
    print(HELP)
    while True:
        try:
            line = input("bench> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not line:
            continue
        if line.lower() in {"q", "quit", "exit"}:
            return 0
        if line.lower() in {"h", "help", "?"}:
            print(HELP)
            continue
        try:
            session.run_line(line)
        except MenuError as error:
            print(f"  {error}")
        except KeyboardInterrupt:
            print("\n  stopped waiting (the MCU may still answer)")


def scripted(session: BenchSession, commands: list[str], settle: float) -> int:
    failures = 0
    for line in commands:
        print(f"bench> {line}", flush=True)
        try:
            if not session.run_line(line):
                failures += 1
        except MenuError as error:
            print(f"  {error}")
            failures += 1
        time.sleep(settle)
    print(f"{len(commands) - failures}/{len(commands)} commands passed", flush=True)
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="dispense.cpp menu over the framed protocol, for firmware/bench/dispense_UI.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=HELP,
    )
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument(
        "-c", "--command", action="append", default=[],
        help="run this menu command and exit (repeatable); without it the menu is interactive",
    )
    parser.add_argument("--settle", type=float, default=0.5, help="pause between scripted commands, seconds")
    parser.add_argument("-v", "--verbose", action="store_true", help="also print raw received frames")
    parser.add_argument(
        "--boot-wait", type=float, default=2.0,
        help="seconds to wait after opening the port, in case it resets the MCU",
    )
    args = parser.parse_args()

    import serial  # imported here so the unit tests do not need pyserial

    try:
        with serial.Serial(
            port=args.port,
            baudrate=BAUD,
            timeout=0.05,
            write_timeout=1.0,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as connection:
            connection.reset_input_buffer()
            session = BenchSession(connection, args.verbose)
            session.start()
            time.sleep(args.boot_wait)
            try:
                if args.command:
                    return scripted(session, args.command, args.settle)
                return interactive(session)
            finally:
                session.close()
    except (OSError, serial.SerialException) as error:
        print(f"UART ERROR: {error}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
