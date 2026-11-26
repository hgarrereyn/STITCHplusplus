//! smallvec (v1 release line): a vector with inline storage that spills to the heap.
//!
//! Every object holds the same contents three times: a `SmallVec<[u8; 4]>`, a
//! `SmallVec<[Box<u8>; 2]>` (heap-owning items with a smaller inline buffer, so a double drop, a
//! leak or a use-after-free in the unsafe internals shows up under ASan) and a plain `Vec<u8>`
//! that mirrors every operation ("model").
//! Oracles: ASan, both SmallVecs equal the model after every call, capacity/spilled invariants,
//! and round trips (bincode encode/decode, into_vec/from_vec, from_raw_parts).
use smallvec::{smallvec, SmallVec, ToSmallVec};
use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use std::io::Write;
use std::mem::{ManuallyDrop, MaybeUninit};
use stitch::*;

pub type V = SmallVec<[u8; 4]>;
pub type B = SmallVec<[Box<u8>; 2]>;

pub struct Pair {
    pub v: V,
    pub b: B,
    pub model: Vec<u8>,
}

object!(Sv, Pair);

fn boxed<A: smallvec::Array<Item = Box<u8>>>(xs: &[u8]) -> SmallVec<A> {
    xs.iter().map(|&x| Box::new(x)).collect()
}

fn unbox(b: &[Box<u8>]) -> Vec<u8> {
    b.iter().map(|x| **x).collect()
}

fn same(p: &Pair, what: &str) {
    check(p.v.as_slice() == p.model.as_slice(), what);
    check(unbox(&p.b) == p.model, what);
    for (len, cap, spilled, inline) in [
        (p.v.len(), p.v.capacity(), p.v.spilled(), p.v.inline_size()),
        (p.b.len(), p.b.capacity(), p.b.spilled(), p.b.inline_size()),
    ] {
        check(len == p.model.len(), "len()");
        check(cap >= len, "capacity() >= len()");
        check(spilled == (cap > inline), "spilled() iff capacity() > inline_size()");
    }
    check(p.v.inline_size() == 4 && p.b.inline_size() == 2, "inline_size()");
    check(p.v.is_empty() == p.model.is_empty() && p.b.is_empty() == p.model.is_empty(), "is_empty()");
}

fn made(v: V, b: B, model: Vec<u8>, what: &str) -> New<Sv> {
    let p = Pair { v, b, model };
    same(&p, what);
    New::new(p)
}

/// An iterator whose size_hint overstates or understates its length (allowed by Iterator).
struct Lying<I>(I, usize);
impl<I: Iterator> Iterator for Lying<I> {
    type Item = I::Item;
    fn next(&mut self) -> Option<I::Item> {
        self.0.next()
    }
    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.1, Some(self.1))
    }
}

fn hint(len: usize, lie: i64) -> usize {
    match lie {
        0 => len,
        1 => len + 3,
        2 => len / 2,
        _ => 0,
    }
}

// ---------------------------------------------------------------- producers

endpoint! {
    fn new(n: Range<0, 12>, fill: u8, how: Range<0, 2>) -> New<Sv> {
        let n = *n as usize;
        let (v, b): (V, B) = match *how {
            0 => (SmallVec::from_elem(fill, n), SmallVec::from_elem(Box::new(fill), n)),
            1 => (smallvec![fill; n], smallvec![Box::new(fill); n]),
            _ => (SmallVec::new(), SmallVec::default()),
        };
        let model = if *how == 2 { vec![] } else { vec![fill; n] };
        made(v, b, model, "from_elem / smallvec! / new")
    }
}

endpoint! {
    fn from_slice(data: Bytes<32>) -> New<Sv> {
        made(SmallVec::from_slice(&data), boxed(&data), data.to_vec(), "from_slice")
    }
}

endpoint! {
    fn with_capacity(n: Range<0, 40>) -> New<Sv> {
        let n = *n as usize;
        let v = V::with_capacity(n);
        let b = B::with_capacity(n);
        check(v.capacity() >= n && b.capacity() >= n, "with_capacity(n).capacity() >= n");
        made(v, b, vec![], "with_capacity")
    }
}

