//! toml_edit: format-preserving TOML editing.
//! Oracles: parse then print reproduces the input; an edited document prints text that parses
//! and prints the same again.
use stitch::*;
use toml_edit::{value, Array, Document, InlineTable, Item, Table};

object!(Doc, Document);
datatype!(TomlText);

decoder!(TomlText, fn toml_text(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        let key = if s.coin(0.5) { s.token() } else { s.pick(&["a", "b.c", "\"q k\"", "'lit'", "x-y", "1"]).to_string() };
        match s.choice(6) {
            0 => out += &format!("[{key}]\n"),
            1 => out += &format!("[[{key}]]\n"),
            2 => out += &format!("# {}\n", s.str(8)),
            _ => {
                let v = match s.choice(8) {
                    0 => s.i64().to_string(),
                    1 => format!("\"{}\"", s.str(10).replace('\\', "\\\\").replace('"', "\\\"")),
                    2 => s.pick(&["1.5", "-0.0", "inf", "nan", "1e10", "+3.25", "0x1f", "0o17", "1_000"]).to_string(),
                    3 => s.pick(&["true", "false", "1979-05-27", "07:32:00", "1979-05-27T07:32:00Z"]).to_string(),
                    4 => format!("'''{}'''", s.str(8)),
                    5 => format!("[{}, {}]", s.i64(), s.pick(&["\"x\"", "1", "[]", "{}"])),
                    6 => format!("{{ k = {}, s = \"{}\" }}", s.i64(), s.str(4)),
                    _ => s.token(),
                };
                out += &format!("{key} = {v}\n");
            }
        }
    }
    out
});

fn stable(doc: &Document, what: &str) {
    let text = doc.to_string();
    match text.parse::<Document>() {
        Ok(again) => check(again.to_string() == text, &format!("printed document reprints the same after {what}")),
        Err(e) => {
            log(&format!("printed: {text:?}\nerror: {e}"));
            check(false, &format!("printed document parses after {what}"))
        }
    }
}

endpoint! {
    fn parse(text: Str<2048, TomlText>) -> Result<New<Doc>, toml_edit::TomlError> {
        let doc: Document = text.parse()?;
        check(doc.to_string() == *text, "parse/print preserves the text");
        Ok(New::new(doc))
    }
}

endpoint! {
    fn new_doc() -> New<Doc> {
        New::new(Document::new())
    }
}

endpoint! {
    fn set(doc: Borrow<Doc>, path: Str<32>, kind: Range<0, 8>, n: i64, x: f64, s: Str<16>) {
        let keys: Vec<&str> = path.split('.').collect();
        require(keys.iter().all(|k| !k.is_empty()) && keys.len() <= 3, "bad path");
        let v: Item = match *kind {
            0 => value(n),
            1 => value(x),
            2 => value(s.as_str()),
            3 => value(n % 2 == 0),
            4 => { let mut a = Array::new(); a.push(n); a.push(s.as_str()); value(a) }
            5 => { let mut t = InlineTable::new(); t.insert("k", n.into()); value(t) }
            6 => Item::Table(Table::new()),
            7 => Item::ArrayOfTables(toml_edit::ArrayOfTables::new()),
            _ => Item::None,
        };
        let mut cur: &mut Item = doc.as_item_mut();
        for k in &keys[..keys.len() - 1] {
            require(cur.is_table_like(), "not a table");
            cur = &mut cur[*k];
            if cur.is_none() {
                *cur = Item::Table(Table::new());
            }
        }
        require(cur.is_table_like(), "not a table");
        let last = keys[keys.len() - 1];
        if v.is_table() || v.is_array_of_tables() {
            // a new header: drop the old key, whose decor (newlines, comments) was valid for a
            // `key = value` line but not inside `[...]`
            cur.as_table_like_mut().unwrap().remove(last);
        }
        cur[last] = v;
        stable(&doc, "set");
    }
}

endpoint! {
    fn remove(doc: Borrow<Doc>, s: Src<8>) {
        let keys: Vec<String> = doc.as_table().iter().map(|(k, _)| k.to_string()).collect();
        require(!keys.is_empty(), "empty");
        let k = s.pick(&keys);
        doc.remove(&k);
        stable(&doc, "remove");
    }
}

endpoint! {
    fn format_ops(doc: Borrow<Doc>, op: Range<0, 4>) {
        match *op {
            0 => doc.fmt(),
            1 => { doc.as_table_mut().sort_values(); }
            2 => { doc.as_table_mut().set_implicit(true); }
            3 => { doc.as_table_mut().clear(); }
            _ => {}
        }
        stable(&doc, "format ops");
    }
}

endpoint! {
    // decor is raw text the caller keeps valid (documented): whitespace and comments only
    fn decor(doc: Borrow<Doc>, s: Src<8>, which: Range<0, 4>) {
        let prefix = ["", " ", "\t", "\n", "# c\n"][*which as usize];
        let keys: Vec<String> = doc.as_table().iter().map(|(k, _)| k.to_string()).collect();
        require(!keys.is_empty(), "empty");
        let k = s.pick(&keys);
        // a table's key sits inside its header ("[ k ]", "[[ k ]]"): spaces and tabs only there
        let header = doc.as_table().get(&k).map(|i| i.is_table() || i.is_array_of_tables()).unwrap_or(false);
        require(!header || *which <= 2, "newline or comment inside a table header");
        if let Some(mut key) = doc.as_table_mut().key_decor_mut(&k) {
            key.set_prefix(prefix);
        }
        let _ = doc.to_string();
    }
}
