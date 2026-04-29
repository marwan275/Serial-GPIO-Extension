"""Send grouped digital write frames to blink one or more pins together."""

from __future__ import annotations

import argparse
from collections.abc import Iterable
import dataclasses
import queue
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError as error:
    raise SystemExit(
        "pyserial is required. Install it with: pip install pyserial"
    ) from error


SUCCESS_ERROR_CODE = 255
MIN_PHYSICAL_PIN = 1
MAX_PHYSICAL_PIN = 41


@dataclasses.dataclass(frozen=True)
class ParsedResponse:
    raw_line: str
    frame_type: str
    request_id: int
    value: int
    error: int
    debug_message: str | None = None


@dataclasses.dataclass(frozen=True)
class ValidationStatistics:
    expected_response_count: int
    received_line_count: int
    parsed_response_count: int
    success_response_count: int
    error_response_count: int
    malformed_line_count: int
    missing_response_count: int
    duplicate_response_count: int
    unexpected_response_count: int
    debug_response_count: int
    validation_failure_count: int


def looks_like_teensy(port_info: list_ports.ListPortInfo) -> bool:
    text_fields = [
        port_info.description or "",
        port_info.manufacturer or "",
        port_info.product or "",
        port_info.hwid or "",
    ]
    joined_text = " ".join(text_fields).lower()

    return "teensy" in joined_text or port_info.vid == 0x16C0


def resolve_port(requested_port: str | None) -> str:
    if requested_port:
        return requested_port

    available_ports = list(list_ports.comports())
    if not available_ports:
        raise SystemExit(
            "No serial ports found. Connect the board or pass the port explicitly."
        )

    preferred_ports = [
        port_info for port_info in available_ports if looks_like_teensy(port_info)
    ]
    if len(preferred_ports) == 1:
        selected_port = preferred_ports[0]
        print(
            f"Auto-detected Teensy port: {selected_port.device} ({selected_port.description})"
        )
        return selected_port.device

    if len(available_ports) == 1:
        selected_port = available_ports[0]
        print(
            f"Auto-detected serial port: {selected_port.device} ({selected_port.description})"
        )
        return selected_port.device

    port_lines = [
        f"  - {port_info.device}: {port_info.description}"
        for port_info in available_ports
    ]
    raise SystemExit(
        "Multiple serial ports found. Pass the port explicitly.\nAvailable ports:\n"
        + "\n".join(port_lines)
    )


def normalize_pins(pins: int | Iterable[int]) -> list[int]:
    if isinstance(pins, int):
        normalized_pins = [pins]
    else:
        normalized_pins = list(pins)

    if not normalized_pins:
        raise ValueError("at least one pin is required")

    invalid_pins = [
        pin
        for pin in normalized_pins
        if pin < MIN_PHYSICAL_PIN or pin > MAX_PHYSICAL_PIN
    ]
    if invalid_pins:
        invalid_pin_text = ", ".join(str(pin) for pin in invalid_pins)
        raise ValueError(
            f"pins must be between {MIN_PHYSICAL_PIN} and {MAX_PHYSICAL_PIN}: {invalid_pin_text}"
        )

    seen_pins: set[int] = set()
    duplicate_pins: list[int] = []
    for pin in normalized_pins:
        if pin in seen_pins and pin not in duplicate_pins:
            duplicate_pins.append(pin)
        seen_pins.add(pin)

    if duplicate_pins:
        duplicate_pin_text = ", ".join(str(pin) for pin in duplicate_pins)
        raise ValueError(f"duplicate pins are not allowed: {duplicate_pin_text}")

    return normalized_pins


def format_pins(pins: list[int]) -> str:
    return ", ".join(str(pin) for pin in pins)


def build_blink_batches(pins: int | Iterable[int], blink_count: int) -> list[list[str]]:
    normalized_pins = normalize_pins(pins)
    request_id = 1
    blink_batches: list[list[str]] = []

    for _ in range(blink_count):
        high_batch: list[str] = []
        for pin in normalized_pins:
            high_batch.append(f"@W,{request_id},D,L,{pin},1;\n")
            request_id += 1
        blink_batches.append(high_batch)

        low_batch: list[str] = []
        for pin in normalized_pins:
            low_batch.append(f"@W,{request_id},D,L,{pin},0;\n")
            request_id += 1
        blink_batches.append(low_batch)

    return blink_batches


