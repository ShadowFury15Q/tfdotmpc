# Stage 26 — protect externally committed guest pages from heap allocation

The native guest allocator now consults the actual GuestMemory committed-page
bitmap before placing **either** fixed-address or first-fit allocations.
Previously, a guest page committed by the PE loader or another kernel service
could be selected by the heap because it was absent from the allocator's own
blocks_ map. The subsequent zero-initialization would silently corrupt data.

GuestMemory::any_committed_range checks whether **any** page in a requested
range is already committed, with invalid nonempty ranges failing closed.
First-fit searches skip externally committed pages while preserving requested
power-of-two alignment. Fixed-address allocations fail without altering memory
or allocator bookkeeping if even one page overlaps.

Regression coverage exercises partial overlap, three noncontiguous external
commits, alignment, exhaustion, zero-damage sentinels, release isolation and
reuse after external decommit. Existing PPC kernel/XAM tests remain enabled.

This protects public synthetic memory operations. It does **not** yet model
full Xbox 360 reservation/commit/decommit semantics, nor prove private PPC
linking or a playable game.