endpoint! {
    fn convert(data: Bytes<32>, how: Range<0, 4>, extra: Range<0, 16>) -> New<Sv> {
        let mut vec = Vec::with_capacity(data.len() + *extra as usize);
        vec.extend_from_slice(&data);
        let mut bvec: Vec<Box<u8>> = Vec::with_capacity(data.len() + *extra as usize);
        bvec.extend(data.iter().map(|&x| Box::new(x)));
        let (v, b): (V, B) = match *how {
            0 => (SmallVec::from_vec(vec), SmallVec::from_vec(bvec)),
            1 => (V::from(vec), B::from(bvec)),
            2 => (V::from(&data[..]), B::from(&bvec[..])),
            3 => (data.to_smallvec(), B::from(&bvec[..])),  // (to_smallvec needs Copy)
            _ => (data.iter().copied().collect(), bvec.into_iter().collect()),
        };
        made(v, b, data.to_vec(), "from_vec / From / to_smallvec / collect")
    }
}

endpoint! {
    fn from_iter(data: Bytes<32>, lie: Range<0, 3>) -> New<Sv> {
        let h = hint(data.len(), *lie);
        let v: V = Lying(data.iter().copied(), h).collect();
        let b: B = Lying(data.iter().map(|&x| Box::new(x)), h).collect();
        made(v, b, data.to_vec(), "FromIterator with a wrong size_hint")
    }
}

endpoint! {
    fn from_buf(data: Bytes<4>, len: Range<0, 4>, how: Range<0, 5>) -> New<Sv> {
        let mut a = [0u8; 4];
        a[..data.len()].copy_from_slice(&data);
        let len = *len as usize;
        let (v, model): (V, Vec<u8>) = match *how {
            0 => (SmallVec::from_buf(a), a.to_vec()),
            1 => (SmallVec::from_buf_and_len(a, len), a[..len].to_vec()),
            2 => (unsafe { SmallVec::from_buf_and_len_unchecked(MaybeUninit::new(a), len) }, a[..len].to_vec()),
            3 => (V::from(a), a.to_vec()),
            4 => (V::from_const(a), a.to_vec()),
            _ => (unsafe { V::from_const_with_len_unchecked(a, len) }, a[..len].to_vec()),
        };
        // the Box twin: only full buffers (a partial one would leak the boxes past len)
        let b: B = if model.len() == 2 && *how != 2 && *how != 5 {
            SmallVec::from_buf([Box::new(model[0]), Box::new(model[1])])
        } else {
            boxed(&model)
        };
        made(v, b, model, "from_buf / from_buf_and_len / from_const")
    }
}

endpoint! {
    fn new_const() -> New<Sv> {
        const E: V = V::new_const();
        made(E, B::new_const(), vec![], "new_const")
    }
}

endpoint! {
    fn clone_sv(p: Borrow<Sv>) -> New<Sv> {
        let q = Pair { v: p.v.clone(), b: p.b.clone(), model: p.model.clone() };
        same(&q, "clone");
        New::new(q)
    }
}

endpoint! {
    fn decode(data: Bytes<64>) -> Result<New<Sv>, bincode::error::DecodeError> {
        let cfg = bincode::config::standard().with_limit::<4096>();
        let (v, n): (V, usize) = bincode::decode_from_slice(&data, cfg)?;
        check(n <= data.len(), "decode consumed at most the input");
        let (v2, n2): (V, usize) = match bincode::borrow_decode_from_slice(&data, cfg) {
            Ok(x) => x,
            Err(_) => { check(false, "borrow_decode accepts what decode accepts"); unreachable!() }
        };
        check(v2 == v && n2 == n, "borrow_decode agrees with decode");
        // a u8 and a Box<u8> have the same encoding
        let b: B = match bincode::decode_from_slice::<B, _>(&data, cfg) {
            Ok((b, nb)) => { check(nb == n, "Box<u8> decode consumes as much"); b }
            // (the limit counts size_of::<Box<u8>>() per element)
            Err(bincode::error::DecodeError::LimitExceeded) => bail("limit for boxes"),
            Err(_) => { check(false, "Box<u8> decode accepts what u8 decode accepts"); unreachable!() }
        };
        let model = v.to_vec();
        Ok(made(v, b, model, "bincode decode"))
    }
}

// ---------------------------------------------------------------- mutation

endpoint! {
    fn push(p: Borrow<Sv>, x: u8) {
        p.v.push(x);
        p.b.push(Box::new(x));
        p.model.push(x);
        same(&p, "push");
    }
}