def build_blink_frames(pins: int | Iterable[int], blink_count: int) -> list[str]:
    blink_batches = build_blink_batches(pins, blink_count)
    return [frame for batch in blink_batches for frame in batch]


def expected_request_ids(frames: list[str]) -> list[int]:
    request_ids: list[int] = []

    for frame in frames:
        parts = frame.strip().removeprefix("@").removesuffix(";").split(",")
        request_ids.append(int(parts[1]))

    return request_ids


def parse_response_line(line: str) -> ParsedResponse:
    if not line.startswith("@") or not line.endswith(";"):
        raise ValueError("line is not a framed response")

    parts = line[1:-1].split(",")
    frame_type = parts[0]

    if frame_type == "B":
        if len(parts) < 2:
            raise ValueError("debug frame missing debug message")

        debug_message = ",".join(parts[1:])
        return ParsedResponse(
            raw_line=line,
            frame_type=frame_type,
            request_id=0,
            value=0,
            error=0,
            debug_message=debug_message,
        )

    if len(parts) != 4:
        raise ValueError(f"expected 4 response fields, got {len(parts)}")

    frame_type, request_id_text, value_text, error_text = parts
    if frame_type not in {"S", "E"}:
        raise ValueError(f"unexpected frame type {frame_type!r}")

    return ParsedResponse(
        raw_line=line,
        frame_type=frame_type,
        request_id=int(request_id_text),
        value=int(value_text),
        error=int(error_text),
    )


def validate_responses(
    sent_frames: list[str], received_lines: list[str]
) -> tuple[list[str], ValidationStatistics]:
    expected_ids = expected_request_ids(sent_frames)
    parsed_responses: list[ParsedResponse] = []
    failures: list[str] = []
    malformed_line_count = 0
    success_response_count = 0
    error_response_count = 0
    debug_response_count = 0
    missing_response_count = 0
    duplicate_response_count = 0
    unexpected_response_count = 0

    for line in received_lines:
        try:
            parsed_responses.append(parse_response_line(line))
        except ValueError as error:
            malformed_line_count += 1
            failures.append(f"Unexpected response line {line!r}: {error}")

    responses_by_id: dict[int, list[ParsedResponse]] = {}
    for response in parsed_responses:
        if response.frame_type == "B":
            debug_response_count += 1
            continue

        responses_by_id.setdefault(response.request_id, []).append(response)

        if response.frame_type != "S":
            error_response_count += 1
            failures.append(
                f"Request {response.request_id} returned error frame {response.raw_line!r}"
            )
        elif response.error != SUCCESS_ERROR_CODE:
            error_response_count += 1
            failures.append(
                f"Request {response.request_id} returned unexpected success error code {response.error}"
            )
        else:
            success_response_count += 1

    for request_id in expected_ids:
        matching_responses = responses_by_id.get(request_id, [])
        if not matching_responses:
            missing_response_count += 1
            failures.append(f"Missing response for request {request_id}")
        elif len(matching_responses) > 1:
            duplicate_response_count += 1
            failures.append(f"Duplicate responses for request {request_id}")

    unexpected_ids = sorted(set(responses_by_id) - set(expected_ids))
    for request_id in unexpected_ids:
        unexpected_response_count += 1
        failures.append(f"Received unexpected response for request {request_id}")

    statistics = ValidationStatistics(
        expected_response_count=len(expected_ids),
        received_line_count=len(received_lines),
        parsed_response_count=len(parsed_responses),
        success_response_count=success_response_count,
        error_response_count=error_response_count,
        malformed_line_count=malformed_line_count,
        missing_response_count=missing_response_count,
        duplicate_response_count=duplicate_response_count,
        unexpected_response_count=unexpected_response_count,
        debug_response_count=debug_response_count,
        validation_failure_count=len(failures),
    )

    return failures, statistics


