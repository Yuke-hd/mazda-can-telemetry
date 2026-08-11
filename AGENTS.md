# Mazda CAN Telemetry — Project Conventions and Plan

> Document status: v0.11 draft (2026-08-11)
>
> Maintenance rule: decisions reached during discussion must be added to this document immediately. Unconfirmed items must remain explicitly marked as recommendations or pending decisions and must not be implemented as established facts.
>
> This file is named `AGENTS.md` so Codex and other development agents discover it automatically.

## 1. Project Goal

Use an ESP32, initially a LILYGO/TTGO T-CAN485, to connect to the CAN network of a 2019 Mazda CX-5 KF and safely and reliably collect vehicle data. The project will progressively identify and export signals including, but not limited to:

- vehicle speed;
- engine speed;
- selector and transmission gear state;
- turn indicators, high/low beam, fog lights, and other indicator states;
- doors, parking brake, seat belts, and other body states;
- any other signals that are later verified as usable.

The project should ultimately evolve from a development-board prototype into dedicated hardware suitable for long-term installation in a vehicle.

The confirmed final product direction is to **provide data to an independent real-time dashboard and an ambient-light frontend driven by vehicle state**. For example, this project detects left turn, right turn, or hazard state and exports a normalized event; an independent frontend then plays the corresponding ARGB animation. Dashboard UI, animation rendering, and LED-strip driving do not belong to the vehicle-acquisition core, and no frontend may influence the vehicle network in the reverse direction.

## 2. Current Scope

### 2.1 Recommended first deliverable

The first MVP is a **strictly receive-only CAN acquisition device with real-time telemetry output**:

1. Receive raw CAN frames in listen-only mode and record them with monotonic timestamps.
2. Export raw frames over USB serial, with optional microSD recording.
3. Use recorded data for offline analysis, replay, and decoder development on a computer.
4. Reliably decode at least vehicle speed, engine speed, gear, left/right turn request, and hazard state.
5. Produce a normalized vehicle state with time, validity, and freshness information.
6. Export those states and events through a versioned internal API decoupled from CAN IDs.
7. During development and decoder validation, let the T-CAN485 drive its single onboard GPIO4 WS2812B as a local reference consumer of the core library. An external strip of approximately eight LEDs is deferred to the effects-acceptance phase; the independent S2 Mini frontend and inter-device telemetry begin in the next version.
8. Degrade safely on vehicle power loss, bus sleep, communication loss, stale data, or storage failure.

The dashboard and ARGB effects are confirmed requirements. During development, the T-CAN485 drives only its single onboard GPIO4 WS2812B: no external strip and no real-time inter-device transport are included. An external strip of approximately eight LEDs is deferred to the effects-acceptance phase. The independent S2 Mini frontend and ESP-NOW are explicitly deferred to the next version. RS485, ESP-NOW, Wi-Fi UDP/WebSocket, BLE, and USB serial remain interchangeable future transport adapters. MQTT, Home Assistant, GPS, and cloud telemetry are later extensions.

### 2.2 Explicitly outside the first version

- Vehicle control, frame injection, frame spoofing, or gateway forwarding.
- Active OBD-II PID polling, UDS diagnostic sessions, clearing DTCs, or ECU programming.
- Any safety-critical decision or use as a replacement for the factory instrument cluster.
- ARGB effects that obscure factory warnings, distract the driver, or act as legally required external vehicle lighting.
- Claims that an unverified signal applies to every CX-5 KF model year, powertrain, configuration, or market.

**The project permanently excludes vehicle-side CAN transmission.** If active diagnostics or control is ever required, it is not an extension of this project. It must use a separate project, separate hardware and firmware, and a separate safety review. A transmission path must never be added to this project's vehicle build.

## 3. Safety Baseline

Vehicle CAN is a safety-related network. Every implementation and test must follow these rules:

- Use TWAI listen-only mode exclusively in vehicle builds. A vehicle build must not expose a `send` or `transmit` interface, link or invoke a transmission path, or fall back to normal mode when configuration is invalid; it must refuse to start instead.
- The public API of the CAN component may expose only start, stop, receive, and statistics operations. Network, serial, RS485, ESP-NOW, storage, and frontend inputs must not gain access to the CAN driver handle or cause CAN transmission.
- The first frontend link has one-way telemetry semantics. If local settings such as brightness are later accepted, they must remain isolated from the CAN receive component and must not create an arbitrary CAN-frame bridge or remote diagnostic entry point.
- Before the first vehicle connection, verify pins, bitrate, listen-only behavior, error counters, and power-loss behavior on an isolated bench.
- Never assume that CAN IDs, bit definitions, or OBD pins found online apply to this vehicle. Record the source and verify it with data from this vehicle.
- Do not casually add a 120 ohm termination resistor to the vehicle bus. Vehicle buses are normally already terminated at both ends, and another parallel resistor changes the impedance. Use two end terminators only on a bench bus whose topology requires them.
- The official T-CAN485 input range is 5–12 V. An automotive “12 V” supply can exceed that range during normal operation and transients. During prototyping, do not power the board directly from an unprotected vehicle supply; prefer isolated USB or a suitable protected regulator.
- Before connecting, use vehicle service information and measurements to identify CAN-H, CAN-L, ground, bus type, and recessive voltage. Pins 6 and 14 are commonly used for high-speed CAN on standard OBD-II, but this project must not infer that every target signal is available on that bus.
- Start vehicle tests while parked, with the parking brake applied, adequate ventilation, supervision, and no interaction with a laptop or device while driving merely to inspect logs.
- Secure, insulate, and fuse prototype wiring appropriately. It must not interfere with pedals, steering, airbags, or the driver's view.
- No failure may affect the original vehicle. The device must be fail-silent: reset, power loss, storage-card failure, and software crashes must not place dominant bits on the bus.
- Limit effect brightness and provide a night mode and master-off control. Effects must not glare at the driver, make the cabin resemble emergency lighting, or reduce recognition of factory warnings.
- On ARGB power failure, a stalled task, or stale CAN data, the default outcome is LEDs off. Never retain the last turn animation frame.

## 4. Confirmed Vehicle and Prototype Hardware

### 4.1 Target vehicle

