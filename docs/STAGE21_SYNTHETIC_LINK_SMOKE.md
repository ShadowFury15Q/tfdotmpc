# Stage 21 — public multi-object native PPC linker smoke test

This stage adds a **real native executable link test**, using only synthetic,
publicly authored PPC source. It does not establish that the original 696
private game translation units can be linked or run.

The CMake target `dotm_synthetic_private_link_test` compiles and links:
- A synthetic XenonRecomp-compatible mapping table and `_xstart` implementation,
  deliberately split into separate C++ translation units.
- The production `compiled_ppc_adapter.cpp`, `recomp_bridge.cpp`, and
  `xam_ppc_import.cpp`.
- A generated fail-fast Xbox kernel import placeholder, produced by the
  **same** `generate_private_link_stubs.py` used for private game builds.
- The native guest memory and XAM keyboard/mouse action bridge.

The executable checks that the original-style mapping installs, the synthetic
entry function can be dispatched, and an XAM import can write an Xbox 360
big-endian controller state into guest memory. The unresolved kernel import
is deliberately not called: it must not be mistaken for a working kernel API.

The fixture uses `--minimum-units 1` **only** in this separate public test
target. The private game target retains its minimum of 600 translation units.
No original XEX, ISO, assets, or proprietary C++ are committed.

## Remaining blockers

Actual Windows linkage of the private generated game source is still
unverified. Real kernel/XAM services, PPC helper semantics, graphics, audio,
resource loading, TLS, game startup, and full campaign parity remain incomplete.