def print_statistics(
    resolved_port: str,
    pins: list[int],
    blink_count: int,
    interval_seconds: float,
    interval_blink: float | None,
    sent_frames: list[str],
    statistics: ValidationStatistics,
) -> None:
    write_frame_count = sum(frame.startswith("@W,") for frame in sent_frames)

    if interval_blink is not None and interval_blink > 0.0:
        blink_frequency_hz = 1.0 / interval_blink
        state_change_rate = 2.0 / interval_blink
        frequency_summary = (
            f"{blink_frequency_hz:g} Hz blink cycles "
            f"({state_change_rate:g} state changes/s)"
        )
        requested_summary = f"Requested blink period: {interval_blink:g} s"
    elif interval_seconds > 0:
        toggle_frequency_hz = 1.0 / interval_seconds
        blink_frequency_hz = 1.0 / (2.0 * interval_seconds)
        frequency_summary = (
            f"{blink_frequency_hz:g} Hz blink cycles "
            f"({toggle_frequency_hz:g} state changes/s)"
        )
        requested_summary = f"Per-batch delay: {interval_seconds:g} s"
    else:
        frequency_summary = "unbounded (no inter-frame delay)"
        requested_summary = "No delay requested"

    print("Statistics")
    print(f"  Port: {resolved_port}")
    print(f"  Pins: {format_pins(pins)}")
    print(f"  Pin count: {len(pins)}")
    print(f"  Blink cycles requested: {blink_count}")
    if interval_blink is not None:
        print(f"  Requested blink period: {interval_blink:g} s")
    else:
        print(f"  Per-batch delay: {interval_seconds:g} s")
    print(f"  Nominal blink frequency: {frequency_summary}")
    print(f"  Frames sent: {len(sent_frames)}")
    print(f"    Write frames: {write_frame_count}")
    print(f"  Expected responses: {statistics.expected_response_count}")
    print(f"  Received lines: {statistics.received_line_count}")
    print(f"  Parsed responses: {statistics.parsed_response_count}")
    print(f"  Success responses: {statistics.success_response_count}")
    print(f"  Error responses: {statistics.error_response_count}")
    print(f"  Malformed lines: {statistics.malformed_line_count}")
    print(f"  Debug responses: {statistics.debug_response_count}")
    print(f"  Missing responses: {statistics.missing_response_count}")
    print(f"  Duplicate responses: {statistics.duplicate_response_count}")
    print(f"  Unexpected responses: {statistics.unexpected_response_count}")
    print(f"  Validation failures: {statistics.validation_failure_count}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send alternating digital write frames to blink one or more pins together."
    )
    parser.add_argument(
        "port",
        nargs="?",
        default=None,
        help="Serial port name, for example COM5. If omitted, the script auto-detects the port.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="Host-side baud rate passed to pyserial. Teensy USB serial ignores it, but pyserial still requires a value.",
    )
    parser.add_argument(
        "--pin",
        type=int,
        nargs="+",
        default=[13],
        help="One or more physical pin numbers to blink together.",
    )
    parser.add_argument(
        "--blink-count",
        type=int,
        default=5,
        help="How many on/off blink cycles to send.",
    )
    parser.add_argument(
        "--interval-seconds",
        type=float,
        default=1.0,
        help="Delay between each write batch when --interval-blink is not set.",
    )
    parser.add_argument(
        "--interval-blink",
        type=float,
        default=None,
        help="Desired blink cycle period (seconds). The script will adjust the sleep time between high/low batches so the actual on/off timing approximates this period.",
    )
    parser.add_argument(
        "--wait-seconds",
        type=float,
        default=2.0,
        help="How long to wait for trailing responses after the blink sequence completes.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.25,
        help="Read and write timeout used by pyserial.",
    )
    return parser.parse_args()


class SerialLineReader(threading.Thread):
    def __init__(
        self,
        connection: serial.Serial,
        line_queue: queue.Queue[str],
        stop_event: threading.Event,
    ) -> None:
        super().__init__(daemon=True)
        self.connection = connection
        self.line_queue = line_queue
        self.stop_event = stop_event
        self.buffer = b""

    def run(self) -> None:
        while not self.stop_event.is_set():
            raw_line = self.connection.readline()
            if not raw_line:
                continue

            self.buffer += raw_line
            while b"\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\n", 1)
                line = line.rstrip(b"\r")
                if line:
                    self.line_queue.put(line.decode("ascii", errors="replace"))

        if self.buffer:
            line = self.buffer.rstrip(b"\r")
            if line:
                self.line_queue.put(line.decode("ascii", errors="replace"))


def collect_lines(connection: serial.Serial, wait_seconds: float) -> list[str]:
    deadline = time.monotonic() + wait_seconds
    received_lines: list[str] = []

    while time.monotonic() < deadline:
        raw_line = connection.readline()
        if not raw_line:
            continue

        received_lines.append(raw_line.decode("ascii", errors="replace").rstrip("\r\n"))

    return received_lines


