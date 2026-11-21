//! Registry, program execution, worker protocol and reproducer CLI (a port of
//! languages/cpp/runtime/src/runtime.cpp). Single-threaded by design.
#![allow(static_mut_refs)]
use crate::cov;
use crate::source::Source;
use crate::{Arg, Borrow, Bytes, Choices, Datatype, File, FlagSet, Flags, New, Object, OneOf, Range, Ret, Src, Str, Take, Value};
use std::any::Any;
use std::collections::HashMap;
use std::fmt::Write as _;
use std::io::{Read, Write};
use std::marker::PhantomData;
use std::os::fd::FromRawFd;
use std::panic::{catch_unwind, AssertUnwindSafe};

const ST_OK: u8 = 1;
const ST_BAIL: u8 = 2;
const ST_SKIP_MISSING: u8 = 3;
const ST_SKIP_INVALID: u8 = 4;
const MAX_NODES: usize = 256;
const STRINGS_SIZE: usize = 65536;

pub struct BailSignal(pub String);
pub struct CheckFailed(pub String);

// ------------------------------------------------------------------------------- specs
pub struct ParamSpec {
    pub kind: &'static str,
    pub ctype: &'static str,
    pub size: usize,
    pub signed: bool,
    pub lo: i64,
    pub hi: i64,
    pub values: Vec<i64>,
    pub datatype: Option<&'static str>,
}

pub enum ArgSpec {
    Obj { ty: usize, take: bool, name: &'static str },
    Param { p: ParamSpec, name: &'static str },
}

pub struct OutSpec {
    pub ty: usize,
    pub optional: bool,
}

pub struct EndpointDef {
    pub name: &'static str,
    pub file: &'static str,
    pub module: &'static str,
    pub args: Vec<ArgSpec>,
    pub outputs: Vec<OutSpec>,
    pub invoke: Box<dyn Fn(&mut Ctx, &mut Store)>,
}

struct ObjType {
    name: &'static str,
    tid: std::any::TypeId,
    drop: Option<Box<dyn Fn(Box<dyn Any>)>>,
}

struct Dt {
    name: String,
    decoder: Option<fn(&mut Source) -> Vec<u8>>,
    dict: Vec<Vec<u8>>,
}

#[derive(Default)]
struct Registry {
    objects: Vec<ObjType>,
    datatypes: Vec<Dt>,
    stubs: Vec<EndpointDef>,
    by_name: HashMap<&'static str, usize>,
}

static mut REG: Option<Registry> = None;

fn reg() -> &'static mut Registry {
    unsafe { REG.get_or_insert_with(Registry::default) }
}

pub fn object_type<T: Object>() -> usize {
    let r = reg();
    let tid = std::any::TypeId::of::<T>();
    if let Some(i) = r.objects.iter().position(|o| o.tid == tid) {
        return i;
    }
    let drop = T::drop_fn().map(|f| Box::new(move |b: Box<dyn Any>| {
        if let Ok(h) = b.downcast::<T::Handle>() {
            f(*h)
        }
    }) as Box<dyn Fn(Box<dyn Any>)>);
    r.objects.push(ObjType { name: T::NAME, tid, drop });
    r.objects.len() - 1
}

fn datatype_idx(name: &str) -> usize {
    let r = reg();
    if let Some(i) = r.datatypes.iter().position(|d| d.name == name) {
        return i;
    }
    r.datatypes.push(Dt { name: name.to_string(), decoder: None, dict: vec![] });
    r.datatypes.len() - 1
}

pub fn register_decoder(name: &str, f: fn(&mut Source) -> Vec<u8>) {
    let i = datatype_idx(name);
    reg().datatypes[i].decoder = Some(f);
}

pub fn register_endpoint(e: EndpointDef) {
    let r = reg();
    if r.by_name.contains_key(e.name) {
        panic!("duplicate stub name '{}'", e.name);
    }
    for a in &e.args {
        if let ArgSpec::Param { p, .. } = a {
            if let Some(d) = p.datatype {
                datatype_idx(d);
            }
        }
    }
    r.by_name.insert(e.name, r.stubs.len());
    r.stubs.push(e);
}

pub trait DecoderOut {
    fn into_bytes(self) -> Vec<u8>;
}
impl DecoderOut for String {
    fn into_bytes(self) -> Vec<u8> {
        self.into_bytes()
    }
}
impl DecoderOut for Vec<u8> {
    fn into_bytes(self) -> Vec<u8> {
        self
    }
}

fn json_str(s: &str) -> String {
    let mut o = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(o, "\\u{:04x}", c as u32);
            }
            c => o.push(c),
        }
    }
    o + "\""
}

fn hex(b: &[u8]) -> String {
    b.iter().map(|c| format!("{c:02x}")).collect()
}

fn harness_rel(file: &str) -> String {
    match file.find("src/") {
        Some(i) if file.contains("stitch_harness") || !file.starts_with('/') => format!("harness/{}", &file[i + 4..]),
        _ => file.to_string(),
    }
}

fn manifest() -> String {
    let r = reg();
    let mut o = String::from("{\"version\":2,\"objects\":[");
    for (i, t) in r.objects.iter().enumerate() {
        let _ = write!(o, "{}{{\"name\":{},\"drop\":{}}}", if i > 0 { "," } else { "" }, json_str(t.name), t.drop.is_some());
    }
    o.push_str("],\"datatypes\":[");
    for (i, d) in r.datatypes.iter().enumerate() {
        let _ = write!(o, "{}{{\"name\":{},\"decoder\":{}}}", if i > 0 { "," } else { "" }, json_str(&d.name), d.decoder.is_some());
    }
    o.push_str("],\"endpoints\":[");
    for (i, e) in r.stubs.iter().enumerate() {
        let _ = write!(o, "{}{{\"name\":{},\"file\":{},\"inputs\":[", if i > 0 { "," } else { "" }, json_str(e.name),
                       json_str(&harness_rel(e.file)));
        let mut n = 0;
        for a in &e.args {
            if let ArgSpec::Obj { ty, take, name } = a {
                let _ = write!(o, "{}{{\"type\":{},\"mode\":\"{}\",\"name\":{}}}", if n > 0 { "," } else { "" },
                               json_str(r.objects[*ty].name), if *take { "take" } else { "borrow" }, json_str(name));
                n += 1;
            }
        }
        o.push_str("],\"params\":[");
        n = 0;
        for a in &e.args {
            if let ArgSpec::Param { p, name } = a {
                let _ = write!(o, "{}{{\"kind\":\"{}\",\"type\":\"{}\",\"size\":{},\"signed\":{}", if n > 0 { "," } else { "" },
                               p.kind, p.ctype, p.size, p.signed);
                if p.kind == "range" {
                    let _ = write!(o, ",\"lo\":{},\"hi\":{}", p.lo, p.hi);
                }
                if !p.values.is_empty() {
                    let v: Vec<String> = p.values.iter().map(|x| x.to_string()).collect();
                    let _ = write!(o, ",\"values\":[{}]", v.join(","));
                }
                let _ = write!(o, ",\"datatype\":{},\"name\":{}}}", p.datatype.map(json_str).unwrap_or("null".into()), json_str(name));
                n += 1;
            }
        }
        o.push_str("],\"outputs\":[");
        for (k, out) in e.outputs.iter().enumerate() {
            let _ = write!(o, "{}{{\"type\":{},\"optional\":{}}}", if k > 0 { "," } else { "" }, json_str(r.objects[out.ty].name), out.optional);
        }
        o.push_str("]}");
    }
    o + "]}"
}

