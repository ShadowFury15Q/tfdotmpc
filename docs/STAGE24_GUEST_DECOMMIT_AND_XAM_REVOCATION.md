# Stage 24 — revoke released guest virtual pages

GuestMemory now exposes `decommit(address, length)`. Windows uses
`VirtualFree(MEM_DECOMMIT)`; Linux discards old bytes using
`madvise(MADV_DONTNEED)` and revokes access with `mprotect(PROT_NONE)`.
The guest committed-page bitmap changes only after host calls succeed.
Recommitted pages start zeroed.

GuestVirtualAllocator::release decommits an exact-base allocation before
removing it from allocator tracking. If host decommit fails, allocation
metadata remains intact. Neighboring allocations stay committed.

Synthetic tests exercise decommit/recommit, zero-fill, adjacent pages,
multi-page release, allocator reuse and XAM rejection of freed pointers
both through the PPC import ABI and the native multi-object link smoke.

This is not the Xbox 360 NtAllocateVirtualMemory/NtFreeVirtualMemory ABI,
nor proof of original-game linking or booting. No proprietary assets or
recompiled game code are published.
