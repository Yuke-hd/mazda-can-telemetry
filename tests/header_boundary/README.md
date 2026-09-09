# Public header boundary checker

`tools/check_public_headers.py` is an independently invokable, diagnostic
checker for the application-facing C++ headers. It compiles each entry point
in a fresh translation unit, reads the compiler-generated dependency file, and
then builds a tiny CMake consumer against the real
`vehicle_telemetry_contracts` target. The consumer probe checks the exported
include flags as well as compilation, so an accidental `private_include`
export is reported even when the source happens to compile.

Run it directly from the repository root:

```sh
python3 tools/check_public_headers.py
```

The current Stage 1.5 baseline is expected to report violations: the Mazda
facade contracts still reach decoder/frame/signal headers, the consumer target
exports a private include directory, and `mazda/internal_contracts.hpp` is
still publicly reachable. This check is intentionally not required CI yet.

The optional fixture test proves the checker itself. It copies the clean
fixture to a temporary directory, adds a forbidden transitive include or an
exported private path there, and verifies that the corresponding diagnostic is
returned. Production files are never mutated:

```sh
python3 tests/header_boundary/check_public_headers_test.py
```

The checker also runs two access probes. A normal consumer is expected to fail
when it includes `mazda/internal_contracts.hpp`; an explicitly authorized
consumer that receives the private include directory must succeed. The
baseline's public compatibility location is reported as a boundary failure,
while the authorized probe remains useful evidence during the header split.