// ------------------------------------------------------------------------------- state
struct Slot {
    ty: usize,
    value: Option<Box<dyn Any>>,
    addr: usize,
    alive: bool,
    gen: u64,
    node: usize,
    out: usize,
    attrs: HashMap<String, Value>,
}

/// Arguments of the running call (used by `Arg::fetch`).
pub struct Ctx {
    objs: Vec<usize>, // slot index per object input, in argument order
    params: Vec<(u8, Vec<u8>)>,
    oi: usize,
    pi: usize,
}

/// Outputs of the running call (filled by `Ret::store`).
pub struct Store {
    outs: Vec<Option<(usize, Box<dyn Any>)>>,
    pub(crate) bail: Option<String>,
}

#[derive(Default)]
struct NodeRes {
    status: u8,
    n_out: u8,
    out_mask: u16,
    reason: u32,
    miss: u32,
    consumed: u32,
}

#[derive(Default)]
struct Rt {
    trace: bool,
    plan: Option<std::fs::File>,
    in_exec: bool,
    cur_node: i64,
    pool: Vec<Slot>,
    obj_of: HashMap<(usize, usize), usize>,
    globals: HashMap<String, i64>,
    pending: Vec<(usize, String, Value)>,
    cur_in: Vec<usize>,
    cur_consumed: u32,
    last_miss: u32,
    gen: u64,
    tmp_files: Vec<std::path::PathBuf>,
    exc_bail: bool,
    plan_src: usize,
    n_nodes: usize,
    current: i32,
    crash_pc: u64,
    res: Vec<NodeRes>,
    strs: Vec<u8>,
    seen: std::collections::HashSet<u32>,
}

static mut RT: Option<Rt> = None;

fn rt() -> &'static mut Rt {
    unsafe { RT.get_or_insert_with(|| Rt { cur_node: -1, current: -1, gen: 1, exc_bail: std::env::var("STITCH_EXCEPTIONS").as_deref() == Ok("bail"), ..Default::default() }) }
}

fn fnv1a(b: &[u8]) -> u32 {
    let mut h: u32 = 2166136261;
    for &c in b {
        h = (h ^ c as u32).wrapping_mul(16777619);
    }
    if h == 0 { 1 } else { h }
}

fn intern(s: &str) -> u32 {
    let r = rt();
    let b = s.as_bytes();
    let h = fnv1a(b);
    if r.seen.contains(&h) {
        return h;
    }
    let b = &b[..b.len().min(512)];
    if r.strs.len() + 6 + b.len() <= STRINGS_SIZE {
        r.strs.extend_from_slice(&h.to_le_bytes());
        r.strs.extend_from_slice(&(b.len() as u16).to_le_bytes());
        r.strs.extend_from_slice(b);
        r.seen.insert(h);
    }
    h
}

pub fn log(msg: &str) {
    if rt().trace {
        eprintln!("[stitch]     log: {msg}");
    }
}

fn slot_of(addr: usize) -> Option<usize> {
    rt().pool.iter().rposition(|s| s.alive && s.addr == addr)
}

fn note_miss(key: &str) {
    if rt().in_exec {
        rt().last_miss = intern(key);
    }
}

pub fn consume(addr: usize) {
    if let Some(i) = slot_of(addr) {
        let r = rt();
        for (k, &s) in r.cur_in.iter().enumerate() {
            if s == i {
                r.cur_consumed |= 1 << k;
            }
        }
    }
}

pub fn attr_set(addr: usize, key: &str, v: Value) {
    match slot_of(addr) {
        Some(i) => {
            rt().pool[i].attrs.insert(key.to_string(), v);
        }
        None => rt().pending.push((addr, key.to_string(), v)),
    }
}

pub fn attr_set_obj(addr: usize, key: &str, target: usize) {
    if let Some(t) = slot_of(target) {
        let g = rt().pool[t].gen;
        attr_set(addr, key, Value::Obj(t, g));
    }
}

pub fn attr_get(addr: usize, key: &str, note: bool) -> Option<Value> {
    let v = match slot_of(addr) {
        Some(i) => rt().pool[i].attrs.get(key).cloned(),
        None => rt().pending.iter().rev().find(|p| p.0 == addr && p.1 == key).map(|p| p.2.clone()),
    };
    if v.is_none() && note {
        note_miss(key);
    }
    v
}

pub fn attr_get_obj(addr: usize, key: &str) -> Option<usize> {
    match attr_get(addr, key, false)? {
        Value::Obj(s, g) => {
            let sl = rt().pool.get(s)?;
            if sl.alive && sl.gen == g { Some(sl.addr) } else { None }
        }
        _ => None,
    }
}

pub fn attr_del(addr: usize, key: &str) {
    if let Some(i) = slot_of(addr) {
        rt().pool[i].attrs.remove(key);
    }
}

pub fn global_set(key: &str, v: i64) {
    rt().globals.insert(key.to_string(), v);
}

pub fn global_get(key: &str) -> Option<i64> {
    let v = rt().globals.get(key).copied();
    if v.is_none() {
        note_miss(key);
    }
    v
}

pub fn temp_file(data: &[u8]) -> std::path::PathBuf {
    static mut N: u64 = 0;
    let dir = std::env::var("STITCH_TMPDIR").unwrap_or_else(|_| std::env::temp_dir().to_string_lossy().into_owned());
    let p = std::path::PathBuf::from(dir).join(format!("stitch-{}-{}", std::process::id(), unsafe {
        N += 1;
        N
    }));
    let _ = std::fs::write(&p, data);
    rt().tmp_files.push(p.clone());
    p
}

fn decode_param(dt: Option<&str>, raw: &[u8], form: u8, max: usize) -> Vec<u8> {
    if form == 1 {
        if let Some(d) = dt.and_then(|n| reg().datatypes.iter().find(|x| x.name == n)) {
            if let Some(f) = d.decoder {
                return run_decoder(f, Source::new(raw, raw.len().max(max), false, 0), Some(max));
            }
        }
    }
    raw[..raw.len().min(max)].to_vec()
}

