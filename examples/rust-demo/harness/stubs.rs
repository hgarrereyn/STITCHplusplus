use stitch::*;

object!(Store, kvrs::Store);
datatype!(KvText);

decoder!(KvText, fn kv_text(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        let k = if s.coin(0.3) { s.token() } else { s.str(8) };
        out += &format!("{k}={}\n", s.str(12));
    }
    out
});

endpoint! {
    fn new_store() -> New<Store> {
        New::new(kvrs::Store::new())
    }
}

endpoint! {
    fn load(text: Str<256, KvText>) -> Result<New<Store>, String> {
        Ok(New::new(kvrs::load(&text)?))
    }
}

endpoint! {
    fn put(st: Borrow<Store>, key: Str<16>, value: Str<32>) -> Result<(), String> {
        st.put(&key, &value)
    }
}

endpoint! {
    fn remove(st: Borrow<Store>, key: Str<16>) {
        st.remove(&key);
    }
}

endpoint! {
    fn byte_at(st: Borrow<Store>, key: Str<16>, i: Range<0, 64>) {
        st.byte_at(&key, *i as usize);
    }
}

endpoint! {
    fn roundtrip(st: Borrow<Store>) {
        let again = kvrs::load(&st.dump());
        require(again.is_ok(), "dump did not load");
        check(again.unwrap().len() == st.len(), "dump/load roundtrip");
    }
}

// A typed data type: Pairs decodes to a Vec of (key, value), drawing each with Pair's decoder.
datatype!(Pair);
datatype!(Pairs);

decoder!(Pair, fn pair(s: &mut Source) -> (String, String) {
    let k = if s.coin(0.3) { s.token() } else { s.str(8) };
    let v = if s.coin(0.5) { s.str(12) } else { format!("{:.2}", s.f64_range(0.0, 100.0)) };
    (k, v)
});

decoder!(Pairs, fn pairs(s: &mut Source) -> Vec<(String, String)> {
    let mut out = vec![];
    while s.more() {
        out.push(s.draw::<Pair>());
    }
    out
});

endpoint! {
    fn put_all(st: Borrow<Store>, kvs: Val<Pairs, 256>) -> Result<(), String> {
        for (k, v) in kvs.iter() {
            st.put(k, v)?;
        }
        Ok(())
    }
}