endpoint! {
    fn pop(p: Borrow<Sv>) {
        let a = p.v.pop();
        let b = p.b.pop().map(|x| *x);
        let m = p.model.pop();
        check(a == m && b == m, "pop() result");
        same(&p, "pop");
    }
}

endpoint! {
    fn insert(p: Borrow<Sv>, at: Range<0, 40>, x: u8) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        p.v.insert(i, x);
        p.b.insert(i, Box::new(x));
        p.model.insert(i, x);
        same(&p, "insert");
    }
}

endpoint! {
    fn insert_many(p: Borrow<Sv>, at: Range<0, 40>, data: Bytes<24>, lie: Range<0, 3>) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        let h = hint(data.len(), *lie);
        p.v.insert_many(i, Lying(data.iter().copied(), h));
        p.b.insert_many(i, Lying(data.iter().map(|&x| Box::new(x)), h));
        let tail = p.model.split_off(i);
        p.model.extend_from_slice(&data);
        p.model.extend_from_slice(&tail);
        same(&p, "insert_many");
    }
}

endpoint! {
    fn insert_from_slice(p: Borrow<Sv>, at: Range<0, 40>, data: Bytes<24>) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        p.v.insert_from_slice(i, &data);
        let bs: Vec<Box<u8>> = data.iter().map(|&x| Box::new(x)).collect();
        p.b.insert_many(i, bs);  // (insert_from_slice needs Copy)
        let tail = p.model.split_off(i);
        p.model.extend_from_slice(&data);
        p.model.extend_from_slice(&tail);
        same(&p, "insert_from_slice");
    }
}

endpoint! {
    fn extend_from_slice(p: Borrow<Sv>, data: Bytes<24>, via_trait: bool) {
        let bs: Vec<Box<u8>> = data.iter().map(|&x| Box::new(x)).collect();
        if via_trait {
            smallvec::ExtendFromSlice::extend_from_slice(&mut p.v, &data);
        } else {
            p.v.extend_from_slice(&data);
        }
        p.b.extend(bs);  // (extend_from_slice needs Copy)
        p.model.extend_from_slice(&data);
        same(&p, "extend_from_slice");
    }
}

endpoint! {
    fn remove(p: Borrow<Sv>, at: Range<0, 40>, swap: bool) {
        let i = *at as usize;
        require(i < p.model.len(), "index >= len (documented panic)");
        let (a, b, m) = if swap {
            (p.v.swap_remove(i), *p.b.swap_remove(i), p.model.swap_remove(i))
        } else {
            (p.v.remove(i), *p.b.remove(i), p.model.remove(i))
        };
        check(a == m && b == m, "remove() / swap_remove() result");
        same(&p, "remove / swap_remove");
    }
}

endpoint! {
    fn truncate(p: Borrow<Sv>, n: Range<0, 40>, how: Range<0, 2>) {
        let n = *n as usize;
        match *how {
            0 => {
                p.v.truncate(n);
                p.b.truncate(n);
                p.model.truncate(n);
            }
            1 => {
                p.v.clear();
                p.b.clear();
                p.model.clear();
            }
            _ => {
                // set_len to a shorter length: fine for u8 (nothing to drop)
                require(n <= p.model.len(), "set_len beyond len");
                unsafe { p.v.set_len(n) };
                p.b.truncate(n);
                p.model.truncate(n);
            }
        }
        same(&p, "truncate / clear / set_len");
    }
}

