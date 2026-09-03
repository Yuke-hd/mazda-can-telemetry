"""Synthetic safety and capture tests for the USB2CANFDV2 SLCAN tool."""

from __future__ import annotations

import io
import json
import sys
import tempfile
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

    def read(self, size: int = 1) -> bytes:
        if not self.reads:
            return b""
        chunk = self.reads[0][:size]
        self.reads[0] = self.reads[0][size:]
        if not self.reads[0]:
            self.reads.pop(0)
        return chunk

    def close(self) -> None:
        self.closed = True


class CloseWriteErrorSerial(FakeSerial):
    def write(self, data: bytes) -> int:
        if data == b"C\r" and b"O\r" in self.writes:
            self.writes.append(data)
            raise OSError("synthetic close write failure")
        return super().write(data)


class FailingOutput(io.StringIO):
    def __init__(self, *, fail_write: bool = False, fail_flush: bool = False) -> None:
        super().__init__()
        self.fail_write = fail_write
        self.fail_flush = fail_flush
        self.flush_called = False

    def write(self, data: str) -> int:
        if self.fail_write:
            raise OSError("synthetic output write failure")
        return super().write(data)

    def flush(self) -> None:
        self.flush_called = True
        if self.fail_flush:
            raise OSError("synthetic output flush failure")
        super().flush()


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
        serial_port = FakeSerial(
            [
                b"WeAct Studio V1.0.0.0\r\r\r\r\r\r",
                b"t1231AA\rT000001230\r",
            ]
        )
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
            serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r\r\r\r", line])
            transport = slcan_capture.SlcanTransport(serial_port)
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
            self.assertEqual(serial_port.writes[-1], b"C\r")
            self.assertNotIn(b"t", b"".join(serial_port.writes))

    def test_stock_version_and_status_replies_are_not_capture_frames(self) -> None:
        serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r\r\r\r", b"t1230\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        native = io.StringIO()
        sidecar = io.StringIO()
        frames = slcan_capture.capture(
            transport, native, sidecar, duration_s=None, max_frames=1, clock_us=lambda: 0
        )
        self.assertEqual(frames, 1)
        self.assertEqual(native.getvalue(), "t1230\n")

    def test_only_stock_version_response_is_ignored(self) -> None:
        serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r", b"W1.0.0.0\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        with self.assertRaises(slcan_capture.SlcanProtocolError):
            slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
        self.assertEqual(serial_port.writes[-1], b"C\r")

        for line in (b"W1.0.0.0\r", b"WeAct Studio V1.0.0.0 extra\r", b"WeAct Studio V1.0.0\r"):
            serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r", line])
            transport = slcan_capture.SlcanTransport(serial_port)
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
            self.assertEqual(serial_port.writes[-1], b"C\r")

    def test_invalid_arguments_still_attempt_close(self) -> None:
        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        with self.assertRaises(ValueError):
            slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=-1)
        self.assertEqual(serial_port.writes, [b"C\r"])
        self.assertTrue(serial_port.closed)

    def test_output_open_failure_still_attempts_close(self) -> None:
        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        with tempfile.TemporaryDirectory() as directory:
            missing_path = Path(directory) / "no-such-directory" / "output.slcan"
            with self.assertRaises(OSError):
                slcan_capture.capture(transport, missing_path, io.StringIO(), duration_s=0)
        self.assertEqual(serial_port.writes, [b"C\r"])
        self.assertTrue(serial_port.closed)

    def test_second_output_open_failure_closes_first_output(self) -> None:
        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        with tempfile.TemporaryDirectory() as directory:
            native_path = Path(directory) / "native.slcan"
            missing_sidecar = Path(directory) / "no-such-directory" / "sidecar.jsonl"
            with self.assertRaises(OSError):
                slcan_capture.capture(transport, native_path, missing_sidecar, duration_s=0)
            # A closed path output can be reopened immediately, proving cleanup
            # was attempted after the second output failed to open.
            native_path.open("a", encoding="ascii").close()
        self.assertEqual(serial_port.writes, [b"C\r"])
        self.assertTrue(serial_port.closed)

    def test_header_write_failure_still_attempts_close(self) -> None:
        serial_port = FakeSerial([])
        transport = slcan_capture.SlcanTransport(serial_port)
        with self.assertRaises(OSError):
            slcan_capture.capture(transport, io.StringIO(), FailingOutput(fail_write=True), duration_s=0)
        self.assertEqual(serial_port.writes, [b"C\r"])
        self.assertTrue(serial_port.closed)

    def test_flush_failure_does_not_skip_close_attempt(self) -> None:
        serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r\r\r\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        native = FailingOutput(fail_flush=True)
        sidecar = FailingOutput(fail_flush=True)
        with self.assertRaises(OSError):
            slcan_capture.capture(transport, native, sidecar, duration_s=0)
        self.assertEqual(serial_port.writes[-1], b"C\r")
        self.assertTrue(serial_port.closed)
        self.assertTrue(native.flush_called)
        self.assertTrue(sidecar.flush_called)

    def test_close_write_failure_still_flushes_outputs_and_closes_port(self) -> None:
        serial_port = CloseWriteErrorSerial([b"WeAct Studio V1.0.0.0\r\r\r\r\r\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        native = io.StringIO()
        sidecar = io.StringIO()
        with self.assertRaises(OSError):
            slcan_capture.capture(transport, native, sidecar, duration_s=0)
        self.assertEqual(serial_port.writes[-1], b"C\r")
        self.assertTrue(serial_port.closed)

    def test_m1_failure_does_not_emit_a0_or_open(self) -> None:
        serial_port = FakeSerial([b"WeAct Studio V1.0.0.0\r\r\r\x07\r"])
        transport = slcan_capture.SlcanTransport(serial_port)
        with self.assertRaises(slcan_capture.SlcanProtocolError):
            slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
        self.assertEqual(serial_port.writes, [b"V\r", b"C\r", b"S6\r", b"M1\r", b"C\r"])
        self.assertNotIn(b"A0\r", serial_port.writes)
        self.assertNotIn(b"O\r", serial_port.writes)

    def test_cr_framing_rejects_lf_and_partial_reply(self) -> None:
        for reply in (b"\r\n", b"WeAct Studio V1.0.0.0"):
            serial_port = FakeSerial([reply])
            transport = slcan_capture.SlcanTransport(serial_port)
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.capture(transport, io.StringIO(), io.StringIO(), duration_s=None, max_frames=1)
            self.assertEqual(serial_port.writes[-1], b"C\r")

    def test_classic_parser_rejects_fd_and_malformed_lines(self) -> None:
        with self.assertRaises(slcan_capture.CanFdFrameError):
            slcan_capture.parse_classic_frame("d1230")
        for line in ("t1239AA", "t1231A", "T2000000010", "r1231AA", "x1230"):
            with self.assertRaises(slcan_capture.SlcanProtocolError):
                slcan_capture.parse_classic_frame(line)


if __name__ == "__main__":
    unittest.main()
