//! STITCH++ harness API for Rust.
//!
//! ```ignore
//! use stitch::*;
//! object!(Doc, toml_edit::DocumentMut);
//! datatype!(TomlText);
//!
//! endpoint! {
//!     fn parse(text: Str<4096, TomlText>) -> Result<New<Doc>, toml_edit::TomlError> {
//!         Ok(New::new(text.parse()?))
//!     }
//! }
//! endpoint! {
//!     fn set_int(doc: Borrow<Doc>, key: Str<16>, v: i64) {
//!         doc[key.as_str()] = toml_edit::value(v);
//!     }
//! }
//! ```
//! `Borrow<T>` uses an object, `Take<T>` consumes it, `New<T>` (returned) produces one. Other
//! parameters are fuzzed. `Err` is a bail; a panic is a bug. Reference: `stitch guide rust`.
#![allow(clippy::missing_safety_doc)]

pub mod abi;
mod cov;
pub mod rt;
mod source;

pub use rt::{call, main, run, Outs};
pub use source::Source;
use std::marker::PhantomData;
use std::ops::{Deref, DerefMut};

// ------------------------------------------------------------------------------- objects
/// An object type: declared with `object!(Name, HandleType)`.
pub trait Object: 'static {
    type Handle: 'static;
    const NAME: &'static str;
    fn drop_fn() -> Option<fn(Self::Handle)> {
        None
    }
}

/// A data type marker: declared with `datatype!(Name)`.
pub trait Datatype: 'static {
    const NAME: Option<&'static str>;
}

/// No data type.
pub struct Plain;
impl Datatype for Plain {
    const NAME: Option<&'static str> = None;
}

/// Uses an object; it stays available. Dereferences to the handle.
pub struct Borrow<T: Object> {
    ptr: *mut T::Handle,
    _t: PhantomData<T>,
}
impl<T: Object> Deref for Borrow<T> {
    type Target = T::Handle;
    fn deref(&self) -> &T::Handle {
        unsafe { &*self.ptr }
    }
}
impl<T: Object> DerefMut for Borrow<T> {
    fn deref_mut(&mut self) -> &mut T::Handle {
        unsafe { &mut *self.ptr }
    }
}

/// Consumes the object.
pub struct Take<T: Object>(pub(crate) Box<T::Handle>);
impl<T: Object> Take<T> {
    pub fn into_inner(self) -> T::Handle {
        *self.0
    }
}
impl<T: Object> Deref for Take<T> {
    type Target = T::Handle;
    fn deref(&self) -> &T::Handle {
        &self.0
    }
}
impl<T: Object> DerefMut for Take<T> {
    fn deref_mut(&mut self) -> &mut T::Handle {
        &mut self.0
    }
}

/// A new object, returned by a stub.
pub struct New<T: Object>(pub(crate) Box<T::Handle>);
impl<T: Object> New<T> {
    pub fn new(h: T::Handle) -> Self {
        New(Box::new(h))
    }
    pub fn into_inner(self) -> T::Handle {
        *self.0
    }
}
impl<T: Object> Deref for New<T> {
    type Target = T::Handle;
    fn deref(&self) -> &T::Handle {
        &self.0
    }
}
impl<T: Object> DerefMut for New<T> {
    fn deref_mut(&mut self) -> &mut T::Handle {
        &mut self.0
    }
}

/// Borrow a reproducer's object (standalone reproducers).
pub fn borrow<T: Object>(o: &mut New<T>) -> Borrow<T> {
    Borrow { ptr: &mut *o.0, _t: PhantomData }
}

/// Pass a reproducer's object to a call that consumes it (standalone reproducers).
pub fn take<T: Object>(o: New<T>) -> Take<T> {
    Take(o.0)
}

