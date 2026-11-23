use pulldown_cmark::{Options, Parser};
fn main() {
    let t = "|| |\n-|-";
    for (e, r) in Parser::new_ext(t, Options::ENABLE_TABLES).into_offset_iter() {
        assert!(r.start <= r.end, "range start <= end: {:?} {:?}", e, r);
    }
}
