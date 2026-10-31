# Stage 22 — bounded native guest virtual allocator

This stage adds `GuestVirtualAllocator` as a **native Windows/Linux guest-memory
backend** for future Xbox 360 kernel allocation APIs. It does not claim to
implement `NtAllocateVirtualMemory`, `XPhysicalAlloc`, `RtlAllocateHeap`,
or any other original Xbox ABI entry point.

## Implementation

- Allocates zero-initialized, 4 KiB page-rounded ranges from a configurable
  low 32-bit guest virtual-address window (default `0x10000000..0x70000000`).
- Uses first-fit search and power-of-two page-alignment requirements.
- Supports exact fixed-address reservations, accounting, exact-base release,
  double-free rejection and reuse of adjacent released ranges.
- Refuses windows overlapping the fixed-base original PE image or native PPC
  dispatch table. The low 64 KiB remains excluded.
- Host commit is attempted before recording an allocation. Exhaustion,
  invalid sizes, overlap and overflow cannot partially change allocator state.
- Reused pages are zeroed to avoid leaking stale guest state.
- `test_guest_virtual_allocator.cpp` uses synthetic addresses and exercises
  allocation, alignment, fixed reservations, fragmentation/reuse, bounds,
  invalid requests and accounting on Windows/Linux CI.
- The existing synthetic PPC linker smoke now allocates its XAM output
  buffer from this guest heap and links the allocator with the native
  PPC/XAM bridge; it then verifies guest writes and releases the allocation.

## Limits / next work

This allocator does **not** yet decommit or protect released host pages.
It does not model the complete Xbox 360 memory map, physical memory,
executable protection, kernel heap handles, per-process reservations, or
guest threading. It requires external synchronization when shared across
threads. Kernel PPC import ABI wrappers must be added only after their
exact signatures and flags are verified against the original import list.
The 696 private game translation units are not linked in public CI.

No XEX, ISO, game assets or proprietary recompiled C++ are published.