fn run_decoder(f: fn(&mut Source) -> Vec<u8>, mut s: Source, max: Option<usize>) -> Vec<u8> {
    let mut v = catch_unwind(AssertUnwindSafe(|| f(&mut s))).unwrap_or_default();
    if let Some(m) = max {
        v.truncate(m);
    }
    v
}

// ------------------------------------------------------------------------------- Arg impls
fn le(raw: &[u8], n: usize) -> u64 {
    raw[..raw.len().min(n)].iter().rev().fold(0u64, |v, &c| (v << 8) | c as u64)
}

impl Ctx {
    fn next_param(&mut self) -> (u8, Vec<u8>) {
        let p = self.params.get(self.pi).cloned().unwrap_or((0, vec![]));
        self.pi += 1;
        p
    }
    fn next_obj(&mut self) -> usize {
        let s = self.objs[self.oi];
        self.oi += 1;
        s
    }
}

fn pspec(kind: &'static str, ctype: &'static str, size: usize, signed: bool) -> ParamSpec {
    ParamSpec { kind, ctype, size, signed, lo: 0, hi: 0, values: vec![], datatype: None }
}

macro_rules! int_arg {
    ($t:ty, $ct:expr, $signed:expr) => {
        impl Arg for $t {
            fn spec(name: &'static str) -> ArgSpec {
                ArgSpec::Param { p: pspec("int", $ct, std::mem::size_of::<$t>(), $signed), name }
            }
            fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
                let (_, raw) = ctx.next_param();
                le(&raw, std::mem::size_of::<$t>()) as $t
            }
        }
    };
}
int_arg!(u8, "U8", false);
int_arg!(u16, "U16", false);
int_arg!(u32, "U32", false);
int_arg!(u64, "U64", false);
int_arg!(usize, "U64", false);
int_arg!(i8, "I8", true);
int_arg!(i16, "I16", true);
int_arg!(i32, "I32", true);
int_arg!(i64, "I64", true);
int_arg!(isize, "I64", true);

impl Arg for bool {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: pspec("bool", "Bool", 1, false), name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let (_, raw) = ctx.next_param();
        raw.first().map(|b| b & 1 == 1).unwrap_or(false)
    }
}
impl Arg for f32 {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: pspec("float", "F32", 4, false), name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        f32::from_bits(le(&ctx.next_param().1, 4) as u32)
    }
}
impl Arg for f64 {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: pspec("float", "F64", 8, false), name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        f64::from_bits(le(&ctx.next_param().1, 8))
    }
}
impl<const MAX: usize, T: Datatype> Arg for Str<MAX, T> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { datatype: T::NAME, ..pspec("str", "Str", MAX, false) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let (form, raw) = ctx.next_param();
        Str(String::from_utf8_lossy(&decode_param(T::NAME, &raw, form, MAX)).into_owned(), PhantomData)
    }
}
impl<const MAX: usize, T: Datatype> Arg for Bytes<MAX, T> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { datatype: T::NAME, ..pspec("bytes", "Bytes", MAX, false) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let (form, raw) = ctx.next_param();
        Bytes(decode_param(T::NAME, &raw, form, MAX), PhantomData)
    }
}
impl<const MAX: usize, T: Datatype> Arg for File<MAX, T> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { datatype: T::NAME, ..pspec("file", "File", MAX, false) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let (form, raw) = ctx.next_param();
        File(temp_file(&decode_param(T::NAME, &raw, form, MAX)), PhantomData)
    }
}
impl<const LO: i64, const HI: i64> Arg for Range<LO, HI> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { lo: LO, hi: HI, ..pspec("range", "Range", 8, true) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let v = le(&ctx.next_param().1, 8) as i64;
        if v >= LO && v <= HI {
            return Range(v);
        }
        let span = (HI as u64).wrapping_sub(LO as u64).wrapping_add(1);
        let off = (v as u64).wrapping_sub(LO as u64);
        Range((LO as u64).wrapping_add(if span != 0 { off % span } else { off }) as i64)
    }
}
fn oneof_index(raw: &[u8], n: usize) -> usize {
    let v = le(raw, 8) as i64;
    if v >= 0 && (v as usize) < n { v as usize } else { ((v as u64) % n as u64) as usize }
}
impl<C: Choices> Arg for OneOf<C> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { values: (0..C::values().len() as i64).collect(), ..pspec("oneof", "OneOf", 8, false) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let vals = C::values();
        OneOf(vals[oneof_index(&ctx.next_param().1, vals.len())].clone())
    }
}
impl<F: FlagSet> Arg for Flags<F> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: ParamSpec { values: F::FLAGS.to_vec(), ..pspec("flags", "Flags", 8, false) }, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let mask = F::FLAGS.iter().fold(0i64, |m, f| m | f);
        Flags::new(le(&ctx.next_param().1, 8) as i64 & mask)
    }
}
impl<const MAX: usize> Arg for Src<MAX> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Param { p: pspec("source", "Source", MAX, false), name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let (_, raw) = ctx.next_param();
        let mut s = Source::new(&raw[..raw.len().min(MAX)], MAX, false, 0);
        let r = rt();
        if r.plan.is_some() && r.in_exec && r.cur_node >= 0 {
            let (node, sid) = (r.cur_node, r.plan_src);
            r.plan_src += 1;
            s.log = Some(Box::new(move |op, v, is_bytes| plan_draw(node, sid, op, &v, is_bytes)));
        }
        Src(s)
    }
}
impl<T: Object> Arg for Borrow<T> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Obj { ty: object_type::<T>(), take: false, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let s = ctx.next_obj();
        let b = rt().pool[s].value.as_mut().unwrap().downcast_mut::<T::Handle>().unwrap();
        Borrow { ptr: b as *mut T::Handle, _t: PhantomData }
    }
}
impl<T: Object> Arg for Take<T> {
    fn spec(name: &'static str) -> ArgSpec {
        ArgSpec::Obj { ty: object_type::<T>(), take: true, name }
    }
    fn fetch(ctx: &mut Ctx, _i: usize) -> Self {
        let s = ctx.next_obj();
        Take(rt().pool[s].value.take().unwrap().downcast::<T::Handle>().unwrap())
    }
}