endpoint! {
    fn drain(p: Borrow<Sv>, a: Range<0, 40>, b: Range<0, 40>, shape: Range<0, 4>, front: Range<0, 8>, back: Range<0, 8>) {
        let (a, b) = (*a as usize, *b as usize);
        let len = p.model.len();
        let (s, e) = match *shape { 0 => (a, b), 1 => (a, b + 1), 2 => (0, b), 3 => (a, len), _ => (0, len) };
        require(s <= e && e <= len, "range out of bounds (documented panic)");
        fn run<T: Clone, A: smallvec::Array<Item = T>>(v: &mut SmallVec<A>, shape: i64, a: usize, b: usize,
                                                        front: usize, back: usize) -> (Vec<T>, Vec<T>, usize) {
            let mut d = match shape {
                0 => v.drain(a..b),
                1 => v.drain(a..=b),
                2 => v.drain(..b),
                3 => v.drain(a..),
                _ => v.drain(..),
            };
            let n = d.len();
            let f: Vec<T> = d.by_ref().take(front).collect();
            let mut k: Vec<T> = Vec::new();
            for _ in 0..back {
                match d.next_back() { Some(x) => k.push(x), None => break }
            }
            (f, k, n) // the rest is dropped with `d`
        }
        let (vf, vk, vn) = run(&mut p.v, *shape, a, b, *front as usize, *back as usize);
        let (bf, bk, bn) = run(&mut p.b, *shape, a, b, *front as usize, *back as usize);
        let mut m: Vec<u8> = p.model.drain(s..e).collect();
        check(vn == m.len() && bn == m.len(), "Drain::len()");
        let f: Vec<u8> = m.iter().copied().take(*front as usize).collect();
        m.drain(..f.len());
        let mut k = Vec::new();
        for _ in 0..*back { match m.pop() { Some(x) => k.push(x), None => break } }
        check(vf == f && vk == k, "drain() items (front and back)");
        check(unbox(&bf) == f && unbox(&bk) == k, "drain() items (front and back, boxed)");
        same(&p, "drain");
    }
}

endpoint! {
    fn drain_filter(p: Borrow<Sv>, m: Range<1, 5>, add: u8, take: Range<0, 8>, keep_rest: bool) {
        let (m, take) = (*m as u8, *take as usize);
        let pred = |x: &mut u8| { let y = *x % m == 0; *x = x.wrapping_add(add); y };
        let pv: Vec<u8> = {
            let mut d = p.v.drain_filter(pred);
            let got: Vec<u8> = d.by_ref().take(take).collect();
            if keep_rest { d.keep_rest() }
            got
        };
        let pb: Vec<u8> = {
            let mut d = p.b.drain_filter(|x: &mut Box<u8>| { let y = **x % m == 0; **x = x.wrapping_add(add); y });
            let got: Vec<Box<u8>> = d.by_ref().take(take).collect();
            if keep_rest { d.keep_rest() }
            unbox(&got)
        };
        // model: walk the elements, removing (and yielding) those the predicate picks
        let old = std::mem::take(&mut p.model);
        let mut kept = Vec::new();
        let mut yielded = Vec::new();
        let mut idx = 0;
        while idx < old.len() {
            if keep_rest && yielded.len() == take { break; }
            let mut x = old[idx];
            let y = pred(&mut x);
            idx += 1;
            if y { yielded.push(x) } else { kept.push(x) }
        }
        kept.extend_from_slice(&old[idx..]);
        let first: Vec<u8> = yielded.iter().copied().take(take).collect();
        p.model = kept;
        check(pv == first && pb == first, "drain_filter() items");
        same(&p, "drain_filter / keep_rest");
    }
}

endpoint! {
    fn retain(p: Borrow<Sv>, m: Range<1, 5>, add: u8, mutable: bool) {
        let m = *m as u8;
        let mut f = |x: &mut u8| { let keep = *x % m != 0; *x = x.wrapping_add(add); keep };
        if mutable {
            p.v.retain_mut(&mut f);
            p.b.retain_mut(|x| { let keep = **x % m != 0; **x = x.wrapping_add(add); keep });
        } else {
            p.v.retain(&mut f);
            p.b.retain(|x| { let keep = **x % m != 0; **x = x.wrapping_add(add); keep });
        }
        p.model.retain_mut(f);
        same(&p, "retain / retain_mut");
    }
}

endpoint! {
    fn dedup(p: Borrow<Sv>, how: Range<0, 2>, k: Range<1, 64>) {
        let k = *k as u8;
        match *how {
            0 => { p.v.dedup(); p.b.dedup(); p.model.dedup(); }
            1 => {
                p.v.dedup_by(|a, b| *a / k == *b / k);
                p.b.dedup_by(|a, b| **a / k == **b / k);
                p.model.dedup_by(|a, b| *a / k == *b / k);
            }
            _ => {
                p.v.dedup_by_key(|a| *a / k);
                p.b.dedup_by_key(|a| **a / k);
                p.model.dedup_by_key(|a| *a / k);
            }
        }
        same(&p, "dedup / dedup_by / dedup_by_key");
    }
}