- 2019 Mazda CX-5 KF.
- Australian market, right-hand drive.
- Akera grade with MRCC.
- SKYACTIV-G 2.5T turbocharged petrol engine; Mazda Australia's published specification is 170 kW and 420 Nm.
- Six-speed SKYACTIV-Drive automatic transmission.
- i-ACTIV AWD.
- Prefer a non-destructive OBD-II connection first. Plug-in Y harnesses near the instrument cluster, forward-sensing camera, or CMU may also be evaluated without cutting or stripping wires.

Mazda Australia published the addition of the 2.5T engine to the Australian CX-5 range on 2018-11-25, with availability from December that year, which matches this model year and powertrain. That update also listed GVC Plus, SBS, MRCC, DAA, HBC, and LDW/LAS; the Akera additionally included a 360-degree monitor and 19-inch wheels. For this project, the meaningful conclusion is that several potential data domains exist—powertrain, AWD, cluster, body, forward camera/radar, and parking cameras. The equipment list cannot establish CAN IDs or OBD visibility; those still require VIN-specific documentation and vehicle validation.

### 4.2 Available equipment

- LILYGO/TTGO T-CAN485.
- Two WEMOS/LOLIN D1 Mini boards: ESP8266EX, 3.3 V, 4 MB flash.
- One WEMOS/LOLIN S2 Mini: ESP32-S2FN4R2, 4 MB flash, 2 MB PSRAM, Wi-Fi.
- One ESP32-H2-DevKitM-1-N4: BLE and IEEE 802.15.4, without native Wi-Fi.
- An MCP2515 + TJA1050 CAN module confirmed to use an 8 MHz crystal.
- A 5 V Arduino development board; exact model remains to be recorded.
- A multimeter.
- No dedicated OBD diagnostic tool, oscilloscope, or USB-CAN adapter at present.

The module schematic provided by the project owner shows that the MCP2515 and TJA1050 share one `VCC` rail; the crystal is 8 MHz; and R2 is 120 ohms. Closing J1 connects R2 between CAN-H and CAN-L, while opening J1 disconnects the onboard termination. On an isolated two-ended bench, enable one 120 ohm terminator at each end and expect approximately 60 ohms with power removed. J1 must remain open when connected to the already terminated vehicle bus.

The schematic **does not support the conclusion that a direct ESP connection satisfies every component specification**. With shared `VCC` at 3.3 V, the MCP2515 is within specification but the TJA1050 is below its 4.75–5.25 V supply range. With shared `VCC` at 5 V, the TJA1050 and MCP2515 are correctly powered, but the MCP2515 `MISO/INT` outputs may overvoltage 3.3 V ESP inputs, and an ESP output cannot unconditionally satisfy the input-high threshold of a 5 V MCP2515.

The project owner accepts an experimental direct connection on the isolated bench: power both the D1 Mini and the entire MCP2515/TJA1050 module at 3.3 V, with no SPI level shifting. This exception is limited to a low-risk, supervised simulator. It must never connect to the vehicle and is not evidence for reliability or dedicated-hardware design. If the TJA1050 cannot establish a stable physical CAN layer at 3.3 V, stop using this arrangement and move to the 5 V Arduino, split supplies with appropriate level handling, or a natively 3.3 V-compatible CAN module. **Do not raise the module to 5 V while leaving SPI/INT directly connected to the D1 Mini.**

### 4.3 First ARGB prototype

- During development and decoder validation, do not use an independent frontend MCU or external strip. The T-CAN485 consumes `VehicleState` and turn events directly and drives its single onboard GPIO4 WS2812B.
- One LED validates state mapping, event response, the 250 ms freshness timeout, and driver stability. It cannot validate left/right flow direction, inter-pixel timing, or external-strip power delivery.
- Recommended diagnostic color mapping during development: `left=green`, `right=blue`, `hazard=amber`, and `off/unknown/stale=off`. This is only a bench convention that makes states distinguishable; it does not define final in-vehicle effect colors.
- The external strip of approximately eight WS2812B LEDs is deferred to the effects-acceptance phase. Select the external data GPIO, independent 5 V / 1 A supply, and level-shifting solution at that point.
- The WEMOS S2 Mini and ESP-NOW are both reserved for the next version, when the independent frontend boundary will be restored.
- If `TURN_SWITCH` has not updated for more than 250 ms, CAN is offline, or decoding fails, switch every pixel off immediately. Remain off while the startup state is `unknown`.

### 4.4 Prototype board roles

| Hardware | Phase-one role | Notes |
|---|---|---|
| T-CAN485 | Vehicle-side exporter and development ARGB consumer | TWAI listen-only, decoding, USB, and the single onboard GPIO4 WS2812B; attach the external strip only during effects acceptance |
| D1 Mini #1 | Web CAN simulator | Wi-Fi station/Web UI plus SPI and the 8 MHz MCP2515/TJA1050; experimentally power the whole module at 3.3 V; active transmission is isolated-bench only |
| S2 Mini | Next-version ARGB frontend | Receive via ESP-NOW and drive WS2812B in the next version; unused in the first version |
| D1 Mini #2 | Spare/future test device | May become a second web client or serial tool; without a second CAN transceiver it cannot act as a CAN ACK node |
| ESP32-H2 | Not used in the first version | No native Wi-Fi and cannot directly use ESP-NOW; reserve for BLE/Thread/Zigbee or low-power frontend experiments |
| 5 V Arduino | Backup CAN generator | Can drive the MCP2515 over 5 V SPI; not responsible for the current Web UI |

This inventory provides three compute roles—simulator, exporter, and ARGB frontend—but the physical CAN bus still has only one active MCP2515 node and one listen-only T-CAN485. If the T-CAN485 must remain on the final vehicle firmware while simulated frames are sent continuously, another normal CAN node with a transceiver is still required to provide ACK. A second bare D1 Mini cannot do that.

### 4.5 Verified T-CAN485 facts

These facts apply only to the version shown in the current official LILYGO T-CAN485 material. Verify the PCB revision and schematic against the physical board:

- MCU: ESP32, 4 MB flash, no PSRAM.
- CAN transceiver: SN65HVD231, connected to the ESP32 TWAI controller.
- CAN TX: GPIO27.
- CAN RX: GPIO26.
- CAN speed mode: GPIO23; vendor examples drive it low for high-speed mode.
- CAN/RS485 boost-supply enable: GPIO16.
- microSD: MISO 2, MOSI 15, SCLK 14, CS 13.
- This ESP32 has one classic TWAI controller and does not support CAN FD frames.
- The board also includes a MAX13487E-based RS485 interface. It is a candidate for wired telemetry to a dedicated frontend without adding another transceiver.

