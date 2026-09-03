#!/usr/bin/env python3
"""Passive stock-SLCAN capture for the USB2CANFDV2 instrument.

The USB instrument is intentionally treated as a byte-oriented SLCAN source,
not as a vehicle exporter.  This module has one command-writing path and that
path accepts only the commands required to configure and close a stock
instrument.  Received classic-CAN frame lines are copied byte-for-byte to a
native log; relative host-monotonic observations are written separately as
JSON lines.  There is no MCAN conversion and no transmit API.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Callable, IO, Optional, Sequence, Tuple, Union


class SlcanProtocolError(ValueError):
    """A command or received line is outside the supported SLCAN subset."""


class CanFdFrameError(SlcanProtocolError):
    """A CAN-FD line was offered to the classic-CAN v1 capture path."""


# These are the only command forms this module can emit.  S0-S8 are the
# standard LAWICEL nominal-bitrate presets; the capture CLI defaults to S6
# (500 kbit/s), matching the documented bring-up configuration.
COMMAND_ALLOWLIST = ("V", "C", "O", "M1", "A0")
SPEED_CODE_RE = re.compile(r"^S[0-8]$")
FRAME_PREFIXES = frozenset("tTrR")
CAN_FD_PREFIXES = frozenset("dDbB")
HEX_RE = re.compile(r"^[0-9A-Fa-f]+$")


def _is_stock_status_line(line: str) -> bool:
    """Identify status replies that are not native frame records.

    ``V`` is the stock version response and ``F`` is the stock error/status
    response.  A bare bell is the SLCAN command-error response.  They are not
    copied into the native frame log; every other non-frame line remains a
    protocol error so an unsupported CAN-FD/diagnostic stream cannot be
    silently reinterpreted.
    """

    return line == "\x07" or line in {"OK", "ERROR"} or line.startswith(("V", "F"))


def validate_command(command: str) -> str:
    """Validate and return one command from the tightly scoped allowlist.

    Terminators are deliberately rejected here.  ``send_command`` appends
    exactly one CR, preventing a caller from smuggling a second command or a
    frame command into a write.
    """

    if not isinstance(command, str) or not command or any(ord(char) < 0x20 for char in command):
        raise SlcanProtocolError("SLCAN commands must be one unterminated ASCII token")
    if command in COMMAND_ALLOWLIST or SPEED_CODE_RE.fullmatch(command):
        return command
    raise SlcanProtocolError(f"SLCAN command is not allowlisted: {command!r}")


def is_allowed_command(command: str) -> bool:
    """Return whether *command* can be emitted by this capture tool."""

    try:
        validate_command(command)
    except SlcanProtocolError:
        return False
    return True


def parse_classic_frame(line: str) -> Tuple[str, int, bool]:
    """Validate a native SLCAN classic frame and return (line, dlc, rtr).

    SLCAN's native text is retained instead of being converted into MCAN v1.
    Incoming IDs/data may use either hex case because the instrument is the
    source of these bytes.  The returned line is unchanged.
    """

    if not line:
        raise SlcanProtocolError("empty SLCAN line")
    prefix = line[0]
    if prefix in CAN_FD_PREFIXES:
        raise CanFdFrameError("CAN-FD SLCAN frames are excluded from v1 capture")
    if prefix not in FRAME_PREFIXES:
        raise SlcanProtocolError(f"unsupported SLCAN record: {prefix!r}")

    extended = prefix in "TR"
    remote = prefix in "rR"
    id_width = 8 if extended else 3
    if len(line) < 1 + id_width + 1:
        raise SlcanProtocolError("SLCAN frame is truncated")
    identifier = line[1 : 1 + id_width]
    dlc_text = line[1 + id_width]
    if not HEX_RE.fullmatch(identifier):
        raise SlcanProtocolError("SLCAN identifier is not hexadecimal")
    max_identifier = 0x1FFFFFFF if extended else 0x7FF
    if int(identifier, 16) > max_identifier:
        raise SlcanProtocolError("SLCAN identifier exceeds selected frame format")
    if dlc_text not in "012345678":
        raise SlcanProtocolError("classic-CAN SLCAN DLC must be 0 through 8")
    dlc = int(dlc_text)
    payload = line[2 + id_width :]
    if remote and payload:
        raise SlcanProtocolError("SLCAN remote frames must not contain payload")
    if not remote and (len(payload) != dlc * 2 or not (not payload or HEX_RE.fullmatch(payload))):
        raise SlcanProtocolError("SLCAN data length does not match DLC")
    return line, dlc, remote


def _strip_line_ending(raw: bytes) -> bytes:
    """Remove serial line framing without changing any frame characters."""

    return raw[:-2] if raw.endswith(b"\r\n") else raw[:-1] if raw.endswith((b"\r", b"\n")) else raw


class SlcanTransport:
    """The sole command-writing boundary for the stock SLCAN device."""

    def __init__(self, serial_port: Any) -> None:
        self.serial = serial_port
        self.emitted_commands = []
        self._closed = False

    def send_command(self, command: str) -> None:
        command = validate_command(command)
        # ASCII encoding is intentional: all allowlisted commands are ASCII.
        self.serial.write((command + "\r").encode("ascii"))
        self.emitted_commands.append(command)

    def configure_vehicle_capture(self, speed_code: str = "6") -> None:
        """Issue the fixed stock-SLCAN setup sequence.

        ``C`` first closes a pre-existing session, while the ``C`` sent by
        ``close`` is the required deterministic capture shutdown.
        """

        if not re.fullmatch(r"[0-8]", speed_code):
            raise SlcanProtocolError("speed code must be one digit from 0 through 8")
        for command in ("V", "C", f"S{speed_code}", "M1", "A0", "O"):
            self.send_command(command)

    def close(self) -> None:
        """Close the stock-SLCAN capture exactly once, then close the port."""

        if self._closed:
            return
        self._closed = True
        try:
            self.send_command("C")
        finally:
            close = getattr(self.serial, "close", None)
            if close is not None:
                close()


def _open_serial(port: str, baudrate: int, timeout: float) -> Any:
    try:
        import serial  # type: ignore
    except ImportError as error:  # pragma: no cover - exercised by CLI users
        raise RuntimeError("pyserial is required; install it with 'python3 -m pip install pyserial'") from error
    return serial.Serial(port=port, baudrate=baudrate, timeout=timeout)


def capture(
    transport: SlcanTransport,
    native_output: Union[Path, str, IO[str]],
    sidecar_output: Union[Path, str, IO[str]],
    *,
    duration_s: Optional[float] = 10.0,
    max_frames: Optional[int] = None,
    clock_us: Callable[[], int] = lambda: time.monotonic_ns() // 1000,
    speed_code: str = "6",
) -> int:
    """Capture native classic-CAN frames and relative host sidecar records.

    ``duration_s=None`` requires ``max_frames``.  A zero-duration capture is
    valid and still performs setup followed by the required ``C`` shutdown.
    The serial object must provide pyserial's ``readline()``, ``write()``, and
    optionally ``close()`` methods.  Tests can therefore use a deterministic
    in-memory serial double without installing pyserial.
    """

    if duration_s is not None and duration_s < 0:
        raise ValueError("duration_s must be non-negative or None")
    if max_frames is not None and max_frames < 0:
        raise ValueError("max_frames must be non-negative")
    if duration_s is None and max_frames is None:
        raise ValueError("duration_s=None requires max_frames")

    opened: list[IO[str]] = []

    def open_output(target: Union[Path, str, IO[str]]) -> IO[str]:
        if hasattr(target, "write"):
            return target  # type: ignore[return-value]
        handle = Path(target).open("w", encoding="ascii", newline="\n")
        opened.append(handle)
        return handle

    native = open_output(native_output)
    sidecar = open_output(sidecar_output)
    start_us = clock_us()
    frames = 0
    native_line = 0
    sidecar.write(
        json.dumps(
            {
                "schema": "mcan-usb-slcan-sidecar",
                "version": 1,
                "clock": "host-monotonic-relative",
                "clock_unit": "us",
                "source": "usb2canfdv2-stock-slcan",
            },
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )

    deadline_us = None if duration_s is None else start_us + int(duration_s * 1_000_000)
    try:
        transport.configure_vehicle_capture(speed_code)
        while max_frames is None or frames < max_frames:
            now_us = clock_us()
            if deadline_us is not None and now_us >= deadline_us:
                break
            raw = transport.serial.readline()
            if not raw:
                # pyserial's timeout allows a bounded poll so duration remains
                # deterministic and shutdown is reachable even on an idle bus.
                continue
            raw_line = _strip_line_ending(bytes(raw))
            try:
                line = raw_line.decode("ascii")
            except UnicodeDecodeError as error:
                raise SlcanProtocolError("SLCAN input is not ASCII") from error
            if not line:
                continue
            if _is_stock_status_line(line):
                continue
            parse_classic_frame(line)
            native_line += 1
            native.write(line + "\n")
            sidecar.write(
                json.dumps(
                    {
                        "capture_line": native_line,
                        "host_monotonic_us": max(0, clock_us() - start_us),
                        "record": "FRAME",
                    },
                    sort_keys=True,
                    separators=(",", ":"),
                )
                + "\n"
            )
            frames += 1
    finally:
        # This is deliberately in the finally block, including malformed-input
        # and output-error paths.  No best-effort transmit or recovery command
        # exists in this module.
        transport.close()
        native.flush()
        sidecar.flush()
        for handle in opened:
            handle.close()
    return frames


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="serial port for the stock USB2CANFDV2")
    parser.add_argument("--output", type=Path, required=True, help="native SLCAN output path")
    parser.add_argument("--sidecar", type=Path, required=True, help="relative host-monotonic JSONL sidecar path")
    parser.add_argument("--speed-code", default="6", help="stock SLCAN S0-S8 nominal bitrate code (default: S6)")
    parser.add_argument("--duration", type=float, default=10.0, help="capture duration in seconds (default: 10)")
    parser.add_argument("--max-frames", type=int, help="stop after this many frames")
    parser.add_argument("--baudrate", type=int, default=115200, help="serial line speed (default: 115200)")
    parser.add_argument("--serial-timeout", type=float, default=0.1, help="serial read timeout in seconds")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.duration is None or args.duration < 0:
        print("error: --duration must be non-negative", file=sys.stderr)
        return 2
    try:
        serial_port = _open_serial(args.port, args.baudrate, args.serial_timeout)
        frames = capture(
            SlcanTransport(serial_port),
            args.output,
            args.sidecar,
            duration_s=args.duration,
            max_frames=args.max_frames,
            speed_code=args.speed_code,
        )
    except (OSError, RuntimeError, SlcanProtocolError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"captured {frames} classic-CAN frame(s); native log is auxiliary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
