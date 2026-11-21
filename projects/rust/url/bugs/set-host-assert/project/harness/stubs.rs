//! url (rust-url): WHATWG URL parsing and setters.
//! Oracle: Url::parse(u.as_str()) == u after parsing, joining and every setter.
use stitch::*;
use url::Url;

object!(U, Url);
datatype!(UrlText);

decoder!(UrlText, fn url_text(s: &mut Source) -> String {
    let mut out = String::new();
    out += &s.pick(&["http://", "https://", "file://", "file:///", "data:", "mailto:", "x:", "ws://", "", "//"]);
    while s.more() {
        let part = match s.choice(6) {
            0 => s.token(),
            1 => s.pick(&["a", "b.c", "xn--nxasmq6b", "[::1]", "1.2.3.4", "0x7f.1", "%41", "..", ".", "@", ":", "/", "?", "#"]).to_string(),
            2 => format!(":{}", s.range(0, 70000)),
            _ => s.str(6),
        };
        out += &part;
    }
    out
});

fn roundtrip(u: &Url, what: &str) {
    match Url::parse(u.as_str()) {
        Ok(again) => {
            check(&again == u, &format!("parse(as_str()) == url after {what}"));
            check(again.as_str() == u.as_str(), &format!("serialization is stable after {what}"));
        }
        Err(_) => check(false, &format!("as_str() parses after {what}")),
    }
}

endpoint! {
    fn parse(text: Str<256, UrlText>) -> Result<New<U>, url::ParseError> {
        let u = Url::parse(&text)?;
        roundtrip(&u, "parse");
        Ok(New::new(u))
    }
}

endpoint! {
    fn join(base: Borrow<U>, rel: Str<128, UrlText>) -> Result<New<U>, url::ParseError> {
        let u = base.join(&rel)?;
        roundtrip(&u, "join");
        Ok(New::new(u))
    }
}

endpoint! {
    fn set(u: Borrow<U>, what: Range<0, 8>, value: Str<64, UrlText>, port: Range<0, 70000>) {
        let ok = match *what {
            0 => u.set_scheme(&value).is_ok(),
            1 => u.set_host(Some(&value)).is_ok(),
            2 => { u.set_path(&value); true }
            3 => { u.set_query(Some(&value)); true }
            4 => { u.set_fragment(Some(&value)); true }
            5 => u.set_username(&value).is_ok(),
            6 => u.set_password(Some(&value)).is_ok(),
            7 => u.set_port(if *port > 65535 { None } else { Some(*port as u16) }).is_ok(),
            _ => u.set_ip_host(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, (*port % 256) as u8))).is_ok(),
        };
        require(ok, "setter refused (documented)");
        roundtrip(&u, &format!("setter {}", *what));
    }
}

endpoint! {
    fn clear(u: Borrow<U>, what: Range<0, 3>) {
        match *what {
            0 => u.set_query(None),
            1 => u.set_fragment(None),
            2 => { let _ = u.set_host(None); }
            _ => { let _ = u.set_password(None); }
        }
        roundtrip(&u, "clear");
    }
}

endpoint! {
    fn path_segments(u: Borrow<U>, seg: Str<32, UrlText>, op: Range<0, 3>) {
        let r = u.path_segments_mut().map(|mut p| {
            match *op {
                0 => { p.push(&seg); }
                1 => { p.pop(); }
                2 => { p.pop_if_empty(); }
                _ => { p.clear(); }
            }
        });
        require(r.is_ok(), "cannot-be-a-base URL");
        roundtrip(&u, "path_segments_mut");
    }
}

endpoint! {
    fn query_pairs(u: Borrow<U>, k: Str<16>, v: Str<16>, clear: bool) {
        {
            let mut q = u.query_pairs_mut();
            if clear { q.clear(); }
            q.append_pair(&k, &v);
        }
        roundtrip(&u, "query_pairs_mut");
        let _: Vec<_> = u.query_pairs().collect();
    }
}

endpoint! {
    fn inspect(u: Borrow<U>) {
        let _ = (u.origin().ascii_serialization(), u.host_str(), u.port_or_known_default(), u.domain());
        if u.scheme() == "file" {
            let _ = u.to_file_path();
        }
    }
}
