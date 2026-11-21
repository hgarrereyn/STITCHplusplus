//! `Source`: decisions inside decoders and stubs, read from a fuzzer-owned choice buffer.
//! Same encoding as the C++ runtime: little-endian fixed-width integers, 8-byte ranges,
//! 1-byte coin/more, length-prefixed strings.

const INTERESTING: [u64; 29] = [
    0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512, 1000, 1024, 4096, 0x7fff, 0x8000, 0xffff,
    0x10000, 0x7fffffff, 0x80000000, 0xffffffff,
];

pub struct Source {
    pub(crate) buf: Vec<u8>,
    pos: usize,
    cap: usize,
    gen: bool,
    rng: u64,
    pub(crate) dict: Vec<Vec<u8>>,
    pub(crate) spans: Option<Vec<u32>>,
    exhausted: bool,
    pub(crate) log: Option<Box<dyn FnMut(&str, String, bool)>>,
}

impl Source {
    /// Replay recorded choices (standalone reproducers).
    pub fn replay(choices: &[u8]) -> Source {
        Source::new(choices, choices.len(), false, 0)
    }

    pub(crate) fn new(b: &[u8], cap: usize, gen: bool, seed: u64) -> Source {
        Source { buf: b.to_vec(), pos: 0, cap: cap.max(b.len()), gen, rng: seed, dict: vec![], spans: None, exhausted: false, log: None }
    }

    fn rnd(&mut self) -> u64 {
        self.rng = self.rng.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.rng;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }

    fn unit(&mut self) -> f64 {
        (self.rnd() >> 11) as f64 / 9007199254740992.0
    }

    fn draw(&mut self, n: usize, synth: impl FnOnce(&mut Self) -> Vec<u8>) -> Vec<u8> {
        let mut out = vec![0u8; n];
        if self.pos + n <= self.buf.len() {
            out.copy_from_slice(&self.buf[self.pos..self.pos + n]);
            self.pos += n;
            return out;
        }
        if self.gen && self.buf.len() + n <= self.cap {
            self.buf.truncate(self.pos);
            let f = synth(self);
            let k = f.len().min(n);
            out[..k].copy_from_slice(&f[..k]);
            self.buf.extend_from_slice(&out);
            self.pos += n;
            return out;
        }
        if self.pos < self.buf.len() {
            let k = self.buf.len() - self.pos;
            out[..k].copy_from_slice(&self.buf[self.pos..]);
        }
        self.pos = self.buf.len();
        self.exhausted = true;
        out
    }

    fn le(b: &[u8]) -> u64 {
        b.iter().rev().fold(0u64, |v, &c| (v << 8) | c as u64)
    }

    fn to_le(v: u64, n: usize) -> Vec<u8> {
        v.to_le_bytes()[..n].to_vec()
    }

    fn interesting(&mut self, width: usize) -> u64 {
        let mask = if width >= 8 { u64::MAX } else { (1u64 << (8 * width)) - 1 };
        match self.rnd() % 4 {
            0 => mask,
            1 => mask >> 1,
            _ => INTERESTING[(self.rnd() % INTERESTING.len() as u64) as usize] & mask,
        }
    }

    fn uint(&mut self, width: usize) -> u64 {
        let b = self.draw(width, |s| {
            let x = if s.unit() < 0.3 { s.interesting(width) } else { s.rnd() };
            Self::to_le(x, width)
        });
        Self::le(&b)
    }

    fn len_prefix(max: usize) -> usize {
        if max < 256 {
            1
        } else if max < 65536 {
            2
        } else {
            4
        }
    }

    fn synth_len(&mut self, max: usize) -> usize {
        let u = self.unit();
        let l = if u < 0.7 { self.rnd() % 17 } else if u < 0.95 { self.rnd() % 65 } else { self.rnd() % (max as u64 + 1) };
        (l as usize).min(max)
    }

    fn blob(&mut self, max: usize, use_dict: bool) -> Vec<u8> {
        let w = Self::len_prefix(max);
        let p0 = self.pos;
        let out = self.blob_inner(max, use_dict, w);
        if let Some(sp) = &mut self.spans {
            if p0 + w <= self.buf.len() {
                sp.extend_from_slice(&[p0 as u32, w as u32, out.len() as u32]);
            }
        }
        out
    }

    fn blob_inner(&mut self, max: usize, use_dict: bool, w: usize) -> Vec<u8> {
        let mut content: Option<Vec<u8>> = None;
        let raw = Self::le(&self.draw(w, |s| {
            let c = if use_dict && !s.dict.is_empty() && s.unit() < 0.5 {
                let i = (s.rnd() % s.dict.len() as u64) as usize;
                let t = &s.dict[i];
                t[..t.len().min(max)].to_vec()
            } else {
                let l = s.synth_len(max);
                let printable = s.unit() < 0.7;
                (0..l).map(|_| if printable { 32 + (s.rnd() % 95) as u8 } else { s.rnd() as u8 }).collect()
            };
            let len = c.len() as u64;
            content = Some(c);
            Self::to_le(len, w)
        }));
        let len = if raw > max as u64 { (raw % (max as u64 + 1)) as usize } else { raw as usize };
        if let Some(c) = content {
            if self.buf.len() + len <= self.cap {
                self.buf.extend_from_slice(&c[..len]);
                self.pos += len;
                return c[..len].to_vec();
            }
            self.exhausted = true;
            return vec![];
        }
        let avail = self.buf.len().saturating_sub(self.pos);
        let n = len.min(avail);
        let out = self.buf[self.pos..self.pos + n].to_vec();
        self.pos += n;
        if n < len {
            self.exhausted = true;
        }
        out
    }

