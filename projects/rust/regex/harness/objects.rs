//! Object types, data types and decoders shared by the regex, regex-syntax and regex-automata stubs.
use regex::bytes::Regex as BRegex;
use regex::Regex;
use regex_automata::{
    dfa, hybrid,
    meta,
    nfa::thompson::{self, backtrack::BoundedBacktracker, pikevm::PikeVM},
    util::syntax,
};
use stitch::*;

/// A `regex` crate regex compiled four ways: str and bytes, each with the default engines and
/// with a tiny DFA cache (which forces the fallback engines).
pub struct Re {
    pub re: Regex,
    pub slow: Regex,
    pub bytes: Option<BRegex>,
    pub bslow: Option<BRegex>, // built on first use by search_bytes
    pub pattern: String,
    pub opts: u32,
}

/// One set of regex-automata patterns compiled by every engine with the same syntax and NFA
/// configuration. The DFA engines are optional (built by their own stubs; they can refuse).
pub struct Auto {
    pub pats: Vec<String>,
    pub syn: syntax::Config,
    pub th: thompson::Config,
    pub meta: meta::Regex,
    pub implicit_only: bool,
    pub pike: PikeVM,
    pub bt: BoundedBacktracker,
    pub hybrid: Option<hybrid::regex::Regex>,
    pub dense: Option<dfa::regex::Regex>,
    pub sparse: Option<dfa::regex::Regex<dfa::sparse::DFA<Vec<u8>>>>,
    pub onepass: Option<dfa::onepass::DFA>,
}

/// A dense-DFA regex (forward and reverse DFAs) and its sparse twin, built by `auto_dense`.
pub struct Dense {
    pub dense: dfa::regex::Regex,
    pub sparse: Option<dfa::regex::Regex<dfa::sparse::DFA<Vec<u8>>>>,
    pub npats: usize,
}

object!(R, Re);
object!(A, Auto);
object!(D, Dense);
datatype!(Pattern);
datatype!(Haystack);
datatype!(PatList);

decoder!(Pattern, fn pattern(s: &mut Source) -> String {
    fn atom(s: &mut Source, d: u32) -> String {
        match s.choice(if d > 3 { 5 } else { 9 }) {
            0 => s.str(3),
            1 => s.pick(&["a", "b", "ab", "x", "\u{e9}", "\u{3b1}", "\u{1f600}", "\\n", "0", " ", "k", "\u{212a}", "\\xff"]).to_string(),
            2 => s.pick(&[".", "\\w", "\\W", "\\d", "\\s", "\\b", "\\B", "^", "$", "\\A", "\\z", "\\pL", "\\p{Greek}", "\\PN", "\\p{Lu}", "\\x41", "\\x{10FFFF}", "\\u{e9}", "\\b{start}", "\\b{end}", "\\<", "\\>", "\\b{start-half}", "(?-u:\\xFF)", "(?-u:\\b)", "\\p{Emoji}"]).to_string(),
            3 => format!("[{}{}]", if s.coin(0.2) { "^" } else { "" }, s.pick(&["a-z", "0-9", "[:alpha:]", "\\d&&[0-5]", "a-z--aeiou", "\\pL~~\\p{Greek}", "\u{e9}-\u{3c9}", "]", "-a", "^", "\\x00-\\x{10FFFF}", "[:^space:]", "\\p{Any}"])),
            4 => s.token(),
            5 => format!("({})", alt(s, d + 1)),
            6 => format!("(?{}:{})", s.pick(&["i", "m", "s", "U", "x", "-u", "u", "i-s", "P<name>", "", "#", "R", "Rm"]), alt(s, d + 1)),
            7 => format!("(?P<{}>{})", s.pick(&["a", "b", "x1"]), alt(s, d + 1)),
            _ => format!("{}{}", atom(s, d + 1), s.pick(&["*", "+", "?", "*?", "+?", "??", "{2}", "{1,3}", "{0,}", "{3,2}", "{1000}", "{2,}?", "{,3}", "{0}"])),
        }
    }
    fn seq(s: &mut Source, d: u32) -> String {
        let mut out = String::new();
        loop {
            out += &atom(s, d);
            if !s.more() || !s.coin(0.6) { break; }
        }
        out
    }
    fn alt(s: &mut Source, d: u32) -> String {
        let mut out = seq(s, d);
        while s.more() && s.coin(0.25) {
            out += "|";
            out += &seq(s, d);
        }
        out
    }
    alt(s, 0)
});

decoder!(Haystack, fn haystack(s: &mut Source) -> String {
    let mut out = String::new();
    while s.more() {
        out += &match s.choice(4) {
            0 => s.str(8),
            1 => s.token(),
            2 => s.pick(&["a", "ab", "\n", "\r\n", "x y", "\u{e9}", "\u{3b1}\u{3b2}", "\u{1f600}", "0123", "  ", "K", "\u{212a}", "\0"]).to_string(),
            _ => s.pick(&["foo", "bar", "Hello World", "AaBb", "123abc"]).to_string(),
        };
    }
    out
});

decoder!(PatList, fn pat_list(s: &mut Source) -> Vec<String> {
    let mut v = vec![s.draw::<Pattern>()];
    while v.len() < 3 && s.more() {
        v.push(s.draw::<Pattern>());
    }
    v
});

flags!(Opts = [1, 2, 4, 8, 16, 32, 64, 128]);
