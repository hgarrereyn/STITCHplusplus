//! url (rust-url): WHATWG URL parsing, joining, setters (Url and the WebIDL `quirks` API), path and
//! query editing, hosts, origins, file paths, serde and the re-exported form_urlencoded.
//!
//! Oracles: after every parse, join and setter, `Url::parse(u.as_str()) == u` and the library's own
//! `check_invariants()`; the getters agree with `Position` slicing, with `quirks` and with each
//! other; `options().base_url(b).parse(x) == b.join(x)`; `b.join(b.make_relative(u)) == u`;
//! Host and form_urlencoded print/parse round trips; file path and serde round trips.
use std::borrow::Cow;
use std::cell::RefCell;
use std::path::{Component, PathBuf};
use stitch::*;
use url::form_urlencoded;
use url::{quirks, Host, Origin, Position, Url};

object!(U, Url);
datatype!(UrlText);

decoder!(UrlText, fn url_text(s: &mut Source) -> String {
    let mut out = String::new();
    out += &s.pick(&["http://", "https://", "file://", "file:///", "data:", "mailto:", "x:", "ws://", "", "//", "blob:https://", "ftp://", "non-spec:/"]);
    while s.more() {
        let part = match s.choice(6) {
            0 => s.token(),
            1 => s.pick(&["a", "b.c", "xn--nxasmq6b", "[::1]", "1.2.3.4", "0x7f.1", "%41", "..", ".", "@", ":", "/", "?", "#", "C:", "\\", "%2e", "&", "=", "+"]).to_string(),
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
    if let Err(e) = u.check_invariants() {
        log(&e);
        check(false, &format!("check_invariants() after {what}"));
    }
}

endpoint! {
    fn parse(text: Str<256, UrlText>) -> Result<New<U>, url::ParseError> {
        let r = Url::parse(&text);
        check(r == text.parse::<Url>() && r == Url::try_from(text.as_str()), "parse / FromStr / TryFrom agree");
        let u = r?;
        roundtrip(&u, "parse");
        Ok(New::new(u))
    }
}

endpoint! {
    fn parse_options(text: Str<256, UrlText>, encode: bool) -> Result<New<U>, url::ParseError> {
        let violations = RefCell::new(Vec::new());
        let cb = |v: url::SyntaxViolation| violations.borrow_mut().push(v);
        fn enc(s: &str) -> Cow<'_, [u8]> {
            // a single-byte "legacy" encoding: chars past U+00FF become '?'
            Cow::Owned(s.chars().map(|c| if (c as u32) < 256 { c as u32 as u8 } else { b'?' }).collect())
        }
        let mut opts = Url::options().syntax_violation_callback(Some(&cb));
        if encode {
            opts = opts.encoding_override(Some(&enc));
        }
        let r = opts.parse(&text);
        for v in violations.borrow().iter() {
            check(!v.description().is_empty() && v.to_string() == v.description(), "SyntaxViolation description");
        }
        if !encode {
            check(r == Url::parse(&text), "options().parse agrees with Url::parse");
        }
        let u = r?;
        roundtrip(&u, "options().parse");
        Ok(New::new(u))
    }
}

endpoint! {
    fn parse_with_params(text: Str<128, UrlText>, k: Str<16>, v: Str<16>, n: Range<0, 3>) -> Result<New<U>, url::ParseError> {
        let pairs: Vec<(&str, &str)> = (0..*n).map(|_| (k.as_str(), v.as_str())).collect();
        let u = Url::parse_with_params(&text, &pairs)?;
        let mut expect = Url::parse(&text).unwrap();
        expect.query_pairs_mut().extend_pairs(&pairs);
        check(u == expect, "parse_with_params == parse + query_pairs_mut().extend_pairs");
        roundtrip(&u, "parse_with_params");
        Ok(New::new(u))
    }
}

endpoint! {
    fn join(base: Borrow<U>, rel: Str<128, UrlText>) -> Result<New<U>, url::ParseError> {
        let r = base.join(&rel);
        check(r == Url::options().base_url(Some(&base)).parse(&rel), "join agrees with options().base_url().parse");
        let u = r?;
        roundtrip(&u, "join");
        Ok(New::new(u))
    }
}

endpoint! {
    fn make_relative(base: Borrow<U>, u: Borrow<U>) {
        let rel = base.make_relative(&u);
        log(&format!("make_relative: {rel:?}"));
        require(rel.is_some(), "no relative URL (documented)");
        let rel = rel.unwrap();
        // (make_relative ignores the username and password)
        require(base.username() == u.username() && base.password() == u.password(), "credentials differ");
        match base.join(&rel) {
            Ok(back) => check(back == *u, "base.join(base.make_relative(u)) == u"),
            Err(_) => check(false, "make_relative() result joins"),
        }
    }
}

endpoint! {
    fn set(u: Borrow<U>, what: Range<0, 9>, value: Str<64, UrlText>, port: Range<0, 70000>) {
        let ok = match *what {
            0 => u.set_scheme(&value).is_ok(),
            1 => u.set_host(Some(&value)).is_ok(),
            2 => { u.set_path(&value); true }
            3 => { u.set_query(Some(&value)); true }
            4 => { u.set_fragment(Some(&value)); true }
            5 => u.set_username(&value).is_ok(),
            6 => u.set_password(Some(&value)).is_ok(),
            7 => u.set_port(if *port > 65535 { None } else { Some(*port as u16) }).is_ok(),
            8 => u.set_ip_host(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, (*port % 256) as u8))).is_ok(),
            _ => u.set_ip_host(std::net::IpAddr::V6(std::net::Ipv6Addr::new(0, 0, 0, 0, 0, 0xffff, (*port % 65536) as u16, 1))).is_ok(),
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
    fn quirks_set(u: Borrow<U>, what: Range<0, 9>, value: Str<64, UrlText>) {
        let ok = match *what {
            0 => quirks::set_href(&mut u, &value).is_ok(),
            1 => quirks::set_protocol(&mut u, &value).is_ok(),
            2 => quirks::set_username(&mut u, &value).is_ok(),
            3 => quirks::set_password(&mut u, &value).is_ok(),
            4 => quirks::set_host(&mut u, &value).is_ok(),
            5 => quirks::set_hostname(&mut u, &value).is_ok(),
            6 => quirks::set_port(&mut u, &value).is_ok(),
            7 => { quirks::set_pathname(&mut u, &value); true }
            8 => { quirks::set_search(&mut u, &value); true }
            _ => { quirks::set_hash(&mut u, &value); true }
        };
        require(ok, "setter refused (documented)");
        roundtrip(&u, &format!("quirks setter {}", *what));
    }
}

endpoint! {
    fn path_segments(u: Borrow<U>, seg: Str<32, UrlText>, op: Range<0, 4>) {
        let r = u.path_segments_mut().map(|mut p| {
            match *op {
                0 => { p.push(&seg); }
                1 => { p.pop(); }
                2 => { p.pop_if_empty(); }
                3 => { p.extend(seg.split('/')); }
                _ => { p.clear(); }
            }
        });
        require(r.is_ok(), "cannot-be-a-base URL");
        roundtrip(&u, "path_segments_mut");
    }
}

endpoint! {
    fn query_pairs(u: Borrow<U>, k: Str<16>, v: Str<16>, op: Range<0, 3>, clear: bool) {
        {
            let mut q = u.query_pairs_mut();
            if clear { q.clear(); }
            match *op {
                0 => { q.append_pair(&k, &v); }
                1 => { q.append_key_only(&k); }
                2 => { q.extend_pairs([(&*k, &*v), (&*v, &*k)]); }
                _ => { q.extend_keys_only::<_, &str>([k.as_str(), v.as_str()]); }
            }
        }
        roundtrip(&u, "query_pairs_mut");
        let pairs: Vec<(String, String)> = u.query_pairs().into_owned().collect();
        let (k, v) = (k.to_string(), v.to_string());
        let want: Vec<(String, String)> = match *op {
            0 => vec![(k, v)],
            1 => vec![(k, String::new())],
            2 => vec![(k.clone(), v.clone()), (v, k)],
            _ => vec![(k, String::new()), (v, String::new())],
        };
        // (an empty key-only entry serializes to nothing: the parser skips empty sequences)
        let want: Vec<(String, String)> = match *op {
            1 | 3 => want.into_iter().filter(|(k, _)| !k.is_empty()).collect(),
            _ => want,
        };
        check(pairs.len() >= want.len() && pairs[pairs.len() - want.len()..] == want[..],
              "query_pairs() ends with the appended pairs");
        if clear {
            check(pairs == want, "after clear(), query_pairs() is exactly the appended pairs");
        }
    }
}

fn default_port(scheme: &str) -> Option<u16> {
    // documented on port_or_known_default
    match scheme {
        "http" | "ws" => Some(80),
        "https" | "wss" => Some(443),
        "ftp" => Some(21),
        _ => None,
    }
}

fn check_origin(o: &Origin) {
    check(Origin::new_opaque() != Origin::new_opaque(), "every new opaque origin is unique");
    let ascii = o.ascii_serialization();
    let _ = o.unicode_serialization();
    if !o.is_tuple() {
        check(ascii == "null", "opaque origins serialize as null");
        return;
    }
    match Url::parse(&ascii) {
        Ok(back) => check(back.origin() == *o, "the ASCII serialization of a tuple origin parses to the same origin"),
        Err(_) => check(false, "the ASCII serialization of a tuple origin parses"),
    }
}

endpoint! {
    fn inspect(u: Borrow<U>) {
        let s = u.as_str();
        let scheme = u.scheme();
        check(&u[..] == s, "u[..] == as_str()");
        check(&u[Position::BeforeScheme..Position::AfterScheme] == scheme, "Position: scheme");
        check(&u[Position::BeforeUsername..Position::AfterUsername] == u.username(), "Position: username");
        check(&u[Position::BeforePassword..Position::AfterPassword] == u.password().unwrap_or(""), "Position: password");
        check(&u[Position::BeforeHost..Position::AfterHost] == u.host_str().unwrap_or(""), "Position: host");
        check(u[Position::BeforePort..Position::AfterPort] == u.port().map(|p| p.to_string()).unwrap_or_default(), "Position: port");
        check(&u[Position::BeforePath..Position::AfterPath] == u.path(), "Position: path");
        check(&u[Position::BeforeQuery..Position::AfterQuery] == u.query().unwrap_or(""), "Position: query");
        check(&u[Position::BeforeFragment..Position::AfterFragment] == u.fragment().unwrap_or(""), "Position: fragment");
        check(&u[..Position::AfterFragment] == s && &u[Position::BeforeScheme..] == s, "Position: whole URL");
        if u.has_authority() {
            check(u.authority() == &u[Position::BeforeUsername..Position::AfterPort], "authority()");
        } else {
            check(u.authority().is_empty(), "authority() is empty without an authority");
        }
        check(u.has_authority() == s[scheme.len() + 1..].starts_with("//"), "has_authority()");
        check(u.is_special() == matches!(scheme, "http" | "https" | "ws" | "wss" | "ftp" | "file"), "is_special()");
        let host = u.host();
        check(u.has_host() == host.is_some(), "has_host() == host().is_some()");
        check(u.host_str().map(|h| h.to_string()) == host.as_ref().map(|h| h.to_string()), "host_str() == host().to_string()");
        check(u.domain().is_some() == matches!(host, Some(Host::Domain(_))), "domain() iff a domain host");
        check(u.port_or_known_default() == u.port().or(default_port(scheme)), "port_or_known_default()");
        if let Some(p) = u.port() {
            check(Some(p) != default_port(scheme), "port() is None for the default port");
        }
        match u.path_segments() {
            Some(segs) => {
                check(!u.cannot_be_a_base(), "path_segments() is None for cannot-be-a-base URLs");
                let joined: Vec<&str> = segs.collect();
                check(format!("/{}", joined.join("/")) == u.path(), "path_segments() split path()");
            }
            // (also None for an empty path, e.g. "x://host")
            None => check(u.cannot_be_a_base() || u.path().is_empty(), "path_segments() is Some for hierarchical URLs"),
        }
        let _: Vec<(Cow<str>, Cow<str>)> = u.query_pairs().collect();
        // the WebIDL getters
        check(quirks::href(&u) == s, "quirks::href");
        check(quirks::protocol(&u) == format!("{scheme}:"), "quirks::protocol");
        check(quirks::username(&u) == u.username(), "quirks::username");
        check(quirks::password(&u) == u.password().unwrap_or(""), "quirks::password");
        check(quirks::hostname(&u) == u.host_str().unwrap_or(""), "quirks::hostname");
        check(quirks::port(&u) == u.port().map(|p| p.to_string()).unwrap_or_default(), "quirks::port");
        let want_host = match u.port() { Some(p) => format!("{}:{}", u.host_str().unwrap_or(""), p), None => u.host_str().unwrap_or("").to_string() };
        check(quirks::host(&u) == want_host, "quirks::host");
        check(quirks::pathname(&u) == u.path(), "quirks::pathname");
        let q = u.query().unwrap_or("");
        check(quirks::search(&u) == if q.is_empty() { String::new() } else { format!("?{q}") }, "quirks::search");
        let f = u.fragment().unwrap_or("");
        check(quirks::hash(&u) == if f.is_empty() { String::new() } else { format!("#{f}") }, "quirks::hash");
        check(quirks::origin(&u) == u.origin().ascii_serialization(), "quirks::origin");
        let ic = quirks::internal_components(&u);
        check(ic.scheme_end as usize == scheme.len() && ic.path_start as usize == u[..Position::BeforePath].len(), "internal_components");
        // origin
        let o = u.origin();
        if o.is_tuple() {
            check(o == u.origin(), "a tuple origin equals itself");
        }
        check_origin(&o);
        // conversions
        check(u.to_string() == s && String::from(u.clone()) == s && { #[allow(deprecated)] let x = u.clone().into_string(); x } == s && AsRef::<str>::as_ref(&*u) == s, "Display / into_string / From / AsRef");
        let _ = format!("{:?}", *u);
        if let Some(h) = host {
            let owned: Host<String> = h.to_owned();
            check(owned.to_string() == u.host_str().unwrap(), "Host::to_owned");
            if !matches!(owned, Host::Domain(_)) {
                // (a domain would need DNS)
                let addrs = u.socket_addrs(|| Some(1));
                if let Ok(a) = addrs {
                    check(a.len() == 1 && Some(a[0].port()) == u.port_or_known_default().or(Some(1)), "socket_addrs() of an IP host");
                }
            }
        }
        if scheme == "file" {
            let _ = u.to_file_path();
        }
    }
}

endpoint! {
    fn compare(a: Borrow<U>, b: Borrow<U>) {
        use std::hash::{Hash, Hasher};
        let h = |u: &Url| { let mut s = std::collections::hash_map::DefaultHasher::new(); u.hash(&mut s); s.finish() };
        check((*a == *b) == (a.as_str() == b.as_str()), "Url equality is serialization equality");
        check((*a).cmp(&*b) == a.as_str().cmp(b.as_str()), "Url order is serialization order");
        if *a == *b {
            check(h(&a) == h(&b), "equal URLs hash the same");
        }
    }
}

endpoint! {
    fn host_parse(text: Str<64, UrlText>, opaque: bool) {
        let r = if opaque { Host::parse_opaque(&text) } else { Host::parse(&text) };
        let ascii = url::quirks::domain_to_ascii(&text);
        let unicode = url::quirks::domain_to_unicode(&text);
        if !opaque {
            match &r {
                Ok(Host::Domain(d)) => check(ascii == *d, "domain_to_ascii agrees with Host::parse"),
                _ => check(ascii.is_empty() && unicode.is_empty(), "domain_to_ascii/unicode give '' for non-domains"),
            }
        }
        let h = match r { Ok(h) => h, Err(_) => bail("not a host") };
        let printed = h.to_string();
        log(&format!("host: {printed}"));
        let again = if opaque { Host::parse_opaque(&printed) } else { Host::parse(&printed) };
        check(again.as_ref() == Ok(&h), "Host print -> parse round trip");
    }
}

endpoint! {
    fn from_file_path(text: Str<64, UrlText>, dir: bool) -> New<U> {
        let p = PathBuf::from(format!("/{}", text.as_str()));
        // ("a/.." is kept verbatim, giving a URL that does not reparse to itself; known behavior)
        require(!p.components().any(|c| c == Component::ParentDir), "'..' component");
        let u = match if dir { Url::from_directory_path(&p) } else { Url::from_file_path(&p) } {
            Ok(u) => u,
            Err(()) => bail("refused"),
        };
        check(u.scheme() == "file" && !u.has_host(), "a file: URL without a host");
        if dir {
            check(u.path().ends_with('/'), "from_directory_path ends the path with /");
        }
        match u.to_file_path() {
            Ok(back) => check(back.components().eq(p.components()), "from_file_path -> to_file_path round trip"),
            Err(_) => check(false, "to_file_path() of from_file_path()"),
        }
        roundtrip(&u, "from_file_path");
        New::new(u)
    }
}

endpoint! {
    fn serde_roundtrip(u: Borrow<U>) {
        let json = serde_json::to_string(&*u).unwrap();
        let back: Result<Url, _> = serde_json::from_str(&json);
        check(back.as_ref().ok() == Some(&*u), "serde round trip");
        let mut buf = Vec::new();
        u.serialize_internal(&mut serde_json::Serializer::new(&mut buf)).unwrap();
        let back = Url::deserialize_internal(&mut serde_json::Deserializer::from_slice(&buf));
        check(back.as_ref().ok() == Some(&*u), "serialize_internal -> deserialize_internal round trip");
    }
}

endpoint! {
    fn deserialize(text: Str<256, UrlText>) -> Result<New<U>, serde_json::Error> {
        let u: Url = serde_json::from_value(serde_json::Value::String(text.to_string()))?;
        check(Url::parse(&text).as_ref() == Ok(&u), "Deserialize agrees with parse");
        Ok(New::new(u))
    }
}

datatype!(FormText);

decoder!(FormText, fn form_text(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        out += &match s.choice(5) {
            0 => s.str(6),
            1 => s.pick(&["&", "=", "+", "%", "%2B", "%20", "%zz", "%e9", "%C3%A9", "a", "b", "\u{e9}", " "]).to_string(),
            _ => s.token(),
        };
    }
    out
});

endpoint! {
    fn form_roundtrip(data: Bytes<128, FormText>, k: Str<16>, v: Str<16>, prefix: Str<8>) {
        // parse (any bytes) -> serialize -> parse gives the same pairs
        let pairs: Vec<(String, String)> = form_urlencoded::parse(&data).into_owned().collect();
        let borrowed: Vec<(Cow<str>, Cow<str>)> = form_urlencoded::parse(&data).collect();
        check(borrowed.len() == pairs.len(), "parse and into_owned agree");
        let mut target = prefix.to_string();
        let start = target.len();
        let mut ser = form_urlencoded::Serializer::for_suffix(&mut target, start);
        ser.extend_pairs(&pairs);
        ser.append_pair(&k, &v);
        ser.append_key_only(&v);
        ser.finish();
        let again: Vec<(String, String)> = form_urlencoded::parse(target[start..].as_bytes()).into_owned().collect();
        let mut want = pairs.clone();
        want.push((k.to_string(), v.to_string()));
        if !v.is_empty() { want.push((v.to_string(), String::new())); }  // (an empty key alone serializes to nothing)
        check(again == want, "form_urlencoded serialize -> parse round trip");
        let enc: String = form_urlencoded::byte_serialize(k.as_bytes()).collect();
        let back: Vec<(String, String)> = form_urlencoded::parse(enc.as_bytes()).into_owned().collect();
        check(k.is_empty() || back == vec![(k.to_string(), String::new())], "byte_serialize -> parse round trip");
        let mut ser = form_urlencoded::Serializer::new(String::new());
        ser.extend_keys_only::<_, &str>([k.as_str()]).clear();
        check(ser.finish().is_empty(), "Serializer::clear");
    }
}