/// Objects that attributes can be attached to.
pub trait HasHandle {
    fn addr(&self) -> usize;
}
impl<T: Object> HasHandle for Borrow<T> {
    fn addr(&self) -> usize {
        self.ptr as usize
    }
}
impl<T: Object> HasHandle for Take<T> {
    fn addr(&self) -> usize {
        &*self.0 as *const T::Handle as usize
    }
}
impl<T: Object> HasHandle for New<T> {
    fn addr(&self) -> usize {
        &*self.0 as *const T::Handle as usize
    }
}

// ------------------------------------------------------------------------------- parameters
/// A string of at most MAX UTF-8 bytes (invalid bytes replaced), of data type T.
pub struct Str<const MAX: usize, T: Datatype = Plain>(pub String, PhantomData<T>);
impl<const MAX: usize, T: Datatype> Str<MAX, T> {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}
impl<const MAX: usize, T: Datatype> From<&str> for Str<MAX, T> {
    fn from(s: &str) -> Self {
        Str(s.to_string(), PhantomData)
    }
}
impl<const MAX: usize, T: Datatype> Deref for Str<MAX, T> {
    type Target = String;
    fn deref(&self) -> &String {
        &self.0
    }
}

/// Bytes, at most MAX, of data type T.
pub struct Bytes<const MAX: usize, T: Datatype = Plain>(pub Vec<u8>, PhantomData<T>);
impl<const MAX: usize, T: Datatype> From<&[u8]> for Bytes<MAX, T> {
    fn from(b: &[u8]) -> Self {
        Bytes(b.to_vec(), PhantomData)
    }
}
impl<const MAX: usize, T: Datatype> Deref for Bytes<MAX, T> {
    type Target = Vec<u8>;
    fn deref(&self) -> &Vec<u8> {
        &self.0
    }
}

/// A temporary file holding at most MAX fuzzed bytes; dereferences to its path.
pub struct File<const MAX: usize, T: Datatype = Plain>(pub std::path::PathBuf, PhantomData<T>);
impl<const MAX: usize, T: Datatype> File<MAX, T> {
    pub fn with(data: &[u8]) -> Self {
        File(rt::temp_file(data), PhantomData)
    }
}
impl<const MAX: usize, T: Datatype> Deref for File<MAX, T> {
    type Target = std::path::Path;
    fn deref(&self) -> &std::path::Path {
        &self.0
    }
}

/// An integer in [LO, HI].
#[derive(Clone, Copy, Debug)]
pub struct Range<const LO: i64, const HI: i64>(pub i64);
impl<const LO: i64, const HI: i64> Range<LO, HI> {
    pub fn new(v: i64) -> Self {
        Range(v)
    }
    pub fn get(self) -> i64 {
        self.0
    }
}
impl<const LO: i64, const HI: i64> Deref for Range<LO, HI> {
    type Target = i64;
    fn deref(&self) -> &i64 {
        &self.0
    }
}

/// A list of values for `OneOf<C>`: `choices!(Mode: u32 = [1, 2, 4])`.
pub trait Choices: 'static {
    type Value: Clone + std::fmt::Debug;
    fn values() -> Vec<Self::Value>;
}

/// One of the values of C.
pub struct OneOf<C: Choices>(pub C::Value);
impl<C: Choices> OneOf<C> {
    pub fn index(i: usize) -> Self {
        OneOf(C::values()[i].clone())
    }
}
impl<C: Choices> Deref for OneOf<C> {
    type Target = C::Value;
    fn deref(&self) -> &C::Value {
        &self.0
    }
}

/// Flags for `Flags<F>`: `flags!(Opts = [1, 2, 4])`.
pub trait FlagSet: 'static {
    const FLAGS: &'static [i64];
}

/// A bitwise OR of a subset of F's flags.
#[derive(Clone, Copy, Debug)]
pub struct Flags<F: FlagSet>(pub i64, PhantomData<F>);
impl<F: FlagSet> Flags<F> {
    pub fn new(v: i64) -> Self {
        Flags(v, PhantomData)
    }
    pub fn bits(self) -> i64 {
        self.0
    }
}

