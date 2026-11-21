//! A tiny key-value store with planted bugs (STITCH++ example).
use std::collections::HashMap;

#[derive(Default)]
pub struct Store {
    data: HashMap<String, String>,
    order: Vec<String>,
}

impl Store {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn put(&mut self, key: &str, value: &str) -> Result<(), String> {
        if key.is_empty() || key.contains('\n') || value.contains('\n') {
            return Err("bad entry".into());
        }
        if !self.data.contains_key(key) {
            self.order.push(key.to_string());
        }
        self.data.insert(key.to_string(), value.to_string());
        Ok(())
    }

    pub fn remove(&mut self, key: &str) {
        if self.data.remove(key).is_some() && !key.starts_with("tmp") {
            self.order.retain(|k| k != key); // BUG: tmp keys stay in the order list
        }
    }

    pub fn dump(&self) -> String {
        self.order.iter().map(|k| format!("{k}={}\n", self.data.get(k).unwrap())).collect() // panics after a tmp removal
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    /// Byte `i` of the value of `key`. BUG: unchecked index (an ASan finding).
    pub fn byte_at(&self, key: &str, i: usize) -> u8 {
        let v = self.data.get(key).map(|s| s.as_bytes()).unwrap_or(b"");
        if v.is_empty() {
            return 0;
        }
        unsafe { std::ptr::read_volatile(v.as_ptr().add(i)) }
    }
}

pub fn load(text: &str) -> Result<Store, String> {
    let mut s = Store::new();
    for line in text.lines().filter(|l| !l.is_empty()) {
        let (k, mut v) = line.split_once('=').ok_or("no '='")?;
        if let Some(rest) = v.strip_prefix("#!") {
            v = rest.split_whitespace().collect::<Vec<_>>()[1]; // BUG: index out of bounds on "#!x"
        }
        s.put(k, v)?;
    }
    Ok(s)
}