// ------------------------------------------------------------------------------- Ret impls
pub trait RetItem {
    fn spec() -> OutSpec;
    fn put(self) -> Option<(usize, Box<dyn Any>)>;
}
impl<T: Object> RetItem for New<T> {
    fn spec() -> OutSpec {
        OutSpec { ty: object_type::<T>(), optional: false }
    }
    fn put(self) -> Option<(usize, Box<dyn Any>)> {
        let addr = &*self.0 as *const T::Handle as usize;
        Some((addr, self.0 as Box<dyn Any>))
    }
}
impl<T: Object> RetItem for Option<New<T>> {
    fn spec() -> OutSpec {
        OutSpec { ty: object_type::<T>(), optional: true }
    }
    fn put(self) -> Option<(usize, Box<dyn Any>)> {
        self.and_then(|n| n.put())
    }
}
impl Ret for () {
    fn outputs() -> Vec<OutSpec> {
        vec![]
    }
    fn store(self, _s: &mut Store) {}
}
impl<T: Object> Ret for New<T> {
    fn outputs() -> Vec<OutSpec> {
        vec![<Self as RetItem>::spec()]
    }
    fn store(self, s: &mut Store) {
        s.outs = vec![self.put()];
    }
}
impl<T: Object> Ret for Option<New<T>> {
    fn outputs() -> Vec<OutSpec> {
        vec![<Self as RetItem>::spec()]
    }
    fn store(self, s: &mut Store) {
        s.outs = vec![self.put()];
    }
}
impl<A: RetItem, B: RetItem> Ret for (A, B) {
    fn outputs() -> Vec<OutSpec> {
        vec![A::spec(), B::spec()]
    }
    fn store(self, s: &mut Store) {
        s.outs = vec![self.0.put(), self.1.put()];
    }
}
impl<A: RetItem, B: RetItem, C: RetItem> Ret for (A, B, C) {
    fn outputs() -> Vec<OutSpec> {
        vec![A::spec(), B::spec(), C::spec()]
    }
    fn store(self, s: &mut Store) {
        s.outs = vec![self.0.put(), self.1.put(), self.2.put()];
    }
}
impl<R: Ret, E: std::fmt::Display> Ret for Result<R, E> {
    fn outputs() -> Vec<OutSpec> {
        R::outputs()
    }
    fn store(self, s: &mut Store) {
        match self {
            Ok(r) => r.store(s),
            Err(e) => {
                let msg: String = format!("error: {e}").chars().take(80).collect();
                s.bail = Some(msg.chars().map(|c| if c.is_ascii_digit() { 'N' } else { c }).collect());
            }
        }
    }
}

// ------------------------------------------------------------------------------- panics
thread_local! {
    static LAST_PANIC: std::cell::RefCell<Option<(String, String)>> = const { std::cell::RefCell::new(None) };
}

fn install_panic_hook() {
    std::panic::set_hook(Box::new(|info| {
        let p = info.payload();
        if p.downcast_ref::<BailSignal>().is_some() {
            return;
        }
        let msg = if let Some(c) = p.downcast_ref::<CheckFailed>() {
            format!("\u{0}check:{}", c.0)
        } else if let Some(s) = p.downcast_ref::<&str>() {
            s.to_string()
        } else if let Some(s) = p.downcast_ref::<String>() {
            s.clone()
        } else {
            "Box<dyn Any>".to_string()
        };
        let mut msg = msg;
        if let Some(l) = info.location() {
            let _ = write!(msg, "\u{1}{}:{}", l.file(), l.line());
        }
        let bt = std::backtrace::Backtrace::force_capture().to_string();
        LAST_PANIC.with(|c| *c.borrow_mut() = Some((msg, bt)));
    }));
}

/// Innermost-first (function, location) from a std backtrace, without std and STITCH frames.
fn frames(bt: &str, loc: Option<&str>) -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = vec![];
    let lines: Vec<&str> = bt.lines().collect();
    let mut i = 0;
    while i < lines.len() {
        let l = lines[i].trim();
        i += 1;
        let Some((num, func)) = l.split_once(": ") else { continue };
        if num.parse::<usize>().is_err() {
            continue;
        }
        let mut at = String::new();
        if i < lines.len() && lines[i].trim().starts_with("at ") {
            at = lines[i].trim()[3..].to_string();
            i += 1;
        }
        let f = strip_hash(func.trim());
        let runtime = at.starts_with("/stitch/rust/") || f.starts_with("stitch::");
        if runtime && !out.is_empty() {
            break; // back in the runtime that called the stub
        }
        let std_frame = at.starts_with("/rustc/") || at.contains("/library/") || f.starts_with("std::") || f.starts_with("core::")
            || f.starts_with("alloc::") || f.starts_with("rust_begin_unwind") || f.starts_with("__rust") || f.contains("panic_fmt")
            || f.starts_with("<alloc::") || f.starts_with("<core::") || f.starts_with("<std::");
        if runtime || std_frame || at.is_empty() || f.contains("__stitch_register") {
            continue;
        }
        let at = at.rsplit_once(':').map(|x| x.0.to_string()).unwrap_or(at); // drop the column
        out.push((f.to_string(), harness_path(&at)));
    }
    if out.is_empty() {
        if let Some(l) = loc {
            out.push(("<panic>".into(), l.to_string()));
        }
    }
    out
}

fn strip_hash(f: &str) -> String {
    // legacy mangling suffix: ::h0123456789abcdef
    match f.rfind("::h") {
        Some(i) if f.len() - i == 19 && f[i + 3..].chars().all(|c| c.is_ascii_hexdigit()) => f[..i].to_string(),
        _ => f.to_string(),
    }
}

/// The crate is a snapshot of /project/harness: report harness paths.
fn harness_path(at: &str) -> String {
    match at.find("/app/crate/src/") {
        Some(i) => format!("/project/harness/{}", &at[i + "/app/crate/src/".len()..]),
        None => at.to_string(),
    }
}

/// `quoted data` in a panic message becomes `…` (identifiers such as `Option::unwrap()` stay).
fn blank_quoted(msg: &str) -> String {
    let mut out = String::new();
    let mut parts = msg.split('`');
    if let Some(first) = parts.next() {
        out.push_str(first);
    }
    for (i, p) in parts.enumerate() {
        out.push('`');
        let code = p.chars().all(|c| c.is_ascii_alphanumeric() || "_:()<>&*, ".contains(c)) && p.len() <= 40;
        out.push_str(if i % 2 == 0 && !code { "…" } else { p });
    }
    out
}

fn panic_kind(msg: &str) -> String {
    let msg = blank_quoted(msg);
    let msg = msg.as_str();
    let head = msg.split(": ").next().unwrap_or(msg);
    let k: String = head.chars().take(60).map(|c| if c.is_ascii_digit() { 'N' } else { c }).collect();
    let mut out = String::new();
    let mut last_n = false;
    for c in k.chars() {
        if c == 'N' && last_n {
            continue;
        }
        last_n = c == 'N';
        out.push(c);
    }
    out.trim().to_string()
}

