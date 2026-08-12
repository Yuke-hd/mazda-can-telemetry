# MCAN-8 output-only USB raw-frame exporter

## Boundaries and ownership

`raw_capture::Exporter` is a portable C++17 component between the MCAN-7
receive API and a write-only output sink. One owner task calls its bounded input
phase and then its bounded output phase. The input phase calls
`can_bus::receive(frame, 0)`, copies each value into a second fixed 64-frame
queue, and immediately returns to the receive boundary. A full exporter queue
drops the newest copy and aggregates an explicit `DROP` record. The output
phase may be slow, absent, disconnected, or temporarily unable to accept bytes
without holding the CAN queue or waiting in the receive phase.

The ESP32 target uses UART0's USB-UART bridge and only the documented
non-blocking `uart_tx_chars()` FIFO API. A complete line is retained until all
bytes are accepted; a partial FIFO write returns `kWouldBlock` and the next
output poll resumes at the saved offset. The sink has no receive method and the
firmware does not install or call a UART receive/command parser. Consequently,
SLCAN/LAWICEL commands and any serial-to-CAN path are structurally absent.

The classic ESP32 USB-UART bridge does not expose a portable host-presence
signal through the UART API. The software boundary therefore treats a full or
empty UART FIFO as backpressure, keeps receiving into the bounded copy queue,
and records queue loss. Host fakes test the sink boundary's disconnect/reconnect
transitions and the resulting `DISCONTINUITY reason=usb-reconnect`; actual USB
unplug/replug detection is not claimed because UART0 provides no host-presence
signal. MCAN-8's physical sustained-rate
acceptance criterion is pending and blocks a complete acceptance claim until
measured on target hardware. The physical no-ACK and wiring evidence is owned
by MCAN-33, as specified by MCAN-7.

UART0 is reserved for capture after startup. ESP-IDF logging is disabled before
the first capture poll so log bytes cannot interleave with a partially accepted
line. There is no UART input path.

## Diagnostics and target

Frame records are never rate-limited by diagnostics. `DROP` and
`DISCONTINUITY` records are emitted at the next available output
slot after their preceding frame boundary (and are never held behind later
frames); `STATS` is emitted at the same default cadence and a requested final checkpoint
is emitted before orderly shutdown. A drop counter is cumulative from the
session baseline and every omitted frame is represented by a pending DROP
record; an output error additionally increments `dropped_records`. Loss markers
are ordered against the timestamp and segment of queued frames, so a marker is
not allowed to make a later frame appear before an earlier discontinuity.

The software target is to sustain 64 queued frame copies plus the USB-UART FIFO
without blocking CAN receive; under a sink that accepts bytes, host tests
verify lossless serialization of every queued frame. If the host is slower
than the configured classic-CAN stream, output drops are expected and are
reported as `DROP` plus cumulative `STATS`, never presented as a continuous
capture. A physical sustained-rate/no-drop measurement is intentionally not
claimed here and requires a dedicated MCAN-8 sustained-rate bench run; MCAN-33
does not replace that acceptance criterion.

## Validation

`tests/host/raw_capture_tests.cpp` uses deterministic source and sink fakes to
check the normative v1 records byte-for-byte, frame identity and payload
formatting, slow/disconnected output, bounded input draining, explicit drops,
and reconnect segment markers. No real vehicle capture or sensitive data is
included.
