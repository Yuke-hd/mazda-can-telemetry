# MCAN-71 DBC metadata verification

Status: Stage 2 S2-D host verification for the S1-E evidence inventory.

The typed constexpr definitions in
`lib/mazda/include/mazda/definitions.hpp`
remain the executable metadata authority. The reviewed source artifact
`docs/protocol/mazda_custom.dbc` remains
byte-for-byte source evidence. S2-D does not parse DBC at runtime, generate a
runtime schema, or change decoder numeric/semantic definitions.

## Host check

Run the comparison from the repository root:

```text
python3 tools/compare_mazda_dbc.py
```

The checker compiles
`tools/dump_mazda_metadata.cpp`, which
exports the compiled constexpr table in a stable tab-delimited host-tool
format. The checker then compares the supported 16-signal subset's identifier,
DBC start bit, length, byte order, scale, offset, and declared physical
range. Numeric mismatches are errors; they cannot be hidden by an evidence
exception.

`SPEED` is intentionally excluded because it is a retained candidate and has
no field in the reviewed custom DBC. The only source-name/interpretation
exception is `DOORS.FrontOtherDoor_Open_Reference` mapped to the public
`front_left_door_open_rhd` channel. Reference suffix removal on other public
names is represented explicitly in the constexpr supported-signal map.

## Evidence boundary

Each supported metadata entry carries a `ValidationStatus` confidence and a
provenance reference. The assignments are the S1-E inventory: `EngineRPM`,
`Selector`, and `FrontRightDoor_Open_RHD` are `Confirmed`; `ActualGear`,
`WiperLow`, and `FrontWiper` are `Observed`; the remaining supported entries
are `Reference`. These statuses describe reviewed evidence, not decoder
validity, freshness, or runtime availability.

The module-local
`dbc_metadata_comparison_test.py`
checks the reviewed baseline, verifies the DBC digest, and mutates temporary
metadata to prove that both a numeric drift and an unsupported confidence
promotion fail with diagnostics. Temporary mutations are never written to the
repository.