fn report(ep: &str, msg: &str, bt: &str) {
    let r = rt();
    let (msg, loc) = match msg.split_once('\u{1}') {
        Some((m, l)) => (m.to_string(), Some(l.to_string())),
        None => (msg.to_string(), None),
    };
    let fr = frames(bt, loc.as_deref());
    if let Some(what) = msg.strip_prefix("\u{0}check:") {
        r.crash_pc = (1u64 << 63) | fnv1a(what.as_bytes()) as u64;
        eprintln!("\n[stitch] CHECK FAILED: {what} (node {})", r.cur_node);
    } else {
        let kind = panic_kind(&msg);
        let top = fr.iter().find(|f| !f.1.contains("/harness/") && !f.1.contains("stitch_harness")).map(|f| f.1.clone()).unwrap_or_default();
        let idx = reg().by_name.get(ep).copied().unwrap_or(0) as u64;
        r.crash_pc = (1u64 << 63) | (idx << 32) | fnv1a(format!("{kind}|{top}").as_bytes()) as u64;
        let m: String = msg.replace('\n', " ").chars().take(500).collect();
        eprintln!("\n[stitch] EXCEPTION in {ep}: {kind}: {m}");
    }
    for (k, (f, at)) in fr.iter().take(24).enumerate() {
        eprintln!("    #{k} 0x0 in {f} {at}");
    }
    eprintln!();
}

// ------------------------------------------------------------------------------- plan mode
fn plan_line(s: &str) {
    if let Some(f) = &mut rt().plan {
        let _ = f.write_all(s.as_bytes());
        let _ = f.write_all(b"\n");
    }
}

fn plan_draw(node: i64, src: usize, op: &str, v: &str, is_bytes: bool) {
    let val = if is_bytes { format!("\"hex\":\"{v}\"") } else { format!("\"v\":{}", json_str(v)) };
    plan_line(&format!("{{\"t\":\"draw\",\"node\":{node},\"src\":{src},\"op\":{},{val}}}", json_str(op)));
}

fn rust_str(b: &[u8]) -> String {
    let s = String::from_utf8_lossy(b);
    format!("{s:?}")
}

/// (description for traces, JSON for plan mode) of one parameter.
fn describe(p: &ParamSpec, raw: &[u8], form: u8) -> (String, String) {
    match p.kind {
        "str" | "bytes" | "file" => {
            let v = decode_param(p.datatype, raw, form, p.size);
            let mut pre = String::new();
            for &c in v.iter().take(160) {
                match c {
                    b'"' | b'\\' => {
                        pre.push('\\');
                        pre.push(c as char)
                    }
                    32..=126 => pre.push(c as char),
                    b'\n' => pre.push_str("\\n"),
                    _ => {
                        let _ = write!(pre, "\\x{c:02x}");
                    }
                }
            }
            let d = format!("{}[{}{}]\"{}\"{}", p.ctype, v.len(), if form == 1 { ",decoded" } else { "" }, pre, if v.len() > 160 { "..." } else { "" });
            let lit = match p.kind {
                "str" => format!("Str::from({})", rust_str(&v)),
                "bytes" => format!("Bytes::from(&{}[..])", byte_lit(&v)),
                _ => format!("File::with(&{}[..])", byte_lit(&v)),
            };
            (d, format!("\"decoded\":{},\"hex\":\"{}\",\"rust\":{}", form == 1, hex(&v), json_str(&lit)))
        }
        "source" => {
            let b = &raw[..raw.len().min(p.size)];
            (format!("Src[{}]{}", raw.len(), hex(&b[..b.len().min(48)])),
             format!("\"hex\":\"{}\",\"rust\":{}", hex(b), json_str(&format!("Src::replay(&{}[..])", byte_lit(b)))))
        }
        _ => {
            let v = le(raw, if p.kind == "int" || p.kind == "float" || p.kind == "bool" { p.size } else { 8 });
            let (d, lit) = match p.kind {
                "bool" => {
                    let b = raw.first().map(|x| x & 1 == 1).unwrap_or(false);
                    (b.to_string(), b.to_string())
                }
                "float" if p.size == 4 => {
                    let f = f32::from_bits(v as u32);
                    (format!("{f}"), float_lit(f as f64, "f32"))
                }
                "float" => {
                    let f = f64::from_bits(v);
                    (format!("{f}"), float_lit(f, "f64"))
                }
                "range" => {
                    let x = v as i64;
                    let x = if x >= p.lo && x <= p.hi {
                        x
                    } else {
                        let span = (p.hi as u64).wrapping_sub(p.lo as u64).wrapping_add(1);
                        let off = (x as u64).wrapping_sub(p.lo as u64);
                        (p.lo as u64).wrapping_add(if span != 0 { off % span } else { off }) as i64
                    };
                    (x.to_string(), format!("Range::new({x})"))
                }
                "oneof" => {
                    let i = oneof_index(raw, p.values.len());
                    (format!("#{i}"), format!("OneOf::index({i})"))
                }
                "flags" => {
                    let m = p.values.iter().fold(0i64, |a, b| a | b);
                    let x = v as i64 & m;
                    (x.to_string(), format!("Flags::new({x})"))
                }
                _ => {
                    let bits = 8 * p.size as u32;
                    let x = if p.signed && p.size < 8 {
                        ((v << (64 - bits)) as i64 >> (64 - bits)).to_string()
                    } else if p.signed {
                        (v as i64).to_string()
                    } else {
                        v.to_string()
                    };
                    let suffix = p.ctype.to_lowercase();
                    (x.clone(), format!("{x}{suffix}"))
                }
            };
            (d.clone(), format!("\"v\":{},\"rust\":{}", json_str(&d), json_str(&lit)))
        }
    }
}

fn byte_lit(b: &[u8]) -> String {
    let mut s = String::from("b\"");
    for &c in b {
        match c {
            b'"' | b'\\' => {
                s.push('\\');
                s.push(c as char)
            }
            32..=126 => s.push(c as char),
            _ => {
                let _ = write!(s, "\\x{c:02x}");
            }
        }
    }
    s + "\""
}

fn float_lit(f: f64, t: &str) -> String {
    if f.is_nan() {
        format!("{t}::NAN")
    } else if f.is_infinite() {
        format!("{t}::{}", if f > 0.0 { "INFINITY" } else { "NEG_INFINITY" })
    } else {
        format!("{f:?}{t}")
    }
}

// ------------------------------------------------------------------------------- execution
struct PNode {
    name: String,
    ins: Vec<(usize, usize)>,
    params: Vec<(u8, Vec<u8>)>,
}