Do not scatter these values throughout the code. Centralize them in the `board` component or board-level configuration so the development board can be replaced and the software migrated to dedicated hardware.

## 5. Framework and Runtime Decisions

### 5.1 Baseline: native ESP-IDF

Use ESP-IDF rather than Arduino for product firmware because:

- TWAI, FreeRTOS, logging, watchdogs, NVS, Wi-Fi, OTA, and low-power functions share one official component and configuration model.
- Listen-only mode, queue capacity, interrupt behavior, memory, task priorities, and error handling are easier to control.
- It supports components, target tests, version pinning, and later board-level migration to custom hardware.
- It avoids rewriting most infrastructure when the prototype moves toward production reliability.

Arduino brings a board up faster, and vendor examples may be used independently to verify hardware, but it is not the primary product framework. If the team later changes to PlatformIO + Arduino after a requirements review, the component boundaries and test requirements in this document must remain intact.

Framework choice depends on the role. The T-CAN485 exporter and future S2 Mini ARGB frontend use ESP-IDF. The ESP8266-based D1 Mini cannot use ESP-IDF, so its isolated-bench Web simulator may use Arduino core for ESP8266 to reduce the cost of a non-product test tool. `vehicle_core` and the telemetry schema remain independent of every device framework.

The toolchain baseline is confirmed:

- Pin T-CAN485 product firmware to ESP-IDF `v5.5.4`. Upgrades require a dedicated Issue, replay tests, and bench validation.
- Implement `vehicle_core` in portable C++17 and run host tests with CMake and doctest.
- Use PlatformIO and Arduino core for ESP8266 `3.1.2` for the D1 Mini bench simulator.
- The first USB capture format is an output-only, versioned text protocol. Do not implement an SLCAN/LAWICEL transmit-command parser.
- Pin opendbc to an exact commit and retain source, license, and vehicle-validation status. Never track a floating branch.

### 5.2 FreeRTOS

Use FreeRTOS. ESP-IDF already runs on FreeRTOS; it is not a separate feature to add. Keep the initial set of long-lived tasks small:

- `can_rx`: highest application priority; retrieve frames promptly, timestamp them, and write them to a bounded queue or ring buffer.
- `processor`: filter, collect statistics, decode, and update normalized `VehicleState`.
- `export`: serial or network output; a slow consumer must not block receive.
- `storage`: batch microSD writes when enabled; it may be combined with `export` or remain separate.
- `local_argb`: enabled only in the first version; consume semantic state/events and refresh WS2812B. Its priority is below CAN receive, and it must neither inspect raw CAN IDs nor block receive.

ISR and driver callbacks may perform only fixed-duration, non-blocking work. Every queue must be bounded and expose an overflow counter. Do not allow indefinite waits, unbounded growth, or heavy dynamic allocation in high-frequency paths. Pin a task to a CPU core or move an interrupt into IRAM only when measurements demonstrate the need.

### 5.3 Core-library and frontend boundary

Use a portable core library, ESP-IDF device firmware, and an independent frontend:

- `vehicle_core` is a portable C/C++ library with no ESP-IDF, FreeRTOS, Wi-Fi, display, or LED dependency. It contains raw-frame types, Mazda decoding, `VehicleState`, freshness, and domain events.
- T-CAN485 firmware hosts the library and owns TWAI receive-only operation, time, queues, recording, and transport adapters.
- The long-term frontend is a protocol consumer responsible for screens, interaction, ARGB animation, and LED-strip hardware. It sees semantic data such as speed, RPM, gear, and turn state, never Mazda CAN IDs. As a temporary first-version exception, the same consumer interface runs as the built-in `local_argb` adapter on the T-CAN485.
- Host replay tools and tests reuse the same core library so MCU and offline decoding cannot diverge into separate implementations.
- Do not reduce this project to a library without flashable firmware. A library cannot initialize hardware or export telemetry by itself, so the repository also retains a thin reference exporter firmware.

Initially keep the core library, protocol, and T-CAN485 exporter in this repository so related changes and tests remain atomic. Keep the independent frontend in a separate project or repository. After protocol v1 is stable, `vehicle_core` may also be published as a separate library package.

## 6. Recommended Software Architecture

Data flow:

```text
Vehicle-side exporter (this project)
TWAI listen-only
    -> RawCanFrame + monotonic timestamp
    -> bounded queue / drop counters
    -> recorder -----------------------> USB / microSD raw capture
    -> vehicle_core decoders
    -> VehicleState + domain events
       |-> v1 local_argb --------------> onboard single LED during development /
       |                                 external ~8-LED WS2812B during effects acceptance
       `-> v2 versioned telemetry protocol
           -> USB / RS485 / ESP-NOW / UDP / WebSocket adapters
                                                  |
                                                  v
Independent v2 frontend (separate project)   dashboard + ARGB renderer
```

Recommended directory structure:

```text
firmware/
  tcan485/              ESP-IDF application assembly, startup, and lifecycle
components/
  board/                pin and power abstraction for T-CAN485 and future boards
  can_bus/              TWAI configuration, receive, statistics, and filtering
  capture/              raw-frame format, buffering, recording, and replay entry points
  exporters/            serial, file, and network adapters
  local_argb/           first-version local ARGB consumer; semantic state/events only
lib/
  vehicle_core/         portable types, VehicleState, freshness, and events
  mazda_kf/             verified model-specific signal decoding only
protocol/               versioned telemetry schema, codecs, and golden message vectors
tools/                  host conversion, analysis, and replay tools
tests/
  fixtures/             anonymized recordings and golden results
examples/
  telemetry_receiver/   minimal host or second-MCU consumer, not a product UI
simulators/
  d1mini_can_web/       isolated-bench Web UI and active MCP2515 frame generator
