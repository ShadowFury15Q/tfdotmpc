# Stage 15 — Private native Windows link probe

Date: 2026-10-28. **No playable game or executed original entry point.**

## Purpose

A new optional CMake target attempts to **compile and link the actual privately
generated XenonRecomp C++ translation units** into a native Windows executable.
It uses the existing ABI-checked `compiled_ppc_adapter.cpp` to validate the
original game's `PPCFuncMappings` table and `_xstart` address. The executable
does **not call** `_xstart`; Xbox kernel, XAM, TLS, thread scheduling, graphics,
audio, and file I/O are not yet sufficiently implemented to run the game.

## Private Windows build (only if the user already owns generated source)

The original `default.xex`, extracted UE3 resources and proprietary
`ppc_recomp.*.cpp` files must remain **outside the public repository**.
Assuming `D:/DOTM/private/ppc` contains `ppc_recomp_shared.h`,
`ppc_context.h`, `ppc_func_mapping.cpp` and 600+ generated C++ units:

```powershell
cmake -S native -B private-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang-cl -DDOTM_PRIVATE_PPC_DIR=D:/DOTM/private/ppc
cmake --build private-build --target dotm_private_link_probe --parallel 2
./private-build/dotm_private_link_probe.exe
```

A link failure is a real blocker to resolve, not proof that game boot works.
The public GitHub Actions workflow cannot link the original game because the
private generated sources are intentionally absent. Windows and Linux CI
instead compile the existing synthetic ABI tests and run the new synthetic
stub-generator tests.

## What the new code does

- `native/generate_private_link_stubs.py` reads the private header and generated
  translation units locally. It finds declared PPC externals not defined by
  recompiled game functions, then writes **abort-on-call** placeholders to the
  local build directory. Two implemented XAM input functions are excluded.
- The generated placeholders exist solely to expose and separate actual native
  C++ linkage from unimplemented Xbox runtime behavior. In particular, PPC
  register-save helpers are **not** emulated by these placeholders.
- `native/private_link_probe_main.cpp` reserves guest memory, installs and
  validates the real compiled function mapping, prints the function count and
  entry address, then exits without executing any game code.
- The private CMake target is opt-in and excluded from normal public builds.
- `.gitignore` guards against accidentally adding private game files.

## Milestone boundary

This stage is verified as a public-source CMake integration plus synthetic
generator tests. A **successful private original-game link is not claimed**
until the optional target is built with the actual privately generated source
and its Windows linker result is recorded. The next real blocker is compiling
and linking that private target, followed by replacing all required fail-fast
imports and register-save helpers with correct native implementations.