fn parse(data: &[u8]) -> Option<Vec<PNode>> {
    let mut off = 0usize;
    let mut take = |n: usize| -> Option<&[u8]> {
        if off + n > data.len() {
            return None;
        }
        let s = &data[off..off + n];
        off += n;
        Some(s)
    };
    if take(4)? != b"STCH" {
        return None;
    }
    let nn = u32::from_le_bytes(take(4)?.try_into().ok()?) as usize;
    if nn > 100000 {
        return None;
    }
    let mut names = vec![];
    for _ in 0..nn {
        let l = u16::from_le_bytes(take(2)?.try_into().ok()?) as usize;
        names.push(String::from_utf8_lossy(take(l)?).into_owned());
    }
    let n = u32::from_le_bytes(take(4)?.try_into().ok()?) as usize;
    if n > MAX_NODES {
        return None;
    }
    let mut nodes = vec![];
    for _ in 0..n {
        let ni = u32::from_le_bytes(take(4)?.try_into().ok()?) as usize;
        let nin = take(1)?[0];
        let mut ins = vec![];
        for _ in 0..nin {
            let src = u32::from_le_bytes(take(4)?.try_into().ok()?) as usize;
            let out = take(1)?[0] as usize;
            ins.push((src, out));
        }
        let np = take(1)?[0];
        let mut params = vec![];
        for _ in 0..np {
            let form = take(1)?[0];
            let l = u32::from_le_bytes(take(4)?.try_into().ok()?) as usize;
            if l > 16 << 20 {
                return None;
            }
            params.push((form, take(l)?.to_vec()));
        }
        nodes.push(PNode { name: names.get(ni).cloned().unwrap_or_else(|| "?".into()), ins, params });
    }
    Some(nodes)
}

fn results_bytes() -> Vec<u8> {
    let r = rt();
    let mut b = vec![];
    b.extend_from_slice(&(r.n_nodes as u32).to_le_bytes());
    b.extend_from_slice(&r.current.to_le_bytes());
    b.extend_from_slice(&r.crash_pc.to_le_bytes());
    for n in &r.res {
        b.push(n.status);
        b.push(n.n_out);
        b.extend_from_slice(&n.out_mask.to_le_bytes());
        b.extend_from_slice(&n.reason.to_le_bytes());
        b.extend_from_slice(&n.miss.to_le_bytes());
        b.extend_from_slice(&n.consumed.to_le_bytes());
    }
    b.extend_from_slice(&(r.strs.len() as u32).to_le_bytes());
    b.extend_from_slice(&r.strs);
    b.extend_from_slice(&cov::edges_bytes());
    b.extend_from_slice(&cov::cmps_bytes());
    b
}

#[derive(PartialEq)]
enum Outcome {
    Ok,
    Finding,
    Invalid,
}

fn run_program(data: &[u8], trace: bool) -> Outcome {
    let Some(nodes) = parse(data) else {
        let r = rt();
        r.n_nodes = 0;
        r.res.clear();
        if trace {
            eprintln!("[stitch] invalid program");
        }
        return Outcome::Invalid;
    };
    {
        let r = rt();
        r.n_nodes = nodes.len();
        r.current = -1;
        r.crash_pc = 0;
        r.res = (0..nodes.len()).map(|_| NodeRes::default()).collect();
        r.strs.clear();
        r.seen.clear();
        r.pool.clear();
        r.obj_of.clear();
        r.globals.clear();
        r.trace = trace;
        r.in_exec = true;
    }
    cov::begin_exec();
    let out = run_nodes(&nodes);
    let r = rt();
    r.in_exec = false;
    r.cur_node = -1;
    cov::set_in_stub(false);
    for f in r.tmp_files.drain(..) {
        let _ = std::fs::remove_file(f);
    }
    out
}

