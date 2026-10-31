# Stage 25 — restricted native Xbox 360 NT virtual memory PPC ABI

Native imports `__imp__NtAllocateVirtualMemory` and
`__imp__NtFreeVirtualMemory` are now linked into the public synthetic PPC
link smoke and the optional private link probe. Their Xbox 360 argument
layout is **five PPC GPR arguments (r3–r7)** for allocate and **four
(r3–r6)** for free, with NTSTATUS returned in r3. The guest
`BaseAddress` and `RegionSize` are pointers to big-endian 32-bit words.

The implementation currently supports **MEM_RESERVE | MEM_COMMIT**
(`0x3000`) with **PAGE_READWRITE** (`0x04`) and **MEM_RELEASE**
(`0x8000`) of an exact allocation base with zero input RegionSize.
Guest addresses are page-rounded, allocated by GuestVirtualAllocator,
and decommitted on release. The output arguments are updated only after
a successful operation. A freed region is inaccessible until
recommitted and returns zero-filled.

The bridge rejects invalid, aliased, uncommitted and dispatch-table
argument pointers. Unsupported operations (reserve-only, commit-only,
MEM_DECOMMIT, MEM_RESET, large pages, non-RW protections and devkit
memory) **return an error, not a fake success**. This is a deliberately
restricted implementation, not full Xbox kernel compatibility.
Its current allocator address window is configurable by the host and
does not yet reproduce the complete 4K/64K Xbox memory map.

Tests: `xbox360_nt_virtual_memory_ppc_abi` exercises the actual PPC
import entry points with a synthetic PPCContext, including big-endian
output, fixed-address allocation, error paths, double free, decommit
revocation and zero-filled reuse. `synthetic_full_ppc_link_and_xam_import`
also calls the native kernel imports from a multi-object synthetic link.
No original game source, XEX or assets are included.

**Not demonstrated:** private DOTM C++ link, real game boot, Xbox kernel
memory parity, gameplay or campaign completion.