    fn range_impl(&mut self, mut lo: i64, mut hi: i64) -> i64 {
        if hi < lo {
            std::mem::swap(&mut lo, &mut hi);
        }
        let span = (hi as u64).wrapping_sub(lo as u64).wrapping_add(1);
        let raw = Self::le(&self.draw(8, |s| {
            let u = s.unit();
            let v = if u < 0.05 {
                lo
            } else if u < 0.1 {
                hi
            } else {
                (lo as u64).wrapping_add(if span != 0 { s.rnd() % span } else { s.rnd() }) as i64
            };
            Self::to_le(v as u64, 8)
        })) as i64;
        if raw >= lo && raw <= hi {
            return raw;
        }
        let off = (raw as u64).wrapping_sub(lo as u64);
        (lo as u64).wrapping_add(if span != 0 { off % span } else { off }) as i64
    }

    fn logged<T: std::fmt::Debug>(&mut self, op: impl FnOnce() -> String, v: T) -> T {
        if let Some(l) = &mut self.log {
            l(&op(), format!("{v:?}"), false);
        }
        v
    }

    fn logged_bytes(&mut self, op: String, v: &[u8]) {
        if let Some(l) = &mut self.log {
            l(&op, v.iter().map(|b| format!("{b:02x}")).collect(), true);
        }
    }

    pub fn u8(&mut self) -> u8 { let v = self.uint(1) as u8; self.logged(|| "u8()".into(), v) }
    pub fn u16(&mut self) -> u16 { let v = self.uint(2) as u16; self.logged(|| "u16()".into(), v) }
    pub fn u32(&mut self) -> u32 { let v = self.uint(4) as u32; self.logged(|| "u32()".into(), v) }
    pub fn u64(&mut self) -> u64 { let v = self.uint(8); self.logged(|| "u64()".into(), v) }
    pub fn i8(&mut self) -> i8 { let v = self.uint(1) as i8; self.logged(|| "i8()".into(), v) }
    pub fn i16(&mut self) -> i16 { let v = self.uint(2) as i16; self.logged(|| "i16()".into(), v) }
    pub fn i32(&mut self) -> i32 { let v = self.uint(4) as i32; self.logged(|| "i32()".into(), v) }
    pub fn i64(&mut self) -> i64 { let v = self.uint(8) as i64; self.logged(|| "i64()".into(), v) }
    pub fn f32(&mut self) -> f32 { let v = f32::from_bits(self.uint(4) as u32); self.logged(|| "f32()".into(), v) }
    pub fn f64(&mut self) -> f64 { let v = f64::from_bits(self.uint(8)); self.logged(|| "f64()".into(), v) }

    /// An integer in [lo, hi].
    pub fn range(&mut self, lo: i64, hi: i64) -> i64 {
        let v = self.range_impl(lo, hi);
        self.logged(|| format!("range({lo}, {hi})"), v)
    }

    /// An index in [0, n).
    pub fn choice(&mut self, n: usize) -> usize {
        let v = if n == 0 { 0 } else { self.range_impl(0, n as i64 - 1) as usize };
        self.logged(|| format!("choice({n})"), v)
    }

    /// One of the items.
    pub fn pick<T: Clone>(&mut self, items: &[T]) -> T {
        assert!(!items.is_empty(), "pick() from an empty list");
        let i = self.range_impl(0, items.len() as i64 - 1) as usize;
        let n = items.len();
        self.logged(|| format!("pick({n} items) -> #{i}"), i);
        items[i].clone()
    }

    /// True with probability about p.
    pub fn coin(&mut self, p: f64) -> bool {
        let thr = p * 256.0;
        let b = self.draw(1, |s| {
            let yes = s.unit() < p;
            let r = s.rnd();
            if yes {
                vec![if thr >= 1.0 { (r % thr.min(256.0) as u64) as u8 } else { 0 }]
            } else {
                vec![(thr + (r % (256.0 - thr).max(1.0) as u64) as f64).min(255.0) as u8]
            }
        })[0];
        // a full or exhausted buffer ends coin-driven loops, like more()
        let v = (b as f64) < thr && !self.exhausted;
        self.logged(|| format!("coin({p})"), v)
    }

    /// Loop condition: `while s.more() { ... }`.
    pub fn more(&mut self) -> bool {
        if self.exhausted && !self.gen {
            return self.logged(|| "more()".into(), false);
        }
        let b = self.draw(1, |s| {
            let fill = if s.cap > 0 { s.buf.len() as f64 / s.cap as f64 } else { 1.0 };
            let p = if fill > 0.5 { 0.15 } else { 0.65 };
            let bit = if s.unit() < p { 1 } else { 0 };
            vec![(s.rnd() as u8 & 0xfe) | bit]
        })[0];
        let v = b & 1 == 1 && !self.exhausted;
        self.logged(|| "more()".into(), v)
    }

    pub fn bytes(&mut self, max: usize) -> Vec<u8> {
        let v = self.blob(max, false);
        self.logged_bytes(format!("bytes({max})"), &v);
        v
    }

    pub fn str(&mut self, max: usize) -> String {
        let v = self.blob(max, false);
        self.logged_bytes(format!("str({max})"), &v);
        String::from_utf8_lossy(&v).into_owned()
    }

    /// A token from the data type's dictionaries.
    pub fn token(&mut self) -> String {
        let v = self.blob(255, true);
        self.logged_bytes("token()".into(), &v);
        String::from_utf8_lossy(&v).into_owned()
    }

    pub fn position(&self) -> usize {
        self.pos
    }

    pub fn exhausted(&self) -> bool {
        self.exhausted
    }
}