fn run_nodes(nodes: &[PNode]) -> Outcome {
    for (i, nd) in nodes.iter().enumerate() {
        let r = rt();
        r.current = i as i32;
        r.cur_node = i as i64;
        cov::set_node(i);
        let Some(&ei) = reg().by_name.get(nd.name.as_str()) else {
            r.res[i].status = ST_SKIP_INVALID;
            plan_line(&format!("{{\"t\":\"skip\",\"node\":{i},\"why\":\"unknown endpoint\"}}"));
            if r.trace {
                eprintln!("[stitch] #{i} {}: unknown endpoint, skipped", nd.name);
            }
            continue;
        };
        let e = &reg().stubs[ei];
        let n_in = e.args.iter().filter(|a| matches!(a, ArgSpec::Obj { .. })).count();
        let n_par = e.args.len() - n_in;
        r.res[i].n_out = e.outputs.len() as u8;
        if nd.ins.len() != n_in || nd.params.len() != n_par {
            r.res[i].status = ST_SKIP_INVALID;
            plan_line(&format!("{{\"t\":\"skip\",\"node\":{i},\"why\":\"signature mismatch\"}}"));
            if r.trace {
                eprintln!("[stitch] #{i} {}: signature mismatch, skipped", nd.name);
            }
            continue;
        }
        let mut in_slot = vec![];
        let mut missing = false;
        let mut k = 0;
        for a in &e.args {
            if let ArgSpec::Obj { ty, .. } = a {
                match r.obj_of.get(&nd.ins[k]) {
                    Some(&s) if r.pool[s].alive && r.pool[s].ty == *ty && !in_slot.contains(&s) => in_slot.push(s),
                    _ => missing = true,
                }
                k += 1;
            }
        }
        if missing {
            r.res[i].status = ST_SKIP_MISSING;
            plan_line(&format!("{{\"t\":\"skip\",\"node\":{i},\"why\":\"input not available\"}}"));
            if r.trace {
                eprintln!("[stitch] #{i} {}: input not available, skipped", nd.name);
            }
            continue;
        }
        let (mut oi, mut pi) = (0, 0);
        let mut parts = vec![];
        let mut plan_args = vec![];
        for a in &e.args {
            match a {
                ArgSpec::Obj { ty, take, name } => {
                    let (s, o) = nd.ins[oi];
                    parts.push(format!("{name}={}o{s}.{o}", if *take { "take " } else { "" }));
                    plan_args.push(format!("{{\"obj\":[{s},{o}],\"type\":{},\"take\":{take},\"name\":{}}}", json_str(reg().objects[*ty].name), json_str(name)));
                    oi += 1;
                }
                ArgSpec::Param { p, name } => {
                    let (form, raw) = &nd.params[pi];
                    let (d, j) = describe(p, raw, *form);
                    parts.push(format!("{name}={d}"));
                    plan_args.push(format!("{{\"kind\":\"{}\",\"size\":{},\"name\":{},{j}}}", p.kind, p.size, json_str(name)));
                    pi += 1;
                }
            }
        }
        if r.trace {
            eprintln!("[stitch] #{i} {}({})", nd.name, parts.join(", "));
        }
        if r.plan.is_some() {
            let outs: Vec<String> = e.outputs.iter().map(|o| json_str(reg().objects[o.ty].name)).collect();
            plan_line(&format!("{{\"t\":\"call\",\"node\":{i},\"ep\":{},\"module\":{},\"args\":[{}],\"outs\":[{}],\"multi\":{}}}",
                               json_str(e.name), json_str(e.module), plan_args.join(","), outs.join(","), e.outputs.len() > 1));
            r.plan_src = 0;
        }
        r.cur_in = in_slot.clone();
        r.cur_consumed = 0;
        r.last_miss = 0;
        r.pending.clear();
        let mut ctx = Ctx { objs: in_slot.clone(), params: nd.params.clone(), oi: 0, pi: 0 };
        let mut store = Store { outs: vec![], bail: None };
        LAST_PANIC.with(|c| *c.borrow_mut() = None);
        cov::set_in_stub(true);
        let res = catch_unwind(AssertUnwindSafe(|| (e.invoke)(&mut ctx, &mut store)));
        cov::set_in_stub(false);
        let r = rt();
        let mut bail_reason: Option<String> = None;
        match res {
            Ok(()) => {
                if let Some(b) = store.bail.take() {
                    bail_reason = Some(b);
                }
            }
            Err(p) => {
                if let Some(b) = p.downcast_ref::<BailSignal>() {
                    bail_reason = Some(b.0.clone());
                } else if r.exc_bail && p.downcast_ref::<CheckFailed>().is_none() {
                    bail_reason = Some("panic".into());
                } else {
                    let (msg, bt) = LAST_PANIC.with(|c| c.borrow_mut().take()).unwrap_or_else(|| ("panic".into(), String::new()));
                    report(e.name, &msg, &bt);
                    return Outcome::Finding;
                }
            }
        }
        r.res[i].status = if bail_reason.is_some() { ST_BAIL } else { ST_OK };
        for (k, &s) in in_slot.iter().enumerate() {
            let take = e.args.iter().filter_map(|a| if let ArgSpec::Obj { take, .. } = a { Some(*take) } else { None }).nth(k).unwrap_or(false);
            if take || r.cur_consumed & (1 << k) != 0 || r.pool[s].value.is_none() {
                r.pool[s].alive = false;
                r.pool[s].value = None;
                if !take {
                    r.res[i].consumed |= 1 << k;
                }
            }
        }
        if r.res[i].status == ST_OK {
            for (k, o) in store.outs.drain(..).enumerate() {
                if let Some((addr, b)) = o {
                    let mut attrs = HashMap::new();
                    for (a, key, v) in &r.pending {
                        if *a == addr {
                            attrs.insert(key.clone(), v.clone());
                        }
                    }
                    let gen = r.gen;
                    r.gen += 1;
                    r.pool.push(Slot { ty: e.outputs[k].ty, value: Some(b), addr, alive: true, gen, node: i, out: k, attrs });
                    r.obj_of.insert((i, k), r.pool.len() - 1);
                    r.res[i].out_mask |= 1 << k;
                }
            }
            if e.outputs.iter().enumerate().any(|(k, o)| !o.optional && r.res[i].out_mask & (1 << k) == 0) {
                r.res[i].status = ST_BAIL;
                bail_reason = Some("returned no object".into());
            }
        }
        if r.res[i].status == ST_BAIL {
            r.res[i].reason = intern(bail_reason.as_deref().unwrap_or("bail"));
            r.res[i].miss = r.last_miss;
        }
        if r.plan.is_some() {
            let outs: Vec<String> = (0..e.outputs.len()).filter(|k| r.res[i].out_mask & (1 << k) != 0).map(|k| k.to_string()).collect();
            let cons: Vec<String> = (0..in_slot.len()).filter(|k| r.res[i].consumed & (1 << k) != 0).map(|k| k.to_string()).collect();
            let st = if r.res[i].status == ST_OK {
                "\"status\":\"ok\"".to_string()
            } else {
                format!("\"status\":\"bail\",\"reason\":{}", json_str(bail_reason.as_deref().unwrap_or("bail")))
            };
            plan_line(&format!("{{\"t\":\"done\",\"node\":{i},{st},\"outs\":[{}],\"consumed\":[{}]}}", outs.join(","), cons.join(",")));
        }
        if r.trace {
            if r.res[i].status == ST_OK {
                let outs: String = (0..e.outputs.len()).filter(|k| r.res[i].out_mask & (1 << k) != 0).map(|k| format!(" o{i}.{k}")).collect();
                eprintln!("[stitch]     -> ok{}", if outs.is_empty() { String::new() } else { format!(" produced{outs}") });
            } else {
                eprintln!("[stitch]     -> bail {:?}", bail_reason.as_deref().unwrap_or("bail"));
            }
        }
    }
    let r = rt();
    r.current = nodes.len() as i32;
    r.cur_node = nodes.len() as i64;
    cov::set_node(nodes.len());
    let drops: Vec<usize> = (0..r.pool.len()).rev().filter(|&k| r.pool[k].alive && r.pool[k].value.is_some() && reg().objects[r.pool[k].ty].drop.is_some()).collect();
    if r.plan.is_some() {
        let objs: Vec<String> = drops.iter().map(|&k| format!("[{},{}]", r.pool[k].node, r.pool[k].out)).collect();
        plan_line(&format!("{{\"t\":\"drops\",\"objs\":[{}]}}", objs.join(",")));
    }
    for k in drops {
        let v = rt().pool[k].value.take().unwrap();
        let ty = rt().pool[k].ty;
        let _ = catch_unwind(AssertUnwindSafe(|| (reg().objects[ty].drop.as_ref().unwrap())(v)));
    }
    let r = rt();
    r.pool.clear();
    r.current = -1;
    plan_line("{\"t\":\"end\"}");
    Outcome::Ok
}

// ------------------------------------------------------------------------------- entry points
fn read_msg(r: &mut impl Read) -> Option<(u8, Vec<u8>)> {
    let mut h = [0u8; 5];
    r.read_exact(&mut h).ok()?;
    let mut p = vec![0u8; u32::from_le_bytes(h[1..5].try_into().unwrap()) as usize];
    r.read_exact(&mut p).ok()?;
    Some((h[0], p))
}

fn send(w: &mut impl Write, tag: u8, p: &[u8]) {
    let mut m = Vec::with_capacity(p.len() + 5);
    m.push(tag);
    m.extend_from_slice(&(p.len() as u32).to_le_bytes());
    m.extend_from_slice(p);
    let _ = w.write_all(&m);
    let _ = w.flush();
}