/// A Source of at most MAX choice bytes, for decisions inside the stub.
pub struct Src<const MAX: usize>(pub Source);
impl<const MAX: usize> Src<MAX> {
    pub fn replay(b: &[u8]) -> Self {
        Src(Source::replay(b))
    }
}
impl<const MAX: usize> Deref for Src<MAX> {
    type Target = Source;
    fn deref(&self) -> &Source {
        &self.0
    }
}
impl<const MAX: usize> DerefMut for Src<MAX> {
    fn deref_mut(&mut self) -> &mut Source {
        &mut self.0
    }
}

// ------------------------------------------------------------------------------- traits used by endpoint!
/// A stub argument: an object input or a fuzzed parameter.
pub trait Arg: Sized {
    fn spec(name: &'static str) -> rt::ArgSpec;
    fn fetch(ctx: &mut rt::Ctx, idx: usize) -> Self;
}

/// A stub's return type: (), New<T>, Option<New<T>>, tuples of those, or Result<_, E>.
pub trait Ret: Sized {
    fn outputs() -> Vec<rt::OutSpec>;
    fn store(self, outs: &mut rt::Store);
}

// ------------------------------------------------------------------------------- control
/// Stop this call; not a bug. The program continues.
pub fn bail(reason: &str) -> ! {
    std::panic::panic_any(rt::BailSignal(reason.to_string()))
}

/// Bail unless cond holds.
pub fn require(cond: bool, reason: &str) {
    if !cond {
        bail(reason)
    }
}

/// An oracle: a false condition is reported as a bug ("check failed: what").
pub fn check(cond: bool, what: &str) {
    if !cond {
        std::panic::panic_any(rt::CheckFailed(what.to_string()))
    }
}

/// Printed by `stitch exec --trace`.
pub fn log(msg: &str) {
    rt::log(msg)
}

/// The library took ownership of a borrowed input in this call.
pub fn consume(obj: &impl HasHandle) {
    rt::consume(obj.addr())
}

/// Attribute values.
#[derive(Clone, Debug)]
pub enum Value {
    Int(i64),
    Str(String),
    Obj(usize, u64),
}

pub fn set_int(obj: &impl HasHandle, key: &str, v: i64) {
    rt::attr_set(obj.addr(), key, Value::Int(v))
}
pub fn set_str(obj: &impl HasHandle, key: &str, v: &str) {
    rt::attr_set(obj.addr(), key, Value::Str(v.to_string()))
}
/// Link to another object (the link dies with it).
pub fn set_obj(obj: &impl HasHandle, key: &str, target: &impl HasHandle) {
    rt::attr_set_obj(obj.addr(), key, target.addr())
}
/// An int attribute. A missing key is reported by `stitch probe`.
pub fn get_int(obj: &impl HasHandle, key: &str) -> Option<i64> {
    match rt::attr_get(obj.addr(), key, true) {
        Some(Value::Int(v)) => Some(v),
        _ => None,
    }
}
pub fn get_str(obj: &impl HasHandle, key: &str) -> Option<String> {
    match rt::attr_get(obj.addr(), key, true) {
        Some(Value::Str(v)) => Some(v),
        _ => None,
    }
}
/// A linked object, or None if it is missing or was consumed.
pub fn get_obj<T: Object>(obj: &impl HasHandle, key: &str) -> Option<Borrow<T>> {
    rt::attr_get_obj(obj.addr(), key).map(|p| Borrow { ptr: p as *mut T::Handle, _t: PhantomData })
}
pub fn has(obj: &impl HasHandle, key: &str) -> bool {
    rt::attr_get(obj.addr(), key, false).is_some()
}
pub fn del(obj: &impl HasHandle, key: &str) {
    rt::attr_del(obj.addr(), key)
}
pub fn global_set(key: &str, v: i64) {
    rt::global_set(key, v)
}
pub fn global_get(key: &str) -> Option<i64> {
    rt::global_get(key)
}

// ------------------------------------------------------------------------------- macros
/// `object!(Doc, toml_edit::DocumentMut);`  `object!(Conn, Conn, drop = |c| c.close());`
#[macro_export]
macro_rules! object {
    ($name:ident, $handle:ty) => {
        pub struct $name;
        impl $crate::Object for $name {
            type Handle = $handle;
            const NAME: &'static str = stringify!($name);
        }
    };
    ($name:ident, $handle:ty, drop = $drop:expr) => {
        pub struct $name;
        impl $crate::Object for $name {
            type Handle = $handle;
            const NAME: &'static str = stringify!($name);
            fn drop_fn() -> Option<fn($handle)> {
                Some($drop)
            }
        }
    };
}

/// `datatype!(TomlText);` (harness/data/TomlText/ holds seeds and *.dict tokens)
#[macro_export]
macro_rules! datatype {
    ($name:ident) => {
        pub struct $name;
        impl $crate::Datatype for $name {
            const NAME: Option<&'static str> = Some(stringify!($name));
        }
    };
}

/// `choices!(Mode: u32 = [1, 2, 4]);` for `OneOf<Mode>`
#[macro_export]
macro_rules! choices {
    ($name:ident : $t:ty = [$($v:expr),+ $(,)?]) => {
        pub struct $name;
        impl $crate::Choices for $name {
            type Value = $t;
            fn values() -> Vec<$t> {
                vec![$($v),+]
            }
        }
    };
}

/// `flags!(Opts = [1, 2, 4]);` for `Flags<Opts>`
#[macro_export]
macro_rules! flags {
    ($name:ident = [$($v:expr),+ $(,)?]) => {
        pub struct $name;
        impl $crate::FlagSet for $name {
            const FLAGS: &'static [i64] = &[$($v as i64),+];
        }
    };
}

/// A decoder for a data type: `decoder!(TomlText, fn toml_text(s: &mut Source) -> String { ... });`
#[macro_export]
macro_rules! decoder {
    ($dt:ident, fn $f:ident ($s:ident : $sty:ty) -> $ret:ty $body:block) => {
        pub fn $f($s: $sty) -> $ret $body
        #[allow(non_snake_case, dead_code, unused_imports)]
        pub mod $f {
            use super::*;
            pub fn __stitch_register() {
                $crate::rt::register_decoder(stringify!($dt), |s| $crate::rt::DecoderOut::into_bytes(super::$f(s)));
            }
        }
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __ret {
    () => { () };
    ($t:ty) => { $t };
}

/// A stub. Parameter and return types are the spec.
#[macro_export]
macro_rules! endpoint {
    ($(#[$m:meta])* $vis:vis fn $name:ident ( $($arg:ident : $ty:ty),* $(,)? ) $(-> $ret:ty)? $body:block) => {
        $(#[$m])*
        #[allow(unused_mut)]
        pub fn $name($(mut $arg: $ty),*) $(-> $ret)? $body
        #[allow(non_snake_case, dead_code, unused_imports)]
        pub mod $name {
            use super::*;
            pub fn __stitch_register() {
                $crate::rt::register_endpoint($crate::rt::EndpointDef {
                    name: stringify!($name),
                    file: file!(),
                    module: module_path!(),
                    args: vec![$( <$ty as $crate::Arg>::spec(stringify!($arg)) ),*],
                    outputs: <$crate::__ret!($($ret)?) as $crate::Ret>::outputs(),
                    invoke: Box::new(|ctx: &mut $crate::rt::Ctx, store: &mut $crate::rt::Store| {
                        let mut __i = 0usize;
                        $( let $arg = <$ty as $crate::Arg>::fetch(ctx, { __i += 1; __i - 1 }); )*
                        let __r = super::$name($($arg),*);
                        <$crate::__ret!($($ret)?) as $crate::Ret>::store(__r, store);
                    }),
                });
            }
        }
    };
}
