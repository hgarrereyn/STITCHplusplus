# smallvec (servo/rust-smallvec @ v1.6.0)

11 stubs: `new`, `from_slice`, `push`, `pop`, `insert`, `insert_many`, `remove`, `truncate_drain`,
`capacity_ops` (`reserve`, `reserve_exact`, `shrink_to_fit`, `grow`, `resize`), `extend`, `into_vec`. Object: `Sv` (a
`SmallVec<[u8; 4]>` with a `Vec<u8>` model). Oracles: AddressSanitizer for the unsafe internals,
and the model after every operation. `insert_many` takes an iterator whose `size_hint` may lie in
either direction, which the `Iterator` contract allows.

Campaign: 10 min, 2 workers: 742k execs (1.2k/s), 650 edges, 4 buckets.

## Library bug

**RUSTSEC-2021-0003: buffer overflow in `SmallVec::insert_many`.** When the iterator yields more
elements than its `size_hint` promised, `insert_many` writes past the reserved space. Seen as:
- an ASan heap-buffer-overflow WRITE (`__asan_memmove` from `insert_many`, lib.rs:1048) once the
  vector has spilled to the heap;
- "entered unreachable code" from smallvec's own debug assertions (`heap_mut`, lib.rs:185) when
  the inline/heap state is already corrupted (3 buckets with one cause).

All 4 buckets are gone at v1.6.1, the fixed release. Reproducer: `bugs/rustsec-2021-0003/`
(the ASan heap-buffer-overflow).
