"""intervaltree: a mutable interval tree.

Oracles: the tree's own verify() after every change, and a plain list of intervals that mirrors
every change ("model"): queries must match brute force over it.
"""
import stitch
from intervaltree import Interval, IntervalTree
from stitch import Bool, Borrow, New, OneOf, Range, Src, Take

Tree = stitch.object("Tree")

B = Range[-40, 40]
L = Range[0, 20]


def model(t):
    m = stitch.get(t, "model", None)
    stitch.require(m is not None, "no model")
    return m


def checked(t, what):
    try:
        t.verify()
    except AssertionError as e:
        stitch.check(False, f"verify() after {what}: {e}"[:80])
    stitch.check(sorted(t) == sorted(model(t)), f"tree == model after {what}")


@stitch.endpoint
def new_tree() -> New[Tree]:
    t = IntervalTree()
    stitch.set(t, "model", [])
    return t


@stitch.endpoint(bails_on=(ValueError,))
def addi(t: Borrow[Tree], begin: B, length: L, data: OneOf[None, 0, 1, "x"]):
    iv = Interval(begin, begin + length, data)
    t.add(iv)
    m = model(t)
    if not iv.is_null() and iv not in m:
        m.append(iv)
    checked(t, "add")


@stitch.endpoint
def remove(t: Borrow[Tree], s: Src[8]):
    stitch.require(len(t) > 0, "empty")
    iv = s.pick(sorted(t))
    t.remove(iv)
    model(t).remove(iv)
    checked(t, "remove")


@stitch.endpoint
def remove_overlap(t: Borrow[Tree], begin: B, length: L):
    m = model(t)
    end = begin + length if length else None
    t.remove_overlap(begin, end)
    gone = [iv for iv in m if (iv.overlaps(begin, end) if end is not None else iv.contains_point(begin))]
    for iv in gone:
        m.remove(iv)
    checked(t, "remove_overlap")


@stitch.endpoint
def remove_envelop(t: Borrow[Tree], begin: B, length: L):
    m = model(t)
    t.remove_envelop(begin, begin + length)
    for iv in [iv for iv in m if begin <= iv.begin and iv.end <= begin + length]:
        m.remove(iv)
    checked(t, "remove_envelop")


@stitch.endpoint
def chop(t: Borrow[Tree], begin: B, length: L):
    t.chop(begin, begin + length)
    stitch.set(t, "model", sorted(t))  # chop's exact result is checked through the queries below
    checked(t, "chop")
    stitch.check(not t.overlaps(begin, begin + length) or length == 0, "chop leaves nothing in the range")


@stitch.endpoint
def slice_at(t: Borrow[Tree], point: B):
    want = set()
    for iv in t:
        if iv.begin < point < iv.end:
            want |= {Interval(iv.begin, point, iv.data), Interval(point, iv.end, iv.data)}
        else:
            want.add(iv)
    t.slice(point)
    stitch.set(t, "model", sorted(t))
    checked(t, "slice")
    stitch.check(set(t) == want, "slice splits exactly the intervals containing the point")


@stitch.endpoint
def merge_overlaps(t: Borrow[Tree], strict: Bool, reduce: Bool):
    t.merge_overlaps(data_reducer=(lambda a, b: (a or 0)) if reduce else None, strict=strict)
    stitch.set(t, "model", sorted(t))
    checked(t, "merge_overlaps")


@stitch.endpoint
def merge_equals(t: Borrow[Tree]):
    t.merge_equals()
    stitch.set(t, "model", sorted(t))
    checked(t, "merge_equals")


@stitch.endpoint
def split_overlaps(t: Borrow[Tree]):
    t.split_overlaps()
    stitch.set(t, "model", sorted(t))
    checked(t, "split_overlaps")


@stitch.endpoint
def query(t: Borrow[Tree], begin: B, length: Range[1, 20], kind: OneOf["at", "overlap", "envelop"]):
    # (an empty range [b, b) means "nothing" to overlap() but "the point b" to Interval.overlaps())
    m = model(t)
    if kind == "at":
        got, want = t.at(begin), {iv for iv in m if iv.contains_point(begin)}
    elif kind == "overlap":
        got, want = t.overlap(begin, begin + length), {iv for iv in m if iv.overlaps(begin, begin + length)}
    else:
        got, want = t.envelop(begin, begin + length), {iv for iv in m if begin <= iv.begin and iv.end <= begin + length}
    stitch.check(set(got) == want, f"{kind}() matches brute force")


@stitch.endpoint
def bounds(t: Borrow[Tree]):
    m = model(t)
    if m:
        stitch.check(t.begin() == min(iv.begin for iv in m) and t.end() == max(iv.end for iv in m), "begin()/end()")
        stitch.check(t.range().length() == t.end() - t.begin(), "range()")


@stitch.endpoint
def set_op(a: Borrow[Tree], b: Borrow[Tree], op: OneOf["union", "difference", "intersection", "symmetric_difference"]) -> New[Tree]:
    r = getattr(a, op)(b)
    sa, sb = set(model(a)), set(model(b))
    want = {"union": sa | sb, "difference": sa - sb, "intersection": sa & sb, "symmetric_difference": sa ^ sb}[op]
    stitch.set(r, "model", sorted(want))
    checked(r, op)
    return r


@stitch.endpoint
def copy(t: Borrow[Tree]) -> New[Tree]:
    c = t.copy()
    stitch.set(c, "model", list(model(t)))
    checked(c, "copy")
    return c


@stitch.endpoint
def from_tuples(a: B, la: L, b: B, lb: L, c: B, lc: L) -> New[Tree]:
    tuples = [(x, x + l) for x, l in ((a, la), (b, lb), (c, lc)) if l > 0]
    t = IntervalTree.from_tuples(tuples)
    stitch.set(t, "model", sorted({Interval(*x) for x in tuples}))
    checked(t, "from_tuples")
    return t


@stitch.endpoint
def clear(t: Take[Tree]):
    t.clear()
    stitch.check(len(t) == 0 and t.is_empty(), "clear() empties")
