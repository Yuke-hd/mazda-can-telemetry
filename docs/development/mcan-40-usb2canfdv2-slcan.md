# MCAN-40 USB2CANFDV2 stock SLCAN capture

Status: host tooling and safety procedure. The USB2CANFDV2 remains a stock-
firmware USB-CAN instrument for passive monitoring and isolated-bench analysis.
It is not the product exporter. The WeAct CAN485 V1.1 T-CAN485 remains the only
canonical source of v1 vehicle capture.

## Host command boundary

[`tools/slcan_capture.py`](../../tools/slcan_capture.py) is the repository
capture path. Its only serial command writer accepts this exact allowlist:

| Command | Purpose |
| --- | --- |
| `V` | Read stock firmware/version identity. |
| `C` | Close an existing session and deterministically shut down capture. |
| `S0`–`S8` | Select one stock SLCAN nominal-bitrate preset (`S6` is 500 kbit/s). |
| `M1` | Apply the approved stock capture acceptance setting. |
| `A0` | Apply the approved stock capture acceptance setting. |
| `O` | Open the capture session. |

The setup sequence is `V`, `C`, `Sx`, `M1`, `A0`, `O`; a final `C` is sent in a
`finally` path on normal completion, malformed input, and output failure. A
caller cannot include a line terminator in a command, and the writer has no
frame/transmit method. In particular, `t`, `T`, `r`, and `R` data/RTR commands,
`L`, `m`, `l`, and all other configuration or transmit commands are rejected
before any serial write.

The `V` response must be recorded by the operator in the private run worksheet
along with the instrument model, stock firmware version, USB identity, and
configuration. Do not publish device serial numbers, MAC addresses, hostnames,
or private USB/log metadata.

## Native capture and sidecar

The tool copies received classic-CAN SLCAN frame lines unchanged into the
native output (`.slcan`). The parser accepts standard/extended data and remote
frames with DLC 0–8 and rejects malformed lines. CAN-FD SLCAN prefixes (`d`,
`D`, `b`, `B`) and all non-frame records are rejected; a rejected input still
causes the required `C` shutdown. Native USB logs are auxiliary diagnostics and
evidence. They are not MCAN v1 records, and the tool never fabricates device
timestamps, device drop counters, or MCAN conversion fields.

The JSONL sidecar has a schema header followed by one observation per native
frame. Its `host_monotonic_us` values are relative to the first observation and
are intentionally not wall-clock timestamps:

```text
{"clock":"host-monotonic-relative","clock_unit":"us","schema":"mcan-usb-slcan-sidecar","source":"usb2canfdv2-stock-slcan","version":1}
{"capture_line":1,"host_monotonic_us":421,"record":"FRAME"}
```

The sidecar does not add a device timestamp, drop counter, VIN, location, or
trip identity. Raw vehicle output and sidecars remain private and must never be
committed or attached to an Issue/PR. Synthetic inputs only are used by the
host tests.

Example (isolated, supervised host run):

```text
python3 tools/slcan_capture.py --port /dev/tty.usbserial-REVIEWED-BENCH --output run.slcan --sidecar run.sidecar.jsonl --speed-code 6 --duration 30
```

The port above is a placeholder, not a repository configuration. Confirm it
from the private bench worksheet before use.

## SavvyCAN and vehicle arrangement

SavvyCAN may be used only as a sanitized live bench/offline visualization,
replay, or inspection tool. It is not a vehicle exporter and must not transmit
on a vehicle bus. Disable transmit/replay controls before opening any session;
active/replay functions are permitted only on a physically isolated bench with
a reviewed synthetic fixture. Do not use SavvyCAN to create canonical v1
captures.

When the USB2CANFDV2 is connected for vehicle monitoring, K2 **MUST REMAIN
OFF**. The monitor arrangement must be independently reviewed as passive before
power is applied. The USB instrument's ability to occupy the CAN ACK slot has
not been measured; stock SLCAN configuration must not be described as
listen-only or no-ACK evidence. Only the WeAct CAN485 V1.1 strict listen-only
path has that product-side role, subject to its documented hardware evidence.

## Isolated M1 smoke test

The host smoke test attempts the shape of a standard data-frame command after
`M1` and verifies that no frame command reaches the serial port. It uses a
synthetic serial double; it is not vehicle evidence. A physical follow-up, if
needed, must use a two-node isolated classic-CAN bench, current-limited power,
reviewed termination, and a fresh run label. Do not connect this instrument to
a vehicle for an active command/no-frame experiment.

```text
python3 -m unittest tests/tools/slcan_capture_test.py
```

The residual ACK-slot question remains open validation work. Until it is
measured under a separately reviewed bench procedure, USB2CANFDV2 captures are
auxiliary only and must not be used to claim vehicle-safe listen-only behavior.

## Evidence and exclusions

| Item | Status |
| --- | --- |
| USB2CANFDV2 stock firmware/version and USB identity | Record privately from `V`; no public serial/MAC data. |
| Approved setup and deterministic `C` shutdown | Enforced by `slcan_capture.py` and host tests. |
| Native SLCAN + relative host sidecar | Enforced by tool and synthetic tests. |
| Classic CAN only | Enforced by parser; CAN-FD is rejected. |
| K2 OFF and independently passive vehicle arrangement | Required review gate; no vehicle run performed here. |
| SavvyCAN sanitized bench/offline use | Required procedure; no vehicle transmit permitted. |
| USB2CANFDV2 ACK-slot behavior | **NOT MEASURED / OPEN VALIDATION WORK**. |
| Canonical v1 vehicle capture | WeAct CAN485 V1.1 only. |