endpoint! {
    fn resize(p: Borrow<Sv>, n: Range<0, 40>, x: u8, with: bool) {
        let n = *n as usize;
        if with {
            let mut c = x;
            p.v.resize_with(n, || { c = c.wrapping_add(1); c });
            let mut c = x;
            p.b.resize_with(n, || { c = c.wrapping_add(1); Box::new(c) });
            let mut c = x;
            p.model.resize_with(n, || { c = c.wrapping_add(1); c });
        } else {
            p.v.resize(n, x);
            p.b.resize(n, Box::new(x));
            p.model.resize(n, x);
        }
        same(&p, "resize / resize_with");
    }
}

choices!(Huge: usize = [usize::MAX, 1usize << 63, usize::MAX / 2 + 1]);

endpoint! {
    fn capacity_ops(p: Borrow<Sv>, n: Range<0, 64>, op: Range<0, 5>) {
        let n = *n as usize;
        let len = p.model.len();
        match *op {
            0 => { p.v.reserve(n); p.b.reserve(n); }
            1 => { p.v.reserve_exact(n); p.b.reserve_exact(n); }
            2 => {
                check(p.v.try_reserve(n).is_ok() && p.b.try_reserve(n).is_ok(), "try_reserve(small)");
            }
            3 => {
                check(p.v.try_reserve_exact(n).is_ok() && p.b.try_reserve_exact(n).is_ok(), "try_reserve_exact(small)");
            }
            4 => {
                require(n >= len, "grow below len (documented panic)");
                p.v.grow(n);
                check(p.b.try_grow(n).is_ok(), "try_grow(small)");
            }
            _ => { p.v.shrink_to_fit(); p.b.shrink_to_fit(); }
        }
        if *op <= 3 {
            check(p.v.capacity() >= len + n && p.b.capacity() >= len + n, "reserve: capacity >= len + additional");
        }
        if *op == 5 {
            check(p.v.spilled() == (len > 4) && p.b.spilled() == (len > 2), "shrink_to_fit moves short vectors inline");
        }
        same(&p, "capacity ops");
    }
}

endpoint! {
    fn try_reserve_huge(p: Borrow<Sv>, n: OneOf<Huge>, op: Range<0, 2>) {
        let n = *n;
        let (a, b) = match *op {
            0 => (p.v.try_reserve(n), p.b.try_reserve(n)),
            1 => (p.v.try_reserve_exact(n), p.b.try_reserve_exact(n)),
            _ => (p.v.try_grow(n), p.b.try_grow(n)),
        };
        let _ = (a.map_err(|e| e.to_string()), b.map_err(|e| format!("{e:?}")));
        same(&p, "a failed try_reserve leaves the vector unchanged");
    }
}

endpoint! {
    fn extend(p: Borrow<Sv>, q: Borrow<Sv>, lie: Range<0, 3>) {
        let other = q.model.clone();
        let h = hint(other.len(), *lie);
        p.v.extend(Lying(other.iter().copied(), h));
        p.b.extend(Lying(other.iter().map(|&x| Box::new(x)), h));
        p.model.extend_from_slice(&other);
        same(&p, "extend");
    }
}

endpoint! {
    fn append(p: Borrow<Sv>, q: Borrow<Sv>) {
        let other = q.model.clone();
        let mut ov: SmallVec<[u8; 8]> = SmallVec::from_slice(&other);
        let mut ob: SmallVec<[Box<u8>; 3]> = boxed(&other);
        p.v.append(&mut ov);
        p.b.append(&mut ob);
        check(ov.is_empty() && ob.is_empty(), "append empties its argument");
        p.model.extend_from_slice(&other);
        same(&p, "append");
    }
}

endpoint! {
    fn clone_from(p: Borrow<Sv>, q: Borrow<Sv>) {
        let (sv, sb, sm) = (q.v.clone(), q.b.clone(), q.model.clone());
        p.v.clone_from(&sv);
        p.b.clone_from(&sb);
        p.model = sm;
        same(&p, "clone_from");
    }
}

endpoint! {
    fn write(p: Borrow<Sv>, data: Bytes<24>, how: Range<0, 2>) {
        match *how {
            0 => { let n = p.v.write(&data).unwrap(); check(n == data.len(), "write() writes everything"); }
            1 => p.v.write_all(&data).unwrap(),
            _ => { write!(p.v, "{}", data.len()).unwrap(); }
        }
        p.v.flush().unwrap();
        let added: Vec<u8> = if *how == 2 { data.len().to_string().into_bytes() } else { data.to_vec() };
        p.b.extend(added.iter().map(|&x| Box::new(x)));
        p.model.extend_from_slice(&added);
        same(&p, "io::Write");
    }
}

