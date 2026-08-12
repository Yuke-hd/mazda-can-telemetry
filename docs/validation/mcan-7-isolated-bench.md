# MCAN-7 isolated CAN bench procedure and evidence record

## Status

**Overall: NOT EXECUTED / PENDING HARDWARE**

This file is a procedure and blank evidence record. It is not proof of frame
reception or strict listen-only electrical behavior. Do not mark an item PASS
from source inspection or ESP-IDF documentation. Enter measured results only
while performing the test on the real isolated bench.

## Purpose and acceptance

The procedure must establish both of the hardware-dependent MCAN-7 criteria:

1. a real standard, extended, data, and RTR frame reaches the public receive
   boundary with identifier, flags, DLC, payload, bus, and monotonic timestamp
   intact; and
2. the T-CAN485 does not drive the ACK slot in strict listen-only mode.

No vehicle is used. Active transmission is allowed only from the isolated bench
generator. A second normal CAN node is required for the successful-reception
part because a strict listener does not ACK.

## Required equipment

- T-CAN485 running the exact firmware commit under test;
- isolated and protected supply within the board's documented input range;
- verified CAN generator/transceiver at the selected bitrate;
- independent normal CAN node/transceiver for ACK during receive tests;
- two 120 ohm end terminators on a two-ended bench bus, with approximately
  60 ohms measured between CAN-H and CAN-L while powered off;
- logic analyzer or oscilloscope able to resolve the ACK slot at the selected
  bitrate;
- serial console for startup and received-frame/statistics output.

The owner-provided MCP2515/TJA1050 module at 3.3 V is an accepted supervised
experiment only. If its physical layer is unstable, stop and use the documented
5 V Arduino fallback with safe logic levels or a native 3.3 V CAN module. Never
connect that experimental arrangement to a vehicle.

## Preflight record

| Field | Required record | Result |
| --- | --- | --- |
| Date/time and operator | Local test record | PENDING |
| Firmware commit | Full Git commit | PENDING |
| ESP-IDF version | Must be v5.5.4 | PENDING |
| T-CAN485 PCB/schematic revision | Photograph/reference | PENDING |
| Generator and ACK-node hardware | Exact models and transceivers | PENDING |
| Bitrate | One explicitly allowed value | PENDING |
| CAN-H to CAN-L resistance, power off | Measured ohms | PENDING |
| Supply voltage/current limit | Measured/configured | PENDING |
| CAN TX/RX/speed/boost pins | Continuity or revision evidence | PENDING |
| Analyzer model/sample rate | Exact setup | PENDING |

Stop if the topology, termination, supply, revision, or pin mapping is not
verified. Do not improvise on a vehicle network.

## Build and flash commands

Run from an ESP-IDF v5.5.4 PowerShell environment. Record the actual port and
complete command results; do not paste credentials or private data.

```powershell
idf.py --version
idf.py -C firmware/tcan485 fullclean
idf.py -C firmware/tcan485 set-target esp32
idf.py -C firmware/tcan485 build
idf.py -C firmware/tcan485 -p COM_PORT flash monitor
```

Expected startup evidence includes the exact phrases `STRICT LISTEN-ONLY`,
`TX queue disabled`, and `strict listen-only CAN acquisition started`. Record
the serial lines and whether startup refused invalid configuration. A source
log without hardware execution is not bench evidence.

## Receive-fidelity procedure

1. Power off and assemble the isolated, correctly terminated bus with the
   generator, independent normal ACK node, and T-CAN485.
2. Configure every node to the recorded allowed bitrate.
3. Start the T-CAN485 and confirm the strict listen-only startup log.
4. From the bench generator, send at least these named test cases at a low,
   recorded rate:

   | Case | Format | ID | RTR | DLC | Payload |
   | --- | --- | ---: | --- | ---: | --- |
   | STD-DATA | standard | `0x123` | no | 8 | `00 11 22 33 44 55 66 77` |
   | EXT-DATA | extended | `0x1ABCDE0` | no | 3 | `A5 5A C3` |
   | STD-RTR | standard | `0x456` | yes | 4 | no data bytes on bus |

5. Record the generator command or Web UI action exactly. Record the decoded
   public `RawCanFrame` fields and successive monotonic timestamps.
6. Verify FIFO order, increasing timestamps, bus ID, exact identifiers,
   standard/extended and RTR flags, DLCs, and data-frame payload bytes.
7. Remove or pause the consumer while sending more than 64 frames. Confirm the
   generator remains independent of the consumer, the application queue depth
   never exceeds 64, and drop/overflow counters rise. Restore the consumer and
   verify existing FIFO frames precede later accepted frames.

### Receive-fidelity evidence

| Field | Result |
| --- | --- |
| Generator commands/actions | NOT EXECUTED |
| STD-DATA observed record | NOT EXECUTED |
| EXT-DATA observed record | NOT EXECUTED |
| STD-RTR observed record | NOT EXECUTED |
| Timestamp monotonicity | NOT EXECUTED |
| Slow/absent consumer counts | NOT EXECUTED |
| Statistics before/after reset | NOT EXECUTED |
| Outcome and anomaly notes | PENDING |

## No-ACK procedure

1. Keep the analyzer connected and use one generator only. Disconnect or power
   down the independent normal ACK node so the T-CAN485 is the sole possible
   receiver. Confirm the bench remains correctly terminated.
2. Trigger exactly one standard data-frame attempt from the generator. Capture
   the complete frame including ACK slot and the generator's resulting retry or
   ACK-error indication.
3. Confirm the ACK slot remains recessive. The generator should report a
   missing ACK and may retry according to its controller configuration. The
   T-CAN485 may still receive the valid frame; duplicate receives caused by
   generator retries must be noted.
4. Power down the T-CAN485 and repeat the same one-frame attempt as a control.
   Record the ACK slot and generator result.
5. Reconnect the independent normal ACK node and repeat. Confirm that the ACK
   slot becomes dominant and the generator reports success. This positive
   control validates that the measurement can distinguish an ACK.
6. Repeat the comparison for at least ten attempts per condition. Preserve
   analyzer screenshots or files in private test records; publish only reviewed,
   non-sensitive evidence. Never attach raw vehicle captures (none should exist
   in this isolated test).

### No-ACK measurements

| Condition | Attempts | Dominant ACK slots | Generator ACK errors | Result |
| --- | ---: | ---: | ---: | --- |
| T-CAN485 only | NOT EXECUTED | NOT EXECUTED | NOT EXECUTED | PENDING |
| No receiver control | NOT EXECUTED | NOT EXECUTED | NOT EXECUTED | PENDING |
| Normal ACK node connected | NOT EXECUTED | NOT EXECUTED | NOT EXECUTED | PENDING |

Acceptance requires zero dominant ACK slots attributable to the T-CAN485-only
condition and a dominant ACK in the positive-control condition. If the result
is ambiguous or any dominant bit is attributable to the T-CAN485, mark FAIL,
disconnect the board, and do not proceed to a vehicle.

## Final disposition

| Criterion | Status |
| --- | --- |
| Physical frame fidelity | PENDING |
| Monotonic timestamp behavior | PENDING |
| Slow/absent consumer behavior | PENDING |
| Queue/statistics observations | PENDING |
| Strict listener emits no ACK | PENDING |
| Vehicle connection authorized by this record | NO |

Final notes: **NOT EXECUTED / PENDING HARDWARE**.
