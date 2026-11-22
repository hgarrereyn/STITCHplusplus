import io
import sqlparse
from sqlparse import sql, tokens as T
from stitch import Borrow, New, Take, Str, Range, OneOf, Bool, Src, Val
import stitch
from ._common import Stmt, Tok, SqlText

KW_CASE = OneOf["upper", "lower", "capitalize", None]


# ---- producers ----
@stitch.endpoint
def parse_one(text: Str[1024, SqlText]) -> New[Stmt]:
    r = sqlparse.parse(text)
    stitch.require(r, "no statements")
    return r[0]


@stitch.endpoint
def parse_last(text: Str[1024, SqlText]) -> New[Stmt]:
    r = sqlparse.parse(text)
    stitch.require(r, "no statements")
    return r[-1]


@stitch.endpoint
def parsestream_one(text: Str[1024, SqlText], enc: OneOf[None, "utf-8"]) -> New[Stmt]:
    r = list(sqlparse.parsestream(io.StringIO(text), enc))
    stitch.require(r, "no statements")
    return r[0]


@stitch.endpoint
def parse_check_lossless(text: Str[2048, SqlText]):
    r = sqlparse.parse(text)
    # trailing whitespace after the last statement is dropped by the splitter
    stitch.check("".join(str(x) for x in r).rstrip() == text.rstrip(), "str(parse(x)) == x")
    stitch.check("".join(t.value for st in r for t in st.flatten()).rstrip() == text.rstrip(), "flatten lossless")


@stitch.endpoint
def split_check(text: Str[2048, SqlText]):
    pieces = sqlparse.split(text)
    joined = "".join("".join(p.split()) for p in pieces)
    orig = "".join(text.split())
    stitch.check(joined == orig, "split pieces join back modulo whitespace")
    stitch.check(all(p == p.strip() for p in pieces), "split pieces stripped")


@stitch.endpoint
def split_vs_parse(text: Str[2048, SqlText]):
    a = [str(s).strip() for s in sqlparse.parse(text)]
    b = sqlparse.split(text)
    stitch.check(len(a) == len(b), "split count == parse count")


@stitch.endpoint
def split_idempotent(text: Str[1024, SqlText]):
    p = sqlparse.split(text)
    for x in p:
        stitch.check(len(sqlparse.split(x)) <= 1, "split piece splits to one")


# ---- format ----
@stitch.endpoint
def fmt_case(text: Str[1024, SqlText], kc: KW_CASE, ic: KW_CASE):
    out = sqlparse.format(text, keyword_case=kc, identifier_case=ic)
    stitch.check(isinstance(out, str), "str")
    sqlparse.parse(out)


@stitch.endpoint
def fmt_strip_comments(text: Str[1024, SqlText]):
    out = sqlparse.format(text, strip_comments=True)
    sqlparse.parse(out)
    out2 = sqlparse.format(out, strip_comments=True)
    stitch.check(out2 == out, "strip_comments idempotent")


@stitch.endpoint
def fmt_strip_whitespace(text: Str[1024, SqlText]):
    out = sqlparse.format(text, strip_whitespace=True)
    stitch.check(sqlparse.format(out, strip_whitespace=True) == out, "strip_whitespace idempotent")


@stitch.endpoint
def fmt_reindent(text: Str[1024, SqlText], iw: Range[1, 8], wrap: OneOf[0, 20, 60],
                 cf: Bool, al: Bool, ra: Bool):
    sqlparse.format(text, reindent=True, indent_width=iw, wrap_after=wrap, comma_first=cf,
                    indent_after_first=al, indent_columns=ra)


@stitch.endpoint
def fmt_reindent_aligned(text: Str[1024, SqlText]):
    sqlparse.format(text, reindent_aligned=True)


@stitch.endpoint
def fmt_reindent_idem(text: Str[512, SqlText]):
    o1 = sqlparse.format(text, reindent=True)
    o2 = sqlparse.format(o1, reindent=True)
    stitch.check(o1 == o2, "reindent idempotent")