docs/                   wiring, experiment records, signal evidence, and hardware design
```

Core constraints:

- The CAN driver knows nothing about Mazda signal definitions; Mazda decoders know nothing about output transports.
- `RawCanFrame` includes at least timestamp, bus number, ID, standard/extended flag, RTR flag, DLC, and data.
- Every signal in `VehicleState` includes at least its value, unit, last update time, and `valid`, `stale`, or `unknown` state.
- Never retain a stale value indefinitely. Define a timeout for each signal.
- Keep decoder functions pure and free of global state wherever possible so recorded frames can test them on a host.
- The raw capture format must declare its version and byte order and include dropped-frame and timebase metadata. First guarantee lossless conversion to common host formats; consider compression later.
- Prefer DBC or an equivalent declarative source for model-specific signal definitions. Golden tests must verify generated or handwritten decoders.
- The telemetry protocol carries semantic state/events rather than raw DBC field names. Replacing the frontend or changing Mazda decoding must not require the CAN logic on the other side to be rewritten.
- Distinguish four concepts: turn-stalk request, hazard-switch request, actual lamp phase, and stale decoded data. The animation state machine must not treat every lamp pulse as a new user action.
- Export state snapshots and edge events. Snapshots recover from packet loss; events provide low latency. Every message includes protocol version, sequence number, source timestamp, and validity/freshness information.
- Dashboard and ARGB consumers use independent frame-rate and throttling policies. Numeric refresh, effect rendering, and network congestion must not occupy the CAN receive path.

## 7. CAN Bus and Signal Discovery

### 7.1 Existing comma.ai/opendbc leads

The open-source comma.ai `opendbc` project currently maps Mazda CX-5 model years 2017–2021 to `mazda_2017.dbc`. That DBC and Mazda's `carstate.py` already contain many candidate signals needed by this project:

- `ENGINE_DATA`, ID 514 / `0x202`: `RPM`, `SPEED`, and accelerator pedal.
- `WHEEL_SPEEDS`, ID 533 / `0x215`: four wheel speeds.
- `GEAR`, ID 552 / `0x228`: candidates for P/R/N/D and actual transmission gear.
- `BLINK_INFO`, ID 154 / `0x09A`: candidates for left/right lamps, high/low beam, and wipers.
- `TURN_SWITCH`, ID 145 / `0x091`: candidates for left/right turn-stalk request and hazard switch.
- `SEATBELT`, ID 832 / `0x340`.
- `DOORS`, ID 1086 / `0x43E`.
- Additional steering angle/torque, braking, cruise, blind-spot monitoring, and camera ADAS signals.

These definitions are useful starting points for the first decoders and test vectors, but they are **not yet confirmed on this vehicle**. The opendbc vehicle table is primarily based on the US market and marks the 2017–2021 CX-5 as dashcam mode. Australian right-hand drive, AWD, engine, and cluster differences may affect IDs, scaling, enumerations, or bus placement. Pin the upstream repository to an exact commit, retain its MIT license and provenance, and do not follow a floating `master` branch in production use.

### 7.2 Access-point priority

Do not assume speed, RPM, gear, and every lighting state appear on the same CAN bus exposed at the OBD connector. Use this priority order:

1. **OBD-II connector:** first choice because it is reversible and does not require trim removal. Connect only verified CAN-H, CAN-L, and ground, record passively, and determine whether the candidate IDs above are visible.
2. **Non-destructive Y harness at the instrument cluster:** if OBD does not expose indicators, doors, or cluster state, the cluster itself must receive much of the displayed data. This is the next candidate, but obtain the Australian wiring diagram for the exact CAN pair and pins.
3. **Non-destructive Y harness at the forward-sensing camera:** suitable for camera, lane, and ADAS buses. comma software distinguishes powertrain and camera buses, but that does not establish physical pin assignments for a 2019 Australian vehicle.
4. **CMU (Mazda Connect) connector:** may receive speed, reverse, lighting, and related signals for the infotainment system and may provide a convenient installation point. Use the wiring diagram to identify its network first.
5. **BCM/front body controller or CAN gateway area:** lighting and door signals may be more complete, but removal and misconnection risks are higher. Use a plug-in harness here only if the previous locations lack required data.

Do not use PCM, ABS/DSC, EPS, airbag, radar, or other safety-critical ECU connectors as the first probe point. Do not use insulation-piercing taps, strip wires, or leave probes inserted behind terminals. Mazda Australia identifies `mazdamanuals.com.au` as the local service-manual and wiring-diagram source. Before building a Y harness, obtain the network configuration, wiring diagram, and connector pinout for the correct model and VIN.

### 7.3 Vehicle validation process

Validate in this order:

1. Review service wiring information for the VIN, market, and powertrain; list CAN networks, gateways, and access points.
2. In listen-only mode, try candidate bitrates and confirm them using valid frame rate, error statistics, and ID distribution rather than hard-coding assumptions.
3. Record scenario-specific baselines: ACC/IGN states, idle, several steady RPM points, stationary gear selection, and isolated operation of each light or door.
4. Change one observable variable at a time and record ground truth from the factory cluster or an independent diagnostic tool.
5. Use differences, change rate, correlation, bit flips, counters, and checksums to identify candidate signals.
6. Record every candidate as a hypothesis with CAN ID, start bit, length, byte order, signedness, scale, offset, unit, period, timeout, sample file, and confidence.
7. Validate against a separate drive or operation sequence that was not used to derive the definition before marking it verified.
8. Add replay and boundary tests for every verified signal.

Any third-party DBC or signal table is only a lead. Verify authorization and licensing, record provenance, and validate it with data from this vehicle.

## 8. Use of Collected Information

The confirmed primary use is a real-time dashboard. The second core use is ARGB effects triggered by vehicle state. This project is the vehicle-side exporter and portable decoder library; the frontend is an independent consumer. They are coupled only through a stable semantic API and telemetry protocol.

### 8.1 Frontend separation and transport recommendations

ESP-NOW may serve a dedicated ESP32/ESP8266 frontend, but it is neither the only nor the core API. The core boundary is a versioned telemetry message whose transport can be replaced:

| Transport | Best use | Advantages | Main limitations | Current recommendation |
|---|---|---|---|---|
| USB serial | Development, recording, host dashboard | Easiest to debug; can also export diagnostics | Requires a cable and host | Required M1 baseline |
| TTL UART | Short bench link | No additional transceiver; protocol remains independent of wireless transport | Unsuitable for long vehicle wiring; requires common ground | Not selected; retain as a debug option |
| RS485 | Permanently installed separate MCU/display | Wired, noise-resistant, sufficient distance; already present on T-CAN485 | Requires wiring at both ends and an application protocol | Preferred long-term/production candidate |
| ESP-NOW | Independent ESP32/ESP8266 frontend and ARGB controller | No AP required, low latency, low packet overhead | Espressif-specific; peer, encryption, and Wi-Fi channel management required; phones and browsers cannot receive it natively | Explicitly deferred to the next version |
| Wi-Fi UDP | Low-latency stream to another MCU or host | General-purpose, simple, supports broadcast discovery | Delivery and order are not guaranteed; snapshots and sequence numbers must recover state | Optional real-time transport |
| Wi-Fi WebSocket | Browser, phone, or tablet dashboard | Mature frontend ecosystem and explicit connection state | TCP congestion and web-service overhead | Preferred browser transport |
| BLE notifications | Phone application | Does not require joining a WLAN | Pairing, throughput, and multi-client complexity | Not preferred; add only if required |

Recommended sequence: `USB serial + local T-CAN485 ARGB -> next-version S2 Mini + ESP-NOW -> choose RS485 or WebSocket for the final installation`. When ESP-NOW is added, both peers must use a matching channel. Callbacks may only copy messages into a bounded queue; they must not decode or render.

Telemetry protocol v1 should contain a magic value, protocol version, message type, sequence number, source monotonic timestamp, validity/staleness flags, payload, and CRC. Send a complete `VehicleState` snapshot at 10–20 Hz and immediately send events for turn/hazard changes. If an event is lost, the next snapshot restores state. USB/RS485 should use framing such as COBS, while ESP-NOW/UDP can use native packet boundaries. JSON is a debugging output, not the only normative format between resource-constrained devices. Freeze the binary schema with golden message vectors before implementation.

### 8.2 Real-time dashboard

Recommended first display set:

- Primary: vehicle speed, engine speed, and gear.
- Status: left/right turn, hazards, high/low beam, parking/brake state.
- Second group: four wheel speeds or slip indication, accelerator, steering angle, doors, seat belts, cruise, and blind-spot state.
- Diagnostics page: CAN frame rate, dropped frames, error counts, signal freshness, firmware version, and DBC version.

Recommended targets, to be quantified after discussion: less than 50 ms end-to-end from turn state to light effect or dashboard state; less than 100 ms display latency for speed and RPM; UI animation stalls must not affect CAN receive. The display device remains pending: phone/tablet web UI, dedicated TFT, existing infotainment browser, or another device.

### 8.3 ARGB effect consumer: same board in v1, independent in v2

- Domain state is `off / left / right / hazard / unknown`. Animation follows the turn-stalk or hazard request state and does not synchronize to the phase of the factory lamps.
- The current opendbc candidate defines independent `HAZARD`, `TURN_LEFT_SWITCH`, and `TURN_RIGHT_SWITCH` bits in `TURN_SWITCH` at ID `0x091`. Normalize with `HAZARD > LEFT/RIGHT > OFF`: whenever `HAZARD=1`, output `hazard` regardless of the left/right bits. If both left and right are active while hazard is inactive, output `unknown` and turn the LEDs off until vehicle evidence supports another interpretation.
- `LEFT_BLINK/RIGHT_BLINK` in `BLINK_INFO` at ID `0x09A` are candidates only for vehicle validation and diagnostics; they do not trigger first-version animation.
- Left and right may flow outward in their respective directions; hazard uses a symmetric effect.
- Switch the strip off when `TURN_SWITCH` has not updated for more than 250 ms, CAN is offline, firmware is starting, state is `unknown`, or an error occurs.
- `vehicle_core` and CAN/exporter components produce only state, events, and health information, never pixel frames. In v1, a separate `local_argb` component owns animation and RMT/LED driving; in v2, move the same consumer logic to the S2 Mini frontend.
- Brightness, color, direction, speed, LED count, and night-time limit are configurable.
- The effects engine accepts simulated events for frame-by-frame tests without a vehicle or LED strip.
- Power the strip from an independent, fused, protected 5 V supply. Never power the complete strip from an ESP32 GPIO or an unevaluated development-board 5 V pin. Design data-line level shifting, series resistance, common ground, and input bulk/decoupling capacitance after the strip type and length are fixed.

### 8.4 Later consumers

Candidate extensions include:

- raw CAN recording and reverse-engineering tools;
- real-time dashboards on a phone, infotainment system, or independent screen;
- SavvyCAN, a SocketCAN bridge, or custom host tools over USB serial;
- microSD trip logging and retrospective analysis;
- local WebSocket/HTTP telemetry;
- MQTT, Home Assistant, or Node-RED automation;
- GPS and CAN data fusion;
- track, efficiency, driver-behavior, or maintenance statistics.

The confirmed overall priority is: raw capture and USB export > decoding verified on this vehicle > versioned real-time telemetry > independent ESP32 + WS2812B frontend > real-time instrument display > other web, MQTT, or cloud extensions. Raw recording remains the foundation for validating output correctness and must not be omitted merely because the final product prioritizes real-time effects.

## 9. Test Strategy

### 9.1 Automated host tests

- Unit-test bit extraction, byte order, signed values, scaling, enumerations, and invalid values.
- Replay short recordings as golden tests and assert state transitions and timing.
- Test out-of-order, duplicate, truncated, unknown-ID, wrong-DLC, counter-jump, and signal-timeout inputs.
- Round-trip capture files through writer and reader.
- Use a simulated clock to test turn/hazard normalization, edge events, freshness timeouts, and priority.
- Store golden messages for every protocol version and test encode/decode, unknown fields, truncation, CRC errors, loss, reordering, and incompatible versions.
- In v1, feed recorded state/events into `local_argb` to test immediate response, timeout to `unknown/off`, and frame-by-frame animation. Add independent-frontend and inter-device transport tests in v2.
- The decoder and exporter layers must be testable without ESP32 hardware.

### 9.2 Isolated CAN bench

- Initially use the Arduino and existing MCP2515/TJA1050 module to send known classic CAN frames while the T-CAN485 runs the final listen-only firmware.
- Terminate both ends of the isolated bus with 120 ohms and verify approximately 60 ohms with power removed.
- Read the MCP2515 crystal marking and select the matching driver setting. Confirm Arduino I/O voltage before choosing MCP2515 logic-side power; keep the TJA1050 side at 5 V.
- Test the required 125/250/500 kbit/s rates, standard and extended IDs, burst load, and bus errors.
- Verify that listen-only firmware neither ACKs nor sends data frames and has no software transmit path. With one active sending node, frames receive no ACK. For low-rate functional tests, use MCP2515 one-shot and treat ACK/TX errors as expected evidence; sustained load requires another normal node to ACK.
- Stress queues, serial, and SD writes; record receive count, drop count, queue high-water mark, and write latency.
- Run full CAN load, recording, and the selected telemetry transport together. Verify that slow consumers, Wi-Fi, or a disconnected frontend do not cause CAN loss.
- Measure latency from injected `TURN_SWITCH`/`BLINK_INFO` frames to exporter domain events. The frontend project separately measures total latency to the first affected pixel.
- Test repeated reset, card removal, power loss, undervoltage, and full storage.

#### 9.2.1 D1 Mini + MCP2515 Web simulator

Implement an independent active-transmit target at `simulators/d1mini_can_web` that is **restricted to the isolated bench**:

1. Join existing Wi-Fi in station mode on the D1 Mini and provide a simple Web UI. Do not create a SoftAP during normal operation.
2. Let the user set vehicle speed, engine speed, P/R/N/D/actual gear, left/right turn, hazards, lighting, and online/stale data state.
3. Generate classic CAN frames such as `ENGINE_DATA`, `GEAR`, `TURN_SWITCH`, and `BLINK_INFO` at candidate DBC periods and transmit them through SPI/MCP2515/TJA1050.
4. Also provide raw ID/DLC/8-byte editing and preset replay so every test does not depend on the same high-level encoder.
5. Support fault injection: stop an ID, change its period, jump counters, use an invalid DLC, restart, or produce bursts.
6. Have the T-CAN485 receive and decode, export diagnostics over USB, and drive its local WS2812B. Reserve S2 Mini/ESP-NOW for the next version.

Web simulator network rules:

- Prefer the mDNS name `mazda-can-sim.local` and print the DHCP address over USB serial as a fallback. Use a router-side DHCP reservation when a stable address is needed; do not hard-code a LAN address in firmware.
- Configure first-time or changed Wi-Fi credentials over USB serial and store them in non-volatile device storage. Never put passwords in source, defaults, or the repository.
- Do not provide a normal-mode SoftAP or captive-portal fallback. If the network is unavailable, the Web UI is unavailable and CAN transmission remains off by default.
- Enable the Web UI only on the isolated bench. Require a local authentication token plus physical or startup confirmation before active transmission so another LAN device cannot operate it accidentally.
- D1 Mini infrastructure Wi-Fi serves only the simulator Web UI. It is independent of first-version local T-CAN485 ARGB and the next-version ESP-NOW link from T-CAN485 to S2 Mini.

The simulator and vehicle firmware are separate build targets. Directory names, binary names, and startup logs must prominently say `BENCH ACTIVE CAN TRANSMITTER`. Never reuse the vehicle harness or connect this target to a vehicle. The first-version simulator can validate decoding, freshness, and local T-CAN485 ARGB end to end; the next version adds the independent frontend and ESP-NOW. It **cannot prove that a candidate DBC matches the vehicle**. Signal truth still requires recordings from this vehicle and independent ground truth.

A strict listen-only receiver does not ACK. Continuous Web simulation requires a third normal CAN node to ACK; add an inexpensive CAN module or USB-CAN later. With only two nodes, use one of these options:

- Use MCP2515 one-shot for a small number of no-ACK safety-test frames and expect transmit errors.
- Only on a physically isolated bench, use a separate `BENCH_ACK_ONLY` receiver configuration that places the T-CAN485 in normal receive mode so it automatically ACKs while its public API still provides no data-frame transmit function. Its build name, startup warning, and physical process must ensure it never connects to a vehicle.

The final vehicle binary is always listen-only, and the release process must never produce a `BENCH_ACK_ONLY` vehicle artifact.

### 9.3 Staged vehicle validation

1. Vehicle powered off: verify harness, ground, absence of extra termination, and device power source.
2. Parked with IGN ON: record only; inspect bus errors, resets, and factory warning lights.
3. Parked with engine running: validate RPM and switch-type signals.
4. Low-speed test in a closed, safe environment: a passenger operates the recorder; the driver only drives.
5. Repeated cold/hot starts and longer drives: inspect time drift, sleep, power loss, frame drops, and thermal stability.

Record vehicle configuration, firmware version/commit, access point, bitrate, time, operation script, raw files, observations, and anomalies for every experiment.

## 10. Extensibility Principles

- Board abstraction: moving to a custom PCB must not change CAN, vehicle, or exporter business components.
- Multiple vehicles: keep vehicle decoders independent; do not put Mazda KF IDs in generic components.
- Multiple buses: reserve `bus_id` in data structures from the beginning. Current hardware has one CAN controller, but a future design may add a second controller or an external SPI CAN/CAN-FD controller.
- Multiple outputs: adapters subscribe to the same normalized state. Slow network or SD consumers must not back-pressure CAN receive.
- Library/protocol boundary: `vehicle_core` does not reference ESP-IDF; telemetry does not expose DBC fields or CAN IDs; the frontend does not link a vehicle CAN driver.
- Data versioning: capture formats, network APIs, configuration, and DBC definitions require compatibility strategies.
- Observability: expose uptime, received/dropped frame counts, bus errors, queue watermarks, remaining storage, reset reason, and firmware version.
- Feature flags: control Wi-Fi, BLE, SD, Web, MQTT, and similar functions with build-time or runtime configuration.
- Secure updates: if OTA is added, include signature verification, rollback, credential protection, and recovery mode.

## 11. Dedicated-Hardware Roadmap

Freeze dedicated-hardware requirements only after software and signal validation. By default, treat the vehicle exporter and display/ARGB frontend as independently replaceable hardware roles. They may become two enclosures, or share one PCB only if electrical isolation and software boundaries remain intact. Consider at least:

- a wide-input automotive buck supply, fuse, reverse-polarity protection, over/undervoltage protection, load-dump/transient protection, and ESD protection;
- low quiescent current, ignition or bus wake, automatic sleep after vehicle sleep, and safe shutdown;
- an automotive-suitable CAN transceiver, TVS, optional common-mode choke, and configurable termination that defaults open;
- for an independent frontend: RS485/wireless link, frontend MCU/display, and connectors; size ARGB supply, wire gauge, fuse, voltage drop, and thermal design from maximum LED count, brightness limit, and duty cycle;
- reliable 3.3 V MCU to 5 V ARGB level shifting and a low or high-impedance data output during power-up/reset to prevent random flashes;
- a hardware-default silent or standby state for the CAN transceiver so the system remains fail-silent before MCU initialization and during reset;
- tradeoffs among ESP32 module/chip, external watchdog, reliable storage, USB, microSD, status LEDs, and debug interfaces;
- whether a second CAN bus, LIN, CAN FD, GPS, IMU, RTC, LTE, display, or isolation is needed;
- OBD plug or dedicated harness, locking connectors, enclosure, flame-retardant materials, temperature, vibration, and EMC;
- production test points, programming fixture, serial numbers, calibration, firmware signing, and traceable BOM;
- schematic and PCB review, EVT bench prototypes, DVT environmental and vehicle validation, and only then a small production run.

Do not copy the T-CAN485's 5–12 V input and development-board protection assumptions into a dedicated board.

## 12. Milestones and Acceptance Gates

### M0 — Requirements and vehicle interface confirmation

- Primary use, vehicle configuration, and receive-only boundary are confirmed. Continue to define the display endpoint, ARGB specification, preferred telemetry transport, and permitted access points.
- Obtain or confirm wiring information.
- Select primary framework and toolchain versions.

Acceptance: enough pending decisions are resolved to start the project scaffold.

### M1 — Development-board and bench bring-up

- The ESP-IDF project builds and flashes reproducibly.
- T-CAN485 listens on the isolated bus and exports timestamped raw frames.
- Frame, error, and overflow statistics are available.
- The existing MCP2515 node or another safe CAN node injects test frames.
- `d1mini_can_web` can set simulated vehicle state and generate periodic frames.
- At least one host-side capture-parser test exists.

Acceptance: the agreed load and duration achieve zero drops or an explained drop rate, with no data-frame transmission from the receive target.

### M2 — Receive-only vehicle capture

- Confirm at least one safe access point and bitrate.
- Record reproducible multi-scenario experiments.
- The vehicle shows no new warning lights or abnormal behavior, and bus errors remain within an agreed limit.

Acceptance: raw data records reliably and replays offline.

### M3 — First verified signal decoders

- Speed, RPM, gear, left/right turn request, and hazard state reach `verified` status.
- Every signal has an evidence record, freshness rule, and golden replay test.
- Add other indicators and body states progressively.

Acceptance: results match independent ground truth in validation logs not used for derivation, with quantified error and latency.

### M4 — Local T-CAN485 ARGB MVP

- Freeze the `VehicleState` and event API; the inter-device telemetry protocol does not need to be frozen in v1.
- T-CAN485 consumes state locally and drives left, right, and hazard animations on approximately eight WS2812B LEDs.
- The independent S2 Mini frontend, inter-device telemetry, and ESP-NOW are next-version work and do not block this milestone.
- Retain USB raw capture and diagnostic statistics.
- Handle disconnect, sleep, power loss, full storage, and version upgrade.

Acceptance: complete an agreed-duration real-world test without unexplained loss or stale retained values. Turn response, animation frame rate, and brightness safety meet agreed targets. A dashboard screen is a separate consumer and does not block this milestone.

### M5 — Long-term installation reliability

- Complete startup, sleep, power, thermal, endurance, and recovery tests.
- Stabilize configuration, logs, error reporting, and release process.

Acceptance: dedicated-hardware requirements are ready to freeze.

### M6 — Dedicated-hardware EVT/DVT

- Complete power, protection, CAN physical layer, and manufacturability design.
- EVT verifies function; DVT verifies environment, EMC, power, and vehicle compatibility.

Quantify acceptance criteria when hardware requirements are frozen. “Works correctly” is not sufficient.

## 13. Pending Decisions

### Confirmed

- Long-term boundary: portable decoder library, receive-only T-CAN485 exporter, versioned telemetry, and independent frontend. V1 temporarily allows the T-CAN485 to drive WS2812B as a reference consumer.
- Primary product use: real-time dashboard. The second core use is left/right/hazard ARGB animation triggered by vehicle state.
- V1 has no screen. Development and decoder validation use the single onboard GPIO4 WS2812B; during effects acceptance, the T-CAN485 drives an external strip of approximately eight WS2812B LEDs. S2 Mini, inter-device telemetry, and ESP-NOW are deferred to v2. RS485 remains a long-term installation candidate.
- Animation is triggered by the stalk/hazard request in `TURN_SWITCH`, not synchronized with the lamp phase in `BLINK_INFO`. Candidate priority is `HAZARD > LEFT/RIGHT > OFF` and still requires vehicle validation.
- Turn state older than 250 ms switches the LEDs off. Startup and every `unknown` state remain off.
- The vehicle side never sends CAN data frames and performs no control, diagnostic polling, or arbitrary frame bridging.
- Target vehicle: 2019 Mazda CX-5 KF Akera, Australian right-hand drive, SKYACTIV-G 2.5T, 6AT, i-ACTIV AWD, with MRCC.
- Start at OBD and evaluate other non-destructive access points if required.
- Available equipment: T-CAN485, two D1 Mini boards, one S2 Mini, one ESP32-H2-DevKitM-1-N4, a 5 V Arduino, a shared-VCC 8 MHz MCP2515+TJA1050 module, and a multimeter.
- Build an independent D1 Mini Web UI + MCP2515 active CAN simulator on a vehicle-free isolated bench. Never connect it to a vehicle.
- The D1 Mini simulator joins existing Wi-Fi in station mode and does not create a SoftAP during normal operation.
- On the isolated bench, experimentally connect the D1 Mini directly to the shared-VCC MCP2515/TJA1050 module with both at 3.3 V. TJA1050 undervoltage may make the experiment fail. Never raise the module to 5 V while keeping it directly connected to the D1 Mini.
- Use comma.ai/opendbc `mazda_2017.dbc` as the primary signal lead pending validation on this vehicle.
- The isolated bench may use a separate `BENCH_ACK_ONLY` T-CAN485 build for sustained functional tests. Vehicle builds remain listen-only; safety tests still use MCP2515 one-shot/no-ACK scenarios.
- Repository name is `mazda-can-telemetry`, initially private. Use GitHub Issues, short-lived branches, Conventional Commits, squash-merged PRs, and a protected `main`.
- Map every GitHub Issue number to the project ticket key `MCAN-<number>`: Issue `#123` is ticket `MCAN-123` and its title is updated to begin with `[MCAN-123]`. GitHub's native `#123` reference remains necessary for automatic linking. Do not maintain a separate counter; gaps are expected because Issues and PRs share GitHub's repository number sequence.
- Repository governance, initial setup, and acceptance items are defined in `docs/work-items/0001-repository-bootstrap.md` and `CONTRIBUTING.md`.
- The project owner and agents may communicate in Chinese, but every repository artifact must be written in English. This includes documentation, source code, identifiers where under project control, comments, configuration descriptions, tests, fixtures created by the project, commit messages, Issues, and PR titles/bodies. Do not add Chinese text to the repository unless a future explicit localization requirement defines an exception.

