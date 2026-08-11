# T-CAN485 board record

## Scope and revision status

This record centralizes the pin capabilities used by the T-CAN485 exporter. The
pin mapping is taken from the current vendor material for the LILYGO T-CAN485:

- <https://github.com/Xinyuan-LILYGO/T-CAN485>
- <https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/peripherals/gpio.html>

The vendor material and the ESP-IDF GPIO API reference were reviewed on
2026-08-11. The vendor material is the source for the board mapping; the
Espressif reference is the source for the GPIO reset, level, direction, and
error-return behavior used by the implementation.

The repository does not yet contain a confirmed PCB revision, schematic
revision, or physical continuity measurements. `HardwareRevision` therefore
remains `kTcan485VendorMaterialRevisionPending`. Before a vehicle connection,
compare the installed PCB and silkscreen with the vendor material and update
this record only when the revision-specific evidence is recorded.

## Capability mapping

| Capability | GPIO | Safe reset state | Status |
| --- | ---: | --- | --- |
| CAN TX | 27 | Output high (recessive) | Vendor material; PCB revision pending |
| CAN RX | 26 | Input | Vendor material; PCB revision pending |
| CAN speed mode | 23 | Output low (high-speed select) | Vendor example; PCB revision pending |
| CAN/RS485 boost enable | 16 | Output low (disabled) | Vendor material; PCB revision pending |
| microSD MISO | 2 | Input | Vendor material; PCB revision pending |
| microSD MOSI | 15 | Input | Vendor material; PCB revision pending |
| microSD SCLK | 14 | Input | Vendor material; PCB revision pending |
| microSD CS | 13 | Input | Vendor material; PCB revision pending |
| Onboard WS2812B data | 4 | Output low (LED off) | Project record; PCB revision pending |

The board component exposes capabilities to firmware assembly only. `vehicle_core`
and Mazda signal decoders do not include board headers and cannot see GPIO
numbers or obtain a CAN driver handle.

## Initialization and fail-closed behavior

`board::initialize_safe_defaults()` must be the first hardware operation in the
T-CAN485 application, before CAN, microSD, transport, or ARGB drivers start. It
performs the following sequence:

1. Validate the compile-time capability record and refuse initialization if it
   is invalid.
2. Disable the CAN/RS485 boost supply and drive the onboard LED data low.
3. Drive CAN TX high (recessive), select the documented speed mode, and make
   CAN RX an input.
4. Leave microSD pins as inputs until the storage driver is explicitly started.
5. Return failure if any GPIO operation fails, reasserting boost disabled, LED
   off, and CAN TX recessive on a best-effort basis. Firmware must then stop and
   must not fall back to normal CAN mode.

Vehicle firmware is permanently listen-only. This component provides no CAN
start, send, transmit, or diagnostic operation. A future CAN component must
enforce `twai_general_config_t::mode = TWAI_MODE_LISTEN_ONLY` and reject any
configuration that is not listen-only; invalid configuration must fail closed.

## Bring-up checklist

The following items require the physical board and an isolated bench; none are
claimed as complete by this record:

- [ ] Photograph the PCB silkscreen and record the exact revision.
- [ ] Compare the CAN, boost, microSD, and GPIO4 WS2812B nets with the vendor
      schematic/material for that revision.
- [ ] With power removed, check that no added termination is present for a
      vehicle connection; use two end terminators only on a two-ended bench bus.
- [ ] Power from an isolated, protected source within the board input range;
      do not connect an unprotected automotive supply.
- [ ] Verify reset/startup levels: CAN TX recessive, boost disabled, and LED
      off before enabling any peripheral.
- [ ] On an isolated bench only, verify that the eventual TWAI configuration is
      listen-only and that no transmit path is linked or callable.
- [ ] Verify microSD and onboard LED behavior after safe defaults, including
      power loss and reset; confirm the LED remains off on stale/unknown state.
- [ ] Record GPIO continuity, error counters, and power-loss observations in a
      revision-specific bring-up log before any vehicle test.
