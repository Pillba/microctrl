"""Interactive bench menu for firmware/bench/dispense_UI over the framed protocol.

Same menu as the plain-text firmware/bench/dispense/src/dispense.cpp, but every
command is sent as a CRC-checked frame at 115200 baud instead of an ASCII line.
The production firmware answers only test, raw, busy, noise and c (it ignores
the other bench commands, so they time out with a NOTE saying so). A production
build made with DEBUG=1 also prints DBG progress lines:

    test N [Q] [G]  DISPENSE_CMD slot N, quantity Q (default 1)  -> DISPENSE_RESULT
                 (N = 7 is the liquid dispenser, Q in mL). G = grams per pill (per mL
                 for 7), sent as unit_weight_mg; without G the firmware's default is used
    raw S Q [G]  DISPENSE_CMD with no range check (slot 0, 8, quantity 0, ...)
    busy N [Q] [G]  test N, then a second command 1 s later: expects BUSY (3)
                 for the second within 5 s, then the first one's result
    noise N [Q] [G]  test N while random non-0xAA bytes are sent: expects exactly
                 one reply
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

The port is --port, else PILLBA_UART_PORT, else /dev/ttyUSB0 if it exists,
else /dev/serial0.

Interactive:
    PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py --port /dev/cu.usbserial-1140

Scripted (exit code 0 only if every command succeeds):
    PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py \\
        --port /dev/serial0 -c 8 -c "test 1" -c w

Expected failures (a dispense passes when its result code is C):
    PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py \\
        --expect-code 2 -c "raw 0 1" -c "raw 8 1" -c "raw 1 0"
"""

from __future__ import annotations

import argparse
import itertools
import os
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

PORT_ENV = "PILLBA_UART_PORT"
DEFAULT_PORTS = ("/dev/ttyUSB0", "/dev/serial0")
DEFAULT_PORT = DEFAULT_PORTS[-1]
BAUD = 115200
LIQUID_SLOT = 7

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

BUSY_DELAY = 1.0        # busy: seconds between the first and the second command
BUSY_REPLY_WAIT = 5.0   # busy: how long the BUSY reply to the second command may take
NOISE_INTERVAL = 0.2    # noise: seconds between bursts of random bytes
NOISE_BURST = 8         # noise: bytes per burst
EXTRA_REPLY_WAIT = 2.0  # noise: how long to listen for extra replies after the result


def default_port(environ=os.environ, exists=os.path.exists) -> str:
    """PILLBA_UART_PORT, else the first of DEFAULT_PORTS that exists."""
    configured = environ.get(PORT_ENV, "").strip()
    if configured:
        return configured
    for candidate in DEFAULT_PORTS:
        if exists(candidate):
            return candidate
    return DEFAULT_PORT


def dispense_timeout(slot: int, quantity: int) -> float:
    """Longer than the MCU's worst case (protocol/protocol.md)."""
    if slot == LIQUID_SLOT:
        return 50.0 + 2.0 * quantity
    if 1 <= slot <= 6 and quantity > 0:
        return 45.0 + 15.0 * quantity
    return 10.0  # an invalid request is answered at once


def noise_bytes(size: int = NOISE_BURST) -> bytes:
    """Random bytes that can never start a frame (no 0xAA)."""
    return bytes(b if b != FRAME_SYNC_0 else 0x00 for b in os.urandom(size))


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


MAX_UNIT_WEIGHT_MG = 0xFFFF  # unit_weight_mg is a uint16


def dispense_frame(request_id: bytes, slot: int, quantity: int, unit_mg: int | None = None) -> Frame:
    """DISPENSE_CMD: 6-byte payload, or 8 bytes with unit_weight_mg (uint16 LE) when unit_mg is set."""
    if len(request_id) != 4:
        raise ValueError("request ID must be 4 bytes")
    payload = request_id + bytes([slot, quantity])
    if unit_mg is not None:
        if not 0 <= unit_mg <= MAX_UNIT_WEIGHT_MG:
            raise ValueError("unit weight must be 0 to 65535 mg")
        payload += unit_mg.to_bytes(2, "little")
    return Frame(DISPENSE_CMD, payload)


def grams_to_milligrams(text: str) -> int:
    """Unit weight for DISPENSE_CMD: '0.65' -> 650 mg (0.001 to 65.535 g)."""
    try:
        grams = float(text)
    except ValueError as error:
        raise MenuError(f"not a number of grams: {text!r}") from error
    milligrams = round(grams * 1000)
    if not 1 <= milligrams <= MAX_UNIT_WEIGHT_MG:
        raise MenuError("grams per pill (or per mL) must be 0.001 to 65.535")
    return milligrams


def grams_to_micrograms(text: str) -> int:
    try:
        grams = float(text)
    except ValueError as error:
        raise MenuError(f"not a number of grams: {text!r}") from error
    if grams <= 0:
        raise MenuError("grams must be more than 0")
    return round(grams * MICROGRAMS_PER_GRAM)