### Highest priority

- What is the exact model of the 5 V Arduino? This affects only the backup bench design and does not block current planning.
- If the D1 Mini plus 3.3 V module is unstable, should the project switch directly to the 5 V Arduino as CAN simulator or purchase a natively 3.3 V CAN module?
- What exact WS2812B version or assembled strip, supply-wire gauge, mounting position, and maximum brightness are intended?
- If OBD capture lacks the required IDs, is removing the cluster or forward-camera trim and building a fully reversible Y harness acceptable?
- What is the team's experience with C/C++, ESP-IDF, Arduino, and Python?

### Later decisions

- Interoperable format for capture files and the live host stream.
- Whether onboard microSD is required, and retention duration and capacity limits.
- Whether internet or cloud connectivity is required, and data ownership, privacy, and credential management.
- Whether relative time is sufficient or RTC/NTP/GPS absolute time is required.
- How the device detects IGN, sleeps, wakes, and safely completes file writes.
- Whether to support a second CAN bus, CAN FD, LIN, or other vehicle models.
- Size, budget, mounting position, standby current, startup time, and environmental temperature targets.
- Project license in addition to the opendbc MIT license, and the vehicle-data sharing/anonymization policy.

## 14. Development-Agent Rules

- Read this document before implementation. If an unresolved decision would change architecture or vehicle safety, stop and request confirmation.
- Prefer the smallest testable and replayable change. Never add active transmission to “try” an unknown signal.
- Every new signal must include provenance/evidence, decoder definition, timeout rule, and test vector.
- Changes to board pins, power control, CAN mode, or bitrate must identify the applicable hardware revision.
- Avoid log storms and dynamic allocation in high-frequency paths. Every buffer requires a capacity, overflow policy, and counter.
- Network outputs consume snapshots or copied events and must not hold or block the CAN receive path.
- Never commit captures containing a VIN, precise location, Wi-Fi password, token, or non-anonymized private trip.
- After changes, run at least the affected host tests. For target-hardware work, state which bench and vehicle validations were and were not performed.
- Communicate with the project owner in Chinese when appropriate, but write every repository artifact in English, including code, comments, documentation, tests, configuration text, commit messages, Issues, and PRs.

