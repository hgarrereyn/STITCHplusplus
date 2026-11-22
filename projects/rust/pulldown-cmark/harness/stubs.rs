//! pulldown-cmark 0.8.0. Doc = markdown text + Options. Oracles: offsets in bounds / on char
//! boundaries / nested properly, rendering deterministic, push_html == write_html, event
//! stream balanced (every Start has a matching End).
use pulldown_cmark::{html, BrokenLink, CowStr, Event, Options, Parser, Tag};
use stitch::*;

pub struct DocT {
    pub text: String,
    pub opts: Options,
}
object!(Doc, DocT);
datatype!(Markdown);

flags!(OptFlags = [2, 4, 8, 16, 32]); // tables, footnotes, strikethrough, tasklists, smart punct

fn opts_of(bits: i64) -> Options {
    Options::from_bits_truncate(bits as u32)
}

decoder!(Markdown, fn markdown(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        match s.choice(5) {
            0 | 1 => out.push_str(&s.token()),
            2 => out.push_str(&s.str(8)),
            3 => out.push_str(&s.pick(&["\n", "\n\n", " ", "  ", "    ", "> ", "- ", "| "])),
            _ => out.push_str(&s.pick(&["[a]", "[a]: /x", "![i](u)", "*a*", "**b**", "`c`", "[^n]", "[^n]: t"])),
        }
    }
    out
});

fn render(text: &str, opts: Options) -> String {
    let mut s = String::new();
    html::push_html(&mut s, Parser::new_ext(text, opts));
    s
}

fn check_events(text: &str, opts: Options) {
    let mut depth = 0i64;
    let mut stack: Vec<String> = Vec::new();
    for (ev, r) in Parser::new_ext(text, opts).into_offset_iter() {
        check(r.start <= r.end, "range start <= end");
        check(r.end <= text.len(), "range end in bounds");
        check(text.is_char_boundary(r.start), "range start on char boundary");
        check(text.is_char_boundary(r.end), "range end on char boundary");
        let _ = &text[r.clone()];
        match ev {
            Event::Start(t) => { depth += 1; stack.push(format!("{:?}", std::mem::discriminant(&t))); }
            Event::End(_) => { depth -= 1; stack.pop(); check(depth >= 0, "End without Start"); }
            _ => {}
        }
    }
    check(depth == 0, "Start/End balanced");
}

endpoint! {
    fn new_doc(text: Str<512, Markdown>, o: Flags<OptFlags>) -> New<Doc> {
        New::new(DocT { text: text.to_string(), opts: opts_of(o.bits()) })
    }
}

endpoint! {
    fn new_doc_raw(text: Str<256>) -> New<Doc> {
        New::new(DocT { text: text.to_string(), opts: Options::empty() })
    }
}

endpoint! {
    fn set_options(d: Borrow<Doc>, o: Flags<OptFlags>) {
        d.opts = opts_of(o.bits());
    }
}

endpoint! {
    fn append_text(d: Borrow<Doc>, more: Str<128, Markdown>) {
        require(d.text.len() < 4096, "too long");
        d.text.push_str(&more);
    }
}

endpoint! {
    fn insert_text(d: Borrow<Doc>, more: Str<64, Markdown>, at: u16) {
        require(d.text.len() < 4096, "too long");
        let mut i = (at as usize) % (d.text.len() + 1);
        while !d.text.is_char_boundary(i) { i -= 1; }
        d.text.insert_str(i, &more);
    }
}

endpoint! {
    fn render_html(d: Borrow<Doc>) {
        let a = render(&d.text, d.opts);
        let b = render(&d.text, d.opts);
        check(a == b, "render is deterministic");
    }
}

endpoint! {
    fn render_write_html(d: Borrow<Doc>) {
        let a = render(&d.text, d.opts);
        let mut v: Vec<u8> = Vec::new();
        html::write_html(&mut v, Parser::new_ext(&d.text, d.opts)).unwrap();
        check(a.as_bytes() == &v[..], "push_html == write_html");
    }
}

endpoint! {
    fn offsets_valid(d: Borrow<Doc>) {
        check_events(&d.text, d.opts);
    }
}

endpoint! {
    fn offsets_match_plain(d: Borrow<Doc>) {
        let a: Vec<Event> = Parser::new_ext(&d.text, d.opts).collect();
        let b: Vec<Event> = Parser::new_ext(&d.text, d.opts).into_offset_iter().map(|(e, _)| e).collect();
        check(a == b, "offset iter yields the same events");
    }
}

endpoint! {
    fn events_to_html_roundtrip(d: Borrow<Doc>) {
        // collecting then rendering must equal direct rendering
        let evs: Vec<Event> = Parser::new_ext(&d.text, d.opts).collect();
        let mut s = String::new();
        html::push_html(&mut s, evs.into_iter());
        check(s == render(&d.text, d.opts), "collected events render the same");
    }
}

endpoint! {
    fn render_with_broken_links(d: Borrow<Doc>, mode: Range<0, 2>) {
        let text = d.text.clone();
        let n = std::cell::Cell::new(0usize);
        let mut cb = |bl: BrokenLink| -> Option<(CowStr, CowStr)> {
            n.set(n.get() + 1);
            check(bl.span.start <= bl.span.end && bl.span.end <= text.len(), "broken link span in bounds");
            check(text.is_char_boundary(bl.span.start) && text.is_char_boundary(bl.span.end), "broken link span on boundary");
            match *mode {
                0 => None,
                1 => Some((CowStr::from("/u"), CowStr::from(""))),
                _ => Some((CowStr::from(bl.reference.to_string()), CowStr::from("t"))),
            }
        };
        let p = Parser::new_with_broken_link_callback(&d.text, d.opts, Some(&mut cb));
        let mut s = String::new();
        html::push_html(&mut s, p);
    }
}

endpoint! {
    fn broken_link_offsets(d: Borrow<Doc>) {
        let mut cb = |_bl: BrokenLink| -> Option<(CowStr, CowStr)> { Some((CowStr::from("/x"), CowStr::from(""))) };
        let p = Parser::new_with_broken_link_callback(&d.text, d.opts, Some(&mut cb));
        for (_, r) in p.into_offset_iter() {
            check(r.end <= d.text.len() && d.text.is_char_boundary(r.start) && d.text.is_char_boundary(r.end), "offset ok with callback");
        }
    }
}

endpoint! {
    fn heading_levels(d: Borrow<Doc>) {
        for ev in Parser::new_ext(&d.text, d.opts) {
            if let Event::Start(Tag::Heading(n)) = ev { check(n >= 1 && n <= 6, "heading level 1..=6"); }
        }
    }
}

endpoint! {
    fn render_idempotent_html(d: Borrow<Doc>) {
        // rendering the output HTML (as markdown) must not panic either
        let h = render(&d.text, d.opts);
        require(h.len() < 20000, "big");
        let _ = render(&h, d.opts);
    }
}

