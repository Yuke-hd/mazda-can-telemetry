"""Synthetic safety and capture tests for the USB2CANFDV2 SLCAN tool."""

from __future__ import annotations

import io
import json
import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))
import slcan_capture  # noqa: E402


class FakeSerial:
    def __init__(self, reads: list[bytes]) -> None:
        self.reads = list(reads)
        self.writes: list[bytes] = []
        self.closed = False

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def readline(self) -> bytes:
        return self.reads.pop(0) if self.reads else b""

    def close(self) -> None:
        self.closed = True


class SlcanCaptureTests(unittest.TestCase):
    def test_allowlist_accepts_only_required_commands(self) -> None:
        for command in ("V", "C", "S0", "S6", "S8", "M1", "A0", "O"):
            self.assertTrue(slcan_capture.is_allowed_command(command), command)
        for command in ("", "L", "m1", "S", "S9", "t1231AA", "T000001231AA", "r1231", "R000001231", "V\r"):
            self.assertFalse(slcan_capture.is_allowed_command(command), command)

    def test_disallowed_frame_command_is_never_emitted(self) -> None:
        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        with self.assertRaises(slcan_capture.SlcanProtocolError):
            transport.send_command("t1231AA")
        self.assertEqual(serial_port.writes, [])
        self.assertEqual(transport.emitted_commands, [])

    def test_isolated_m1_bench_smoke_attempts_frame_without_a_frame(self) -> None:
        """The M1 smoke path must reject a frame command before serial write."""

        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        transport.send_command("M1")
        with self.assertRaises(slcan_capture.SlcanProtocolError):
            transport.send_command("t1230")
        self.assertEqual(serial_port.writes, [b"M1\r"])
        self.assertNotIn(b"t1230\r", serial_port.writes)

    def test_configuration_and_capture_close_use_only_allowlist(self) -> None:
        serial_port = FakeSerial([b"t1231AA\r\n", b"T000001230\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        native = io.StringIO()
        sidecar = io.StringIO()
        ticks = iter([0, 0, 10, 20, 30, 40])
        frames = slcan_capture.capture(
            transport,
            native,
            sidecar,
            duration_s=None,
            max_frames=2,
            clock_us=lambda: next(ticks),
        )
        self.assertEqual(frames, 2)
        self.assertEqual(native.getvalue(), "t1231AA\nT000001230\n")
        self.assertEqual(
            transport.emitted_commands,
            ["V", "C", "S6", "M1", "A0", "O", "C"],
        )
        self.assertEqual(serial_port.writes[-1], b"C\r")
        records = [json.loads(line) for line in sidecar.getvalue().splitlines()]
        self.assertEqual(records[0]["clock"], "host-monotonic-relative")
        self.assertEqual([record["host_monotonic_us"] for record in records[1:]], [10, 30])
        self.assertNotIn("timestamp", sidecar.getvalue())
        self.assertNotIn("drop", sidecar.getvalue().lower())

    def test_shutdown_command_is_sent_after_malformed_or_fd_input(self) -> None:
        for line in (b"d1230\r", b"t1239AA\r"):
            serial_port = FakeSerial([line])
            transport = slcan_capture.SlcanTransport(serial_port)
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
            self.assertEqual(serial_port.writes[-1], b"C\r")
            self.assertNotIn(b"t", b"".join(serial_port.writes))

    def test_stock_version_and_status_replies_are_not_capture_frames(self) -> None:
        serial_port = FakeSerial([b"V0101\r", b"F00\r", b"\x07", b"t1230\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        native = io.StringIO()
        sidecar = io.StringIO()
        frames = slcan_capture.capture(
            transport, native, sidecar, duration_s=None, max_frames=1, clock_us=lambda: 0
        )
        self.assertEqual(frames, 1)
        self.assertEqual(native.getvalue(), "t1230\n")

    def test_classic_parser_rejects_fd_and_malformed_lines(self) -> None:
        with self.assertRaises(slcan_capture.CanFdFrameError):
            slcan_capture.parse_classic_frame("d1230")
        for line in ("t1239AA", "t1231A", "T2000000010", "r1231AA", "x1230"):
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.parse_classic_frame(line)


if __name__ == "__main__":
    unittest.main()