def main() -> int:
    args = parse_args()
    try:
        pins = normalize_pins(args.pin)
    except ValueError as error:
        print(f"Invalid pin selection: {error}", file=sys.stderr)
        return 2

    blink_batches = build_blink_batches(pins, args.blink_count)
    test_frames = [frame for batch in blink_batches for frame in batch]
    resolved_port = resolve_port(args.port)

    send_duration = 0.0
    try:
        with serial.Serial(
            port=resolved_port,
            baudrate=args.baudrate,
            timeout=args.timeout,
            write_timeout=args.timeout,
        ) as connection:
            connection.reset_input_buffer()
            connection.reset_output_buffer()

            sent_frames: list[str] = []
            received_lines: list[str] = []
            response_queue: queue.Queue[str] = queue.Queue()
            stop_event = threading.Event()
            line_reader = SerialLineReader(connection, response_queue, stop_event)
            line_reader.start()

            send_start = time.monotonic()
            for batch_index, frame_batch in enumerate(blink_batches):
                batch_start = time.monotonic()
                for frame in frame_batch:
                    connection.write(frame.encode("ascii"))
                    connection.flush()
                    sent_frames.append(frame.rstrip())

                    while True:
                        try:
                            received_lines.append(response_queue.get_nowait())
                        except queue.Empty:
                            break

                batch_end = time.monotonic()
                is_write_batch = frame_batch[0].startswith("@W,")
                has_more_batches = batch_index + 1 < len(blink_batches)
                if is_write_batch and has_more_batches:
                    state_delay = (
                        args.interval_blink / 2.0
                        if args.interval_blink is not None
                        else args.interval_seconds
                    )
                    sleep_time = state_delay - (batch_end - batch_start)
                    if sleep_time > 0.0:
                        time.sleep(sleep_time)
            send_end = time.monotonic()
            send_duration = send_end - send_start

            print(
                f"Waiting {args.wait_seconds:g} seconds for trailing device responses..."
            )
            deadline = time.monotonic() + args.wait_seconds
            while time.monotonic() < deadline:
                try:
                    received_lines.append(response_queue.get(timeout=0.05))
                except queue.Empty:
                    continue

            stop_event.set()
            line_reader.join(timeout=1.0)
            while True:
                try:
                    received_lines.append(response_queue.get_nowait())
                except queue.Empty:
                    break
    except serial.SerialException as error:
        print(
            f"Failed to open or use serial port {resolved_port}: {error}",
            file=sys.stderr,
        )
        return 1

    print(
        f"Sent {len(sent_frames)} frames to blink pins {format_pins(pins)} "
        f"for {args.blink_count} cycle(s)"
    )
    print(f"Actual send duration: {send_duration:.3f} s")
    if args.blink_count > 0 and send_duration > 0.0:
        actual_blink_period = send_duration / args.blink_count
        actual_blink_frequency = args.blink_count / send_duration
        print(
            f"Actual blink period: {actual_blink_period:.6f} s/cycle "
            f"({actual_blink_frequency:.2f} Hz)"
        )
        print(
            f"Actual state-change rate: {len(sent_frames) / send_duration:.2f} changes/s"
        )

    validation_failures: list[str] = []
    statistics = ValidationStatistics(
        expected_response_count=len(sent_frames),
        received_line_count=len(received_lines),
        parsed_response_count=0,
        success_response_count=0,
        error_response_count=0,
        malformed_line_count=0,
        missing_response_count=len(sent_frames),
        duplicate_response_count=0,
        unexpected_response_count=0,
        debug_response_count=0,
        validation_failure_count=1 if not received_lines else 0,
    )

    if not received_lines:
        print("  <none>")
        print_statistics(
            resolved_port,
            pins,
            args.blink_count,
            args.interval_seconds,
            args.interval_blink,
            sent_frames,
            statistics,
        )
        print("Response validation failed: no responses received", file=sys.stderr)
        return 1

    validation_failures, statistics = validate_responses(sent_frames, received_lines)
    print_statistics(
        resolved_port,
        pins,
        args.blink_count,
        args.interval_seconds,
        args.interval_blink,
        sent_frames,
        statistics,
    )

    error_debug_lines = [
        line for line in received_lines
        if line.startswith("@E,") or line.startswith("@B,")
    ]
    if error_debug_lines:
        print("Error and debug frames:")
        for index, line in enumerate(error_debug_lines, start=1):
            print(f"  {index:02d}: {line}")

    if validation_failures:
        print("Response validation failed", file=sys.stderr)
        for failure in validation_failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"Validated {len(sent_frames)} success response(s)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