@stitch.endpoint
def fmt_case_idem(text: Str[512, SqlText], kc: OneOf["upper", "lower"]):
    o1 = sqlparse.format(text, keyword_case=kc)
    o2 = sqlparse.format(o1, keyword_case=kc)
    stitch.check(o1 == o2, "keyword_case idempotent")


@stitch.endpoint
def fmt_misc(text: Str[1024, SqlText], ob: OneOf["python", "php"], sc: Bool, tl: OneOf[None, 2, 5, 40]):
    sqlparse.format(text, output_format=ob, strip_comments=sc, truncate_strings=tl)


@stitch.endpoint
def fmt_all(text: Str[1024, SqlText], kc: KW_CASE, ic: KW_CASE, sc: Bool, sw: Bool, ri: Bool,
            iw: Range[1, 6], cf: Bool, sp: Bool):
    sqlparse.format(text, keyword_case=kc, identifier_case=ic, strip_comments=sc,
                    strip_whitespace=sw, reindent=ri, indent_width=iw, comma_first=cf,
                    use_space_around_operators=sp)


@stitch.endpoint(bails_on=(sqlparse.exceptions.SQLParseError,))
def fmt_bad_options(text: Str[64, SqlText], kc: Str[8], iw: int, ow: Str[8]):
    sqlparse.format(text, keyword_case=kc, indent_width=iw, output_format=ow)


@stitch.endpoint
def fmt_stmt(st: Borrow[Stmt], kc: KW_CASE, ri: Bool):
    s = str(st)
    sqlparse.format(s, keyword_case=kc, reindent=ri)


# ---- token tree ----
@stitch.endpoint
def stmt_get_type(st: Borrow[Stmt]):
    t = st.get_type()
    stitch.check(isinstance(t, str), "type str")


@stitch.endpoint
def stmt_roundtrip(st: Borrow[Stmt]):
    stitch.check("".join(t.value for t in st.flatten()) == str(st), "flatten == str")
    stitch.check(str(sqlparse.parse(str(st))[0]) == str(st), "reparse stable")


@stitch.endpoint
def tree_consistency(st: Borrow[Stmt]):
    def walk(node):
        for t in node.tokens:
            stitch.check(t.parent is node, "parent link")
            if t.is_group:
                walk(t)
    walk(st)


@stitch.endpoint
def stmt_flatten_types(st: Borrow[Stmt]):
    for t in st.flatten():
        stitch.check(not t.is_group, "flatten yields leaves")
        stitch.check(isinstance(t.ttype, type(T.Token)), "ttype")


@stitch.endpoint
def get_sublist(st: Borrow[Stmt], s: Src[8]) -> New[Tok]:
    subs = list(st.get_sublists())
    stitch.require(subs, "no sublists")
    return s.pick(subs)


@stitch.endpoint
def get_token(st: Borrow[Stmt], s: Src[8]) -> New[Tok]:
    stitch.require(st.tokens, "no tokens")
    return s.pick(st.tokens)


@stitch.endpoint
def tok_next(st: Borrow[Stmt], s: Src[8], skip_ws: Bool, skip_cm: Bool) -> New[Tok]:
    idx = s.choice(len(st.tokens) + 1) - 1
    i, t = st.token_next(idx, skip_ws=skip_ws, skip_cm=skip_cm)
    stitch.require(t is not None, "no next")
    return t


@stitch.endpoint
def tok_first(st: Borrow[Stmt], ws: Bool, cm: Bool) -> New[Tok]:
    t = st.token_first(skip_ws=ws, skip_cm=cm)
    stitch.require(t is not None, "none")
    return t


@stitch.endpoint
def tok_prev(st: Borrow[Stmt], s: Src[8]):
    stitch.require(st.tokens, "empty")
    idx = s.choice(len(st.tokens))   # token_prev(idx) requires a valid index
    st.token_prev(idx)
    st.token_prev(idx, skip_ws=False)


