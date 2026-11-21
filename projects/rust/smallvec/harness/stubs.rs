//! smallvec: a vector with inline storage that spills to the heap.
//! Oracles: ASan (memory safety of the unsafe internals) and a plain Vec<u8> that mirrors every
//! operation ("model").
use smallvec::SmallVec;
use stitch::*;

pub struct Pair {
    pub v: SmallVec<[u8; 4]>,
    pub model: Vec<u8>,
}

object!(Sv, Pair);

fn same(p: &Pair, what: &str) {
    check(p.v.as_slice() == p.model.as_slice(), what);
    check(p.v.len() == p.model.len(), "len()");
    check(p.v.capacity() >= p.v.len(), "capacity() >= len()");
}

endpoint! {
    fn new(n: Range<0, 12>, fill: u8) -> New<Sv> {
        let v: SmallVec<[u8; 4]> = SmallVec::from_elem(fill, *n as usize);
        New::new(Pair { v, model: vec![fill; *n as usize] })
    }
}

endpoint! {
    fn from_slice(data: Bytes<32>) -> New<Sv> {
        New::new(Pair { v: SmallVec::from_slice(&data), model: data.to_vec() })
    }
}

endpoint! {
    fn push(p: Borrow<Sv>, x: u8) {
        p.v.push(x);
        p.model.push(x);
        same(&p, "push");
    }
}

endpoint! {
    fn pop(p: Borrow<Sv>) {
        let a = p.v.pop();
        let b = p.model.pop();
        check(a == b, "pop() result");
        same(&p, "pop");
    }
}

endpoint! {
    fn insert(p: Borrow<Sv>, at: Range<0, 40>, x: u8) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        p.v.insert(i, x);
        p.model.insert(i, x);
        same(&p, "insert");
    }
}

endpoint! {
    fn insert_many(p: Borrow<Sv>, at: Range<0, 40>, data: Bytes<24>, lie: Range<0, 3>) {
        let i = *at as usize;
        require(i <= p.model.len(), "index > len (documented panic)");
        // an iterator whose size_hint overstates or understates its length (allowed by Iterator)
        let hint = match *lie { 0 => data.len(), 1 => data.len() + 3, 2 => data.len() / 2, _ => 0 };
        struct Lying<I>(I, usize);
        impl<I: Iterator<Item = u8>> Iterator for Lying<I> {
            type Item = u8;
            fn next(&mut self) -> Option<u8> { self.0.next() }
            fn size_hint(&self) -> (usize, Option<usize>) { (self.1, Some(self.1)) }
        }
        p.v.insert_many(i, Lying(data.iter().copied(), hint));
        let tail = p.model.split_off(i);
        p.model.extend_from_slice(&data);
        p.model.extend_from_slice(&tail);
        same(&p, "insert_many");
    }
}

endpoint! {
    fn remove(p: Borrow<Sv>, at: Range<0, 40>) {
        let i = *at as usize;
        require(i < p.model.len(), "index >= len (documented panic)");
        let a = p.v.remove(i);
        let b = p.model.remove(i);
        check(a == b, "remove() result");
        same(&p, "remove");
    }
}

endpoint! {
    fn truncate_drain(p: Borrow<Sv>, a: Range<0, 40>, b: Range<0, 40>, op: Range<0, 3>) {
        let (a, b) = (*a as usize, *b as usize);
        match *op {
            0 => {
                p.v.truncate(a);
                p.model.truncate(a);
            }
            1 => {
                require(a <= b && b <= p.model.len(), "range out of bounds");
                let x: Vec<u8> = p.v.drain(a..b).collect();
                let y: Vec<u8> = p.model.drain(a..b).collect();
                check(x == y, "drain() items");
            }
            2 => {
                p.v.retain(|x| *x % 2 == 0);
                p.model.retain(|x| *x % 2 == 0);
            }
            _ => {
                p.v.dedup();
                p.model.dedup();
            }
        }
        same(&p, "truncate/drain/retain/dedup");
    }
}

endpoint! {
    fn capacity_ops(p: Borrow<Sv>, n: Range<0, 64>, op: Range<0, 4>) {
        let n = *n as usize;
        match *op {
            0 => p.v.reserve(n),
            1 => p.v.reserve_exact(n),
            2 => p.v.shrink_to_fit(),
            3 => {
                require(n >= p.v.len(), "grow below len (documented panic)");
                p.v.grow(n)
            }
            _ => {
                p.v.resize(n, 7);
                p.model.resize(n, 7);
            }
        }
        same(&p, "capacity ops");
    }
}

endpoint! {
    fn extend(p: Borrow<Sv>, q: Borrow<Sv>) {
        let other = q.model.clone();
        p.v.extend(other.iter().copied());
        p.model.extend_from_slice(&other);
        same(&p, "extend");
    }
}

endpoint! {
    fn into_vec(p: Take<Sv>) {
        let pair = p.into_inner();
        let model = pair.model.clone();
        check(pair.v.into_vec() == model, "into_vec()");
    }
}