endpoint! {
    fn slice_ops(p: Borrow<Sv>, op: Range<0, 5>, i: Range<0, 40>, j: Range<0, 40>, x: u8) {
        let len = p.model.len();
        let (i, j) = (*i as usize, *j as usize);
        match *op {
            0 => { p.v.as_mut_slice().reverse(); p.b.reverse(); p.model.reverse(); }
            1 => { p.v.sort_unstable(); p.b.as_mut_slice().sort(); p.model.sort(); }
            2 => {
                require(i < len && j < len, "index out of bounds");
                p.v.swap(i, j); p.b.as_mut().swap(i, j); p.model.swap(i, j);
            }
            3 => {
                require(i < len, "index out of bounds");
                p.v[i] = x; *p.b[i] = x; p.model[i] = x;
            }
            4 => {
                require(i <= len, "mid > len");
                p.v.rotate_left(i); p.b.rotate_left(i); p.model.rotate_left(i);
            }
            _ => {
                require(i < len, "index out of bounds");
                unsafe { *p.v.as_mut_ptr().add(i) = x; }
                p.b[i] = Box::new(x);
                p.model[i] = x;
            }
        }
        same(&p, "as_mut_slice / DerefMut / IndexMut");
    }
}

endpoint! {
    fn raw_parts(p: Borrow<Sv>) {
        require(p.v.spilled() && p.b.spilled(), "not spilled (from_raw_parts needs a heap buffer)");
        // take the heap buffer apart and rebuild it with a smaller inline size, then convert back
        let mut v = ManuallyDrop::new(std::mem::take(&mut p.v));
        let (ptr, len, cap) = (v.as_mut_ptr(), v.len(), v.capacity());
        let small: SmallVec<[u8; 2]> = unsafe { SmallVec::from_raw_parts(ptr, len, cap) };
        p.v = V::from_vec(small.into_vec());
        let mut b = ManuallyDrop::new(std::mem::take(&mut p.b));
        let (ptr, len, cap) = (b.as_mut_ptr(), b.len(), b.capacity());
        let small: SmallVec<[Box<u8>; 1]> = unsafe { SmallVec::from_raw_parts(ptr, len, cap) };
        p.b = B::from_vec(small.into_vec());
        same(&p, "from_raw_parts / into_vec / from_vec");
    }
}

// ---------------------------------------------------------------- consumers

endpoint! {
    fn into_vec(p: Take<Sv>, how: Range<0, 3>) {
        let pair = p.into_inner();
        let model = pair.model.clone();
        match *how {
            0 => {
                check(pair.v.into_vec() == model, "into_vec()");
                check(unbox(&pair.b.into_vec()) == model, "into_vec() (boxed)");
            }
            1 => {
                check(&*pair.v.into_boxed_slice() == &model[..], "into_boxed_slice()");
                check(unbox(&pair.b.into_boxed_slice()) == model, "into_boxed_slice() (boxed)");
            }
            _ => {
                let vs = pair.v.spilled();
                match pair.v.into_inner() {
                    Ok(a) => check(!vs && model.len() == 4 && a[..] == model[..], "into_inner() gives the array"),
                    Err(v) => check((vs || model.len() != 4) && v.as_slice() == &model[..], "into_inner() refuses"),
                }
                let bs = pair.b.spilled();
                match pair.b.into_inner() {
                    Ok(a) => check(!bs && model.len() == 2 && unbox(&a) == model, "into_inner() gives the array (boxed)"),
                    Err(b) => check((bs || model.len() != 2) && unbox(&b) == model, "into_inner() refuses (boxed)"),
                }
            }
        }
    }
}