fn worker() {
    let mut rx = std::io::BufReader::new(unsafe { std::fs::File::from_raw_fd(3) });
    let mut tx = std::io::BufWriter::new(unsafe { std::fs::File::from_raw_fd(4) });
    let m = manifest();
    let mut hello = (cov::map_size() as u32).to_le_bytes().to_vec();
    hello.extend_from_slice(m.as_bytes());
    send(&mut tx, b'H', &hello);
    while let Some((tag, p)) = read_msg(&mut rx) {
        let u32_at = |o: usize| u32::from_le_bytes(p[o..o + 4].try_into().unwrap());
        match tag {
            b'X' => {
                let flags = u32_at(0);
                cov::set_cmplog(flags & 2 != 0);
                let out = run_program(&p[4..], flags & 1 != 0);
                cov::set_cmplog(false);
                send(&mut tx, if out == Outcome::Finding { b'C' } else { b'O' }, &results_bytes());
            }
            b'D' => {
                let (dt, cap) = (u32_at(0) as usize, u32_at(4) as usize);
                let ch = &p[8..];
                let v = match reg().datatypes.get(dt).and_then(|d| d.decoder) {
                    Some(f) => run_decoder(f, Source::new(ch, ch.len().max(cap), false, 0), Some(cap)),
                    None => ch[..ch.len().min(cap)].to_vec(),
                };
                send(&mut tx, b'R', &v);
            }
            b'G' => {
                let dt = u32_at(0) as usize;
                let seed = u64::from_le_bytes(p[4..12].try_into().unwrap());
                let cap = u32_at(12) as usize;
                let pre = &p[16..p.len().min(16 + cap)];
                let mut v = vec![];
                if let Some(d) = reg().datatypes.get(dt) {
                    if let Some(f) = d.decoder {
                        let mut s = Source::new(pre, cap, true, seed);
                        s.dict = d.dict.clone();
                        let _ = catch_unwind(AssertUnwindSafe(|| f(&mut s)));
                        v = s.buf;
                        v.truncate(cap);
                    }
                }
                send(&mut tx, b'R', &v);
            }
            b'S' => {
                let dt = u32_at(0) as usize;
                let mut out = vec![];
                if let Some(f) = reg().datatypes.get(dt).and_then(|d| d.decoder) {
                    let mut s = Source::new(&p[4..], p.len() - 4, false, 0);
                    s.spans = Some(vec![]);
                    let _ = catch_unwind(AssertUnwindSafe(|| f(&mut s)));
                    for x in s.spans.unwrap_or_default() {
                        out.extend_from_slice(&x.to_le_bytes());
                    }
                }
                send(&mut tx, b'R', &out);
            }
            b'T' => {
                let dt = u32_at(0) as usize;
                if let Some(d) = reg().datatypes.get_mut(dt) {
                    let t = p[4..].to_vec();
                    if d.dict.len() < 4096 && !d.dict.contains(&t) {
                        d.dict.push(t);
                    }
                }
            }
            b'P' => {
                let path = String::from_utf8_lossy(&p).into_owned();
                rt().plan = if path.is_empty() { None } else { std::fs::File::create(path).ok() };
            }
            b'Q' => return,
            _ => {}
        }
    }
}

fn repro(args: &[String]) -> i32 {
    let has = |f: &str| args.iter().any(|a| a == f);
    let mut files = vec![];
    let mut timeout = 10u64;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--plan" => {
                i += 1;
                rt().plan = std::fs::File::create(&args[i]).ok();
            }
            "--timeout" => {
                i += 1;
                timeout = args[i].parse().unwrap_or(10);
            }
            "--files-from" => {
                i += 1;
                files.extend(std::fs::read_to_string(&args[i]).unwrap_or_default().lines().filter(|l| !l.trim().is_empty()).map(|l| l.trim().to_string()));
            }
            a if !a.starts_with('-') => files.push(a.to_string()),
            _ => {}
        }
        i += 1;
    }
    if has("--manifest") {
        println!("{}", manifest());
        return 0;
    }
    let trace = has("--trace");
    if has("--each") {
        let (mut n, mut ok, mut crashed, mut invalid) = (0, 0, 0, 0);
        let _ = timeout;
        for f in &files {
            n += 1;
            match std::fs::read(f).map(|d| run_program(&d, false)) {
                Ok(Outcome::Ok) => ok += 1,
                Ok(Outcome::Finding) => crashed += 1,
                _ => invalid += 1,
            }
        }
        println!("{{\"programs\":{n},\"ok\":{ok},\"crashed\":{crashed},\"timeouts\":0,\"invalid\":{invalid}}}");
        return 0;
    }
    for f in &files {
        if trace {
            eprintln!("[stitch] running {f}");
        }
        let Ok(data) = std::fs::read(f) else {
            eprintln!("[stitch] cannot open {f}");
            return 2;
        };
        match run_program(&data, trace) {
            Outcome::Finding => return 1,
            Outcome::Invalid => return 2,
            Outcome::Ok => {}
        }
    }
    0
}

/// Entry point of the generated worker binary (`stitch_harness::__stitch_register()` first).
pub fn main() {
    install_panic_hook();
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().map(|a| a == "worker").unwrap_or(false) {
        worker();
        std::process::exit(0);
    }
    std::process::exit(repro(&args));
}

// ------------------------------------------------------------------------------- standalone reproducers
static mut CUR: (i64, &str) = (-1, "?");

/// The outputs of a standalone call: `let mut doc0 = r0.get::<Doc>(0);`
pub struct Outs(Vec<Option<(usize, Box<dyn Any>)>>);
impl Outs {
    pub fn get<T: Object>(&mut self, k: usize) -> New<T> {
        let (_, b) = self.0.get_mut(k).and_then(|o| o.take()).expect("this output was not produced");
        New(b.downcast::<T::Handle>().expect("output type"))
    }
}

/// Run call #n (stub `name`) of a standalone reproducer. A bail means the reproducer diverged.
pub fn call<R: Ret>(n: i64, name: &'static str, f: impl FnOnce() -> R) -> Outs {
    unsafe { CUR = (n, name) };
    let mut s = Store { outs: vec![], bail: None };
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r.store(&mut s),
        Err(p) => match p.downcast::<BailSignal>() {
            Ok(b) => s.bail = Some(b.0),
            Err(p) => std::panic::resume_unwind(p),
        },
    }
    if let Some(b) = s.bail {
        eprintln!("\n[repro] DIVERGED at call #{n} {name}: bailed: {b:?} (it succeeded in the original run)");
        std::process::exit(99);
    }
    if std::env::var_os("STITCH_REPRO_TRACE").is_some() {
        eprintln!("[repro] #{n} {name} -> ok");
    }
    Outs(s.outs)
}

/// Run a call that bailed in the recorded run.
pub fn call_bails<R: Ret>(n: i64, name: &'static str, f: impl FnOnce() -> R) {
    unsafe { CUR = (n, name) };
    let mut s = Store { outs: vec![], bail: None };
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r.store(&mut s),
        Err(p) => {
            if p.downcast_ref::<BailSignal>().is_none() {
                std::panic::resume_unwind(p)
            }
        }
    }
}

/// Run a standalone reproducer; a panic is reported the way the fuzzer does.
pub fn run(main: impl FnOnce()) {
    install_panic_hook();
    if catch_unwind(AssertUnwindSafe(main)).is_err() {
        let (msg, bt) = LAST_PANIC.with(|c| c.borrow_mut().take()).unwrap_or_else(|| ("panic".into(), String::new()));
        let (n, name) = unsafe { CUR };
        rt().cur_node = n;
        report(name, &msg, &bt);
        std::process::exit(1);
    }
}