@stitch.endpoint
def tok_index_lookup(st: Borrow[Stmt], s: Src[8]):
    stitch.require(st.tokens, "no tokens")
    t = s.pick(st.tokens)
    stitch.check(st.token_index(t) == st.tokens.index(t), "token_index")


@stitch.endpoint
def tok_matching(st: Borrow[Stmt], s: Src[8]):
    stitch.require(st.tokens, "no tokens")
    st.token_matching(lambda t: t.ttype is T.Keyword, 0)
    st.token_next_by(i=sql.Parenthesis)
    st.token_next_by(t=T.Keyword)
    st.token_next_by(m=(T.Keyword, "FROM"))


@stitch.endpoint
def ident_names(st: Borrow[Stmt]):
    for t in st.flatten():
        pass
    for sub in st.get_sublists():
        if isinstance(sub, sql.IdentifierList):
            for i in sub.get_identifiers():
                _ident(i)
        elif isinstance(sub, sql.Identifier):
            _ident(sub)


def _ident(i):
    if not isinstance(i, sql.Identifier):
        return
    i.get_name()
    i.get_real_name()
    i.get_alias()
    i.get_parent_name()
    i.get_ordering()
    i.get_array_indices() and list(i.get_array_indices())
    i.get_typecast()
    i.is_wildcard()


@stitch.endpoint
def ident_deep(st: Borrow[Stmt]):
    def walk(n):
        for t in n.tokens:
            if isinstance(t, sql.Identifier):
                _ident(t)
            if isinstance(t, sql.Where):
                list(t.get_sublists())
            if isinstance(t, sql.Comparison):
                t.left, t.right
            if isinstance(t, sql.Function):
                t.get_parameters()
            if isinstance(t, sql.Case):
                t.get_cases()
            if isinstance(t, sql.Parenthesis):
                t._groupable_tokens
            if t.is_group:
                walk(t)
    walk(st)


@stitch.endpoint
def tok_methods(t: Borrow[Tok]):
    t.has_ancestor(t)
    t.within(sql.Statement)
    t.is_whitespace
    t.is_keyword
    t.match(T.Keyword, "SELECT")
    t.match(T.Keyword, ["SELECT", "FROM"])
    t.match(T.Name, r"^a", regex=True)
    t.normalized
    t.flatten()
    t.get_parent_name() if hasattr(t, "get_parent_name") else None
    t.get_real_name() if hasattr(t, "get_real_name") else None
    t.get_name() if hasattr(t, "get_name") else None
    stitch.check(str(t) == "".join(x.value for x in t.flatten()), "tok flatten")


@stitch.endpoint
def tok_edit(st: Borrow[Stmt], s: Src[8], text: Str[16], pos: OneOf["before", "after"]):
    stitch.require(len(st.tokens) > 0, "empty")
    t = s.pick(st.tokens)
    new = sql.Token(T.Keyword, text)
    if pos == "before":
        st.insert_before(t, new)
    else:
        st.insert_after(t, new)
    stitch.check(new in st.tokens, "inserted")


@stitch.endpoint
def tok_group_ops(st: Borrow[Stmt], s: Src[8]):
    stitch.require(st.tokens, "empty")
    st.get_alias()
    st.get_name()
    st.get_real_name()
    st.get_parent_name()
    st.get_type()
    st.token_next_by(i=sql.Where)
    st.ttype
    st.is_group
    st.is_whitespace
    st._pprint_tree(max_depth=3)


@stitch.endpoint
def stmt_str_eq_after_format(st: Borrow[Stmt], kc: OneOf["upper", "lower"]):
    out = sqlparse.format(str(st), keyword_case=kc)
    stitch.check(out.lower().split() == str(st).lower().split(), "keyword_case only changes case")
