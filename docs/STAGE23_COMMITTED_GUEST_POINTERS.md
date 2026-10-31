# Stage 23 — committed guest-pointer validation for XAM

The native guest memory manager now records the 4 KiB pages successfully
committed by the host backend (a 128 KiB bitset covering the 4 GiB guest
address space). `committed_range(address, bytes)` returns false for zero
length, overflow, uncommitted pages, and ranges crossing an uncommitted page.

The native Xbox 360 XAM input import bridge now requires an already
committed guest output buffer before writing the 16-byte gamepad state
or 20-byte capabilities. It no longer silently commits an arbitrary guest
address supplied by recompiled PPC code. Existing native dispatch-table
write protections remain in place. The null GetState presence-query
semantics are unchanged.

Regression tests cover initial uncommitted pages, multi-page commits,
address-space overflow, PE image commits, XAM writes to committed
buffers, and rejection of an uncommitted XAM output pointer. The
synthetic private-link test continues to use an allocator-owned buffer.

**Limitations:** This is host-commit tracking, not the complete Xbox
360 virtual-memory manager or page-protection model. There is no
decommit/revocation or guest thread synchronization yet. The real
696-unit game link and original campaign boot remain unverified.
No original game resources or proprietary C++ are committed.