def parse_test_words(words: list[str]) -> tuple[int, int, int | None]:
    """'test N [Q] [G]' (also busy / noise): slot 1-7, quantity 1-255, and the optional
    unit weight in grams, returned as milligrams (None: let the firmware use its default)."""
    if len(words) not in (2, 3, 4) or not words[1].isdigit() or not 1 <= int(words[1]) <= LIQUID_SLOT:
        raise MenuError(
            f"use {words[0]} 1 to {words[0]} 7, optionally followed by a quantity (mL for 7) "
            "and the grams per pill (per mL for 7)"
        )
    if len(words) >= 3 and not words[2].isdigit():
        raise MenuError("quantity must be 1 to 255")
    quantity = int(words[2]) if len(words) >= 3 else 1
    if not 1 <= quantity <= 255:
        raise MenuError("quantity must be 1 to 255")
    unit_mg = grams_to_milligrams(words[3]) if len(words) == 4 else None
    return int(words[1]), quantity, unit_mg


def parse_menu_line(line: str, request_id: bytes) -> MenuCommand:
    """Map one dispense.cpp menu line to its frame. 'c' is two steps, see run_calibration()."""
    words = line.strip().split()
    if not words:
        raise MenuError("empty command")
    word = words[0].lower()

    if word in ("test", "busy", "noise"):
        slot, quantity, unit_mg = parse_test_words(words)
        unit = "mL" if slot == LIQUID_SLOT else "quantity"
        weight = ""
        if unit_mg is not None:
            weight = f", {unit_mg / 1000:g} g per {'mL' if slot == LIQUID_SLOT else 'pill'}"
        return MenuCommand(
            dispense_frame(request_id, slot, quantity, unit_mg),
            timeout=dispense_timeout(slot, quantity),
            label=f"{word} {slot} ({unit} {quantity}{weight})",
        )

    if word == "raw":
        if len(words) not in (3, 4) or not all(w.isdigit() and int(w) <= 255 for w in words[1:3]):
            raise MenuError("use raw <slot 0-255> <quantity 0-255> [grams per unit]")
        slot, quantity = int(words[1]), int(words[2])
        unit_mg = grams_to_milligrams(words[3]) if len(words) == 4 else None
        return MenuCommand(
            dispense_frame(request_id, slot, quantity, unit_mg),
            timeout=dispense_timeout(slot, quantity),
            label=f"raw slot {slot} quantity {quantity}",
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


PRODUCTION_BENCH_COMMANDS = (CMD_CALIBRATE_START, CMD_CALIBRATE_FINISH)


def no_answer_hint(sent: Frame) -> str | None:
    """Why a command may have gone unanswered: production ignores every bench
    command except calibration, so with production flashed they always time out."""
    if sent.msg_type != BENCH_CMD or len(sent.payload) < 5:
        return None
    command = sent.payload[4]
    if command in PRODUCTION_BENCH_COMMANDS:
        return None
    name = COMMAND_NAMES.get(command, f"0x{command:02X}")
    return (
        f"production firmware ignores bench command {name} (only calibration, c, runs there). "
        "Flash firmware/bench/dispense_UI to use it, or use test/raw/busy/noise with production."
    )


def frame_succeeded(frame: Frame, sent: Frame, expect_code: int | None = None) -> bool:
    """A DISPENSE_RESULT passes with code 0 and every unit confirmed, or, when
    expect_code is set, when its code is expect_code."""
    if frame.msg_type == DISPENSE_RESULT:
        if expect_code is not None:
            return frame.payload[5] == expect_code
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
    def __init__(self, connection, verbose: bool, expect_code: int | None = None) -> None:
        self._connection = connection
        self._verbose = verbose
        self.expect_code = expect_code
        self._splitter = StreamSplitter()
        self._responses: queue.Queue[Frame] = queue.Queue()
        self._stash: dict[bytes, Frame] = {}
        self._ids = itertools.count(1)
        self.busy_delay = BUSY_DELAY
        self.noise_interval = NOISE_INTERVAL
        self.extra_reply_wait = EXTRA_REPLY_WAIT
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

    def _write(self, frame: Frame, label: str) -> None:
        raw = encode_frame(frame.msg_type, frame.payload)
        print(f"  TX   {raw.hex(' ').upper()}   ({label})", flush=True)
        self._connection.write(raw)
        self._connection.flush()

    def _next_response(self, timeout: float) -> Frame | None:
        try:
            return self._responses.get(timeout=timeout)
        except queue.Empty:
            return None

    def _await(self, request_id: bytes, timeout: float, keep: tuple[bytes, ...] = ()) -> Frame | None:
        """The response to request_id, or None after timeout. Answers to the
        request IDs in keep are stashed for a later _await; others are shown."""
        if request_id in self._stash:
            return self._stash.pop(request_id)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frame = self._next_response(0.1)
            if frame is None:
                continue
            answered = frame.payload[:4]
            if answered == request_id:
                return frame
            if answered in keep:
                self._stash[answered] = frame
                continue
            print(f"  (late answer to another request) {describe_frame(frame)}", flush=True)
        return None

    def _judge(self, frame: Frame | None, sent: Frame, timeout: float) -> bool:
        if frame is None:
            print(f"  FAIL: no answer within {timeout:.0f} s (outcome UNKNOWN)", flush=True)
            hint = no_answer_hint(sent)
            if hint:
                print(f"  NOTE: {hint}", flush=True)
            return False
        ok = frame_succeeded(frame, sent, self.expect_code)
        print(f"  {'PASS' if ok else 'FAIL'}: {describe_frame(frame)}", flush=True)
        return ok

    def send(self, command: MenuCommand) -> bool:
        self._write(command.frame, command.label)
        frame = self._await(command.frame.payload[:4], command.timeout)
        return self._judge(frame, command.frame, command.timeout)

    def run_busy(self, command: MenuCommand) -> bool:
        """A second command during a dispense must be answered BUSY (3) at once."""
        first = command.frame
        second = Frame(DISPENSE_CMD, self.next_request_id() + first.payload[4:])  # same slot, quantity, weight
        started = time.monotonic()
        self._write(first, command.label)
        time.sleep(self.busy_delay)
        self._write(second, "second command, expect BUSY")
        reply = self._await(second.payload[:4], BUSY_REPLY_WAIT, keep=(first.payload[:4],))
        busy_ok = (
            reply is not None
            and reply.msg_type == DISPENSE_RESULT
            and reply.payload[5] == 3
            and reply.payload[6] == 0
        )
        if reply is None:
            print(f"  FAIL: no BUSY within {BUSY_REPLY_WAIT:.0f} s", flush=True)
        else:
            print(f"  {'PASS' if busy_ok else 'FAIL'} (second): {describe_frame(reply)}", flush=True)
        remaining = max(0.0, command.timeout - (time.monotonic() - started))
        first_ok = self._judge(self._await(first.payload[:4], remaining), first, command.timeout)
        return busy_ok and first_ok

    def run_noise(self, command: MenuCommand) -> bool:
        """Random non-0xAA bytes during a dispense must not cause extra replies."""
        sent = command.frame
        request_id = sent.payload[:4]
        self._write(sent, command.label)
        result: Frame | None = None
        extra: list[Frame] = []
        bursts = 0
        deadline = time.monotonic() + command.timeout
        next_burst = time.monotonic()
        while time.monotonic() < deadline:
            now = time.monotonic()
            if result is None and now >= next_burst:
                self._connection.write(noise_bytes())
                self._connection.flush()
                bursts += 1
                next_burst = now + self.noise_interval
            frame = self._next_response(0.01)
            if frame is None:
                continue
            if frame.payload[:4] == request_id and result is None:
                result = frame
                deadline = time.monotonic() + self.extra_reply_wait  # keep listening for extras
            else:
                extra.append(frame)
        print(f"  sent {bursts} bursts of random bytes", flush=True)
        for frame in extra:
            print(f"  FAIL (extra reply): {describe_frame(frame)}", flush=True)
        return self._judge(result, sent, command.timeout) and not extra

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
        command = parse_menu_line(line, self.next_request_id())
        word = line.split()[0].lower()
        if word == "busy":
            return self.run_busy(command)
        if word == "noise":
            return self.run_noise(command)
        return self.send(command)


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
        description="dispense.cpp menu over the framed protocol, for firmware/bench/dispense_UI and the production firmware.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=HELP,
    )
    parser.add_argument(
        "--port", default=None,
        help=f"default: ${PORT_ENV}, else {DEFAULT_PORTS[0]} if it exists, else {DEFAULT_PORTS[-1]}",
    )
    parser.add_argument(
        "-c", "--command", action="append", default=[],
        help="run this menu command and exit (repeatable); without it the menu is interactive",
    )
    parser.add_argument("--settle", type=float, default=0.5, help="pause between scripted commands, seconds")
    parser.add_argument("-v", "--verbose", action="store_true", help="also print raw received frames")
    parser.add_argument(
        "--boot-wait", type=float, default=5.0,
        help="seconds to wait after opening the port, in case it resets the MCU (production needs about 4 s)",
    )
    parser.add_argument(
        "--expect-code", type=int, default=None, metavar="C",
        help="a dispense (test, raw, busy, noise) passes when its result code is C, e.g. 2 for invalid requests",
    )
    args = parser.parse_args()
    port = args.port or default_port()

    import serial  # imported here so the unit tests do not need pyserial

    try:
        print(f"Opening {port} at {BAUD} 8N1", flush=True)
        with serial.Serial(
            port=port,
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
            session = BenchSession(connection, args.verbose, args.expect_code)
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