endpoint! {
    fn into_iter(p: Take<Sv>, front: Range<0, 8>, back: Range<0, 8>, clone: bool) {
        let pair = p.into_inner();
        let m = pair.model.clone();
        let (f, k) = (*front as usize, *back as usize);
        let mut it = pair.v.into_iter();
        let mut bit = pair.b.into_iter();
        let mut lo = 0;
        let mut hi = m.len();
        for _ in 0..f {
            let (a, b) = (it.next(), bit.next().map(|x| *x));
            let e = if lo < hi { lo += 1; Some(m[lo - 1]) } else { None };
            check(a == e && b == e, "IntoIter::next()");
        }
        for _ in 0..k {
            let (a, b) = (it.next_back(), bit.next_back().map(|x| *x));
            let e = if lo < hi { hi -= 1; Some(m[hi]) } else { None };
            check(a == e && b == e, "IntoIter::next_back()");
        }
        check(it.as_slice() == &m[lo..hi] && unbox(bit.as_slice()) == m[lo..hi], "IntoIter::as_slice()");
        check(it.len() == hi - lo && bit.size_hint() == (hi - lo, Some(hi - lo)), "IntoIter::len()");
        if let Some(x) = it.as_mut_slice().first_mut() { *x = x.wrapping_add(1); }
        if clone {
            let c: Vec<u8> = it.clone().collect();
            let cb: Vec<u8> = bit.clone().map(|x| *x).collect();
            check(c.len() == hi - lo && cb == m[lo..hi], "IntoIter::clone()");
        }
        // the rest is dropped with the iterators
    }
}

// ---------------------------------------------------------------- read only

fn hash_of<T: Hash + ?Sized>(x: &T) -> u64 {
    let mut h = DefaultHasher::new();
    x.hash(&mut h);
    h.finish()
}

endpoint! {
    fn compare(p: Borrow<Sv>, q: Borrow<Sv>) {
        check((p.v == q.v) == (p.model == q.model), "PartialEq");
        check((p.b == q.b) == (p.model == q.model), "PartialEq (boxed)");
        check(p.v.cmp(&q.v) == p.model.cmp(&q.model), "Ord");
        check(p.b.partial_cmp(&q.b) == p.model.partial_cmp(&q.model), "PartialOrd (boxed)");
        let wide: SmallVec<[u8; 8]> = SmallVec::from_slice(&q.model);
        check((p.v == wide) == (p.model == q.model), "PartialEq across inline sizes");
        check(hash_of(&p.v) == hash_of(&p.model[..]), "Hash agrees with the slice");
    }
}

endpoint! {
    fn inspect(p: Borrow<Sv>, i: Range<0, 40>) {
        let i = *i as usize;
        let m = &p.model;
        check(p.v.iter().copied().eq(m.iter().copied()), "iter()");
        check((&p.v).into_iter().rev().copied().eq(m.iter().rev().copied()), "(&v).into_iter().rev()");
        check(p.v.get(i) == m.get(i), "get()");
        if i <= m.len() {
            check(&p.v[..i] == &m[..i] && &p.v[i..] == &m[i..], "Index<Range>");
            check(unbox(&p.b[i..]) == m[i..], "Index<Range> (boxed)");
        }
        check(p.v.as_ref() == &m[..] && <V as std::borrow::Borrow<[u8]>>::borrow(&p.v) == &m[..], "AsRef / Borrow");
        check(std::ptr::eq(p.v.as_ptr(), p.v.as_slice().as_ptr()), "as_ptr() is the slice's pointer");
        check(format!("{:?}", p.v) == format!("{:?}", m), "Debug");
        for x in (&mut p.b).into_iter() { let _ = **x; }
        let _ = (p.b.as_mut_slice().len(), p.b.as_mut_ptr());
        same(&p, "inspect");
    }
}

endpoint! {
    fn encode(p: Borrow<Sv>) {
        let cfg = bincode::config::standard();
        let ev = bincode::encode_to_vec(&p.v, cfg).unwrap();
        let eb = bincode::encode_to_vec(&p.b, cfg).unwrap();
        check(ev == eb, "u8 and Box<u8> vectors encode the same");
        let em = bincode::encode_to_vec(&p.model, cfg).unwrap();
        check(ev == em, "SmallVec encodes like Vec");
        let back: Result<(V, usize), _> = bincode::decode_from_slice(&ev, cfg);
        check(matches!(&back, Ok((v, n)) if v.as_slice() == &p.model[..] && *n == ev.len()), "encode -> decode round trip");
        let back: Result<(B, usize), _> = bincode::decode_from_slice(&eb, cfg);
        check(matches!(&back, Ok((b, n)) if unbox(b) == p.model && *n == eb.len()), "encode -> decode round trip (boxed)");
    }
}