## 15. Repository Collaboration and Release Rules

- `main` is the only long-lived branch. After the initial bootstrap commit, merge every non-trivial change from a short-lived branch through a PR.
- Follow `CONTRIBUTING.md` for branch names, commits, PRs, merge strategy, and `main` protection.
- Use `MCAN-<GitHub Issue number>` as the human-facing ticket key. Immediately update each newly created Issue title to `[MCAN-<number>] <title>` after GitHub assigns the number. Use the lowercase key in branch names, such as `feat/mcan-123-listen-only-capture`, and retain `#123` in PR bodies for GitHub automation.
- Every implementation PR links a GitHub Issue and states test evidence, bench/vehicle validation status, and CAN safety impact.
- Permit squash merge only by default. A Conventional Commit PR title becomes the commit title on `main`.
- Do not configure required status checks before CI exists. Once stable CI is available, add its checks to protection in a dedicated Issue so nonexistent checks cannot deadlock merges.
- `BENCH_ACK_ONLY` and the active CAN simulator must not enter vehicle release artifacts. Related PRs must be clearly marked `bench-only`.

## 16. Primary References

Pin external definitions to a version or commit and record the access date in implementation and validation evidence:

- LILYGO T-CAN485 official material: <https://github.com/Xinyuan-LILYGO/T-CAN485>
- WEMOS D1 Mini official material: <https://www.wemos.cc/en/latest/d1/d1_mini.html>
- WEMOS S2 Mini official material: <https://www.wemos.cc/en/latest/s2/s2_mini.html>
- ESP32-H2-DevKitM-1 official material: <https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32h2/esp32-h2-devkitm-1/index.html>
- ESP-IDF TWAI: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/twai.html>
- ESP-IDF FreeRTOS: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos.html>
- ESP-IDF RMT/LED strip: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/rmt.html>
- ESP-IDF ESP-NOW: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html>
- ESP-IDF HTTP/WebSocket server: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html>
- Microchip MCP2515 datasheet: <https://ww1.microchip.com/downloads/aemDocuments/documents/APID/ProductDocuments/DataSheets/MCP2515-Family-Data-Sheet-DS20001801K.pdf>
- comma.ai/opendbc: <https://github.com/commaai/opendbc>
- Mazda DBC: <https://github.com/commaai/opendbc/blob/master/opendbc/dbc/mazda_2017.dbc>
- Mazda vehicle-state parser: <https://github.com/commaai/opendbc/blob/master/opendbc/car/mazda/carstate.py>
- opendbc vehicle list: <https://github.com/commaai/opendbc/blob/master/docs/CARS.md>
- Mazda Australia service-manual and wiring-diagram entry point: <https://mazdamanuals.com.au/>
- Mazda Australia 2.5T CX-5 announcement, 2018-11-25: <https://www.mazda.com.au/mazda-news/turbo-boost-for-the-top-selling-mazda-cx-5/>
- NXP TJA1050 datasheet: <https://www.nxp.com/docs/en/data-sheet/TJA1050.pdf>
- Wiring article for the owner-provided MCP2515/TJA1050 module: <https://vegaprocessors.in/blog/interfacing-mcp2515-can-module-with-vega-aries-board/>
- Worldsemi WS2812B-V5 datasheet: <https://www.world-semi.co.kr/_files/ugd/89cd03_1023b0e9d135431aa1e6491bfc318112.pdf>
