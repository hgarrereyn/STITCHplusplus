"""networkx harness: shared object types, data types, helpers and the table-driven registration.

Graphs (`G`) are small (at most ~40 nodes) so exponential algorithms stay fast; slow-by-design
calls run under stitch.time_limit and bail when over budget.

Bugs: exceptions other than networkx's own (NetworkXException and its subclasses), failed checks.
A handful of documented non-networkx exceptions (e.g. ValueError for a bad parameter) are listed
per stub.
"""
import itertools
import math

import types

import networkx as _networkx
import stitch
from stitch import Bool, Borrow, F64, Range, New, OneOf, Src, Str, Take


class _Lenient:
    """networkx (or one of its modules), where a function the running version lacks resolves to a
    stub that bails, so the harness also loads against other releases (latest-release checks)."""

    def __init__(self, mod):
        self._mod = mod

    def __getattr__(self, name):
        try:
            v = getattr(self._mod, name)
        except AttributeError:
            def missing(*a, **k):
                stitch.bail(f"{name} is not in this networkx version")
            missing.__name__ = name
            return missing
        return _Lenient(v) if isinstance(v, types.ModuleType) else v


nx = _Lenient(_networkx)

G = stitch.object("G")
NXERR = (nx.NetworkXException,)
KINDS = OneOf["graph", "digraph", "multigraph", "multidigraph"]
CLASSES = {"graph": nx.Graph, "digraph": nx.DiGraph, "multigraph": nx.MultiGraph, "multidigraph": nx.MultiDiGraph}
MAXN = 40


def small(g, n=MAXN):
    stitch.require(g.number_of_nodes() <= n, f"more than {n} nodes")


def nodes(g):
    ns = list(g.nodes)
    stitch.require(ns, "empty graph")
    return ns


def pick_node(g, s):
    return s.pick(nodes(g))


def pick_edge(g, s):
    es = list(g.edges(keys=True) if g.is_multigraph() else g.edges)
    stitch.require(es, "no edges")
    return s.pick(es)


def weighted(g, s):
    """Give every edge an integer 'weight' (0..9, chosen by the program) and return the graph."""
    for e in (g.edges(keys=True, data=True) if g.is_multigraph() else g.edges(data=True)):
        e[-1]["weight"] = s.range(0, 9) if s.more() else 1
    return g


def simple_undirected(g):
    stitch.require(not g.is_directed() and not g.is_multigraph(), "needs a simple undirected graph")


def simple_directed(g):
    stitch.require(g.is_directed() and not g.is_multigraph(), "needs a simple directed graph")


def simple(g):
    stitch.require(not g.is_multigraph(), "needs a simple graph")


def undirected(g):
    stitch.require(not g.is_directed(), "needs an undirected graph")


def directed(g):
    stitch.require(g.is_directed(), "needs a directed graph")


def connected(g):
    stitch.require(g.number_of_nodes() > 0 and (nx.is_weakly_connected(g) if g.is_directed() else nx.is_connected(g)), "not connected")


def no_selfloops(g):
    stitch.require(nx.number_of_selfloops(g) == 0, "has self-loops")


def close(a, b, tol=1e-6):
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(close(a[k], b[k], tol) for k in a)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return a == b or abs(a - b) <= tol * max(1.0, abs(a), abs(b)) or (math.isnan(a) and math.isnan(b))
    return a == b


def same_graph(a, b):
    return nx.utils.graphs_equal(a, b)


def materialize(x, limit=2000):
    """Consume generators / iterators (bounded) so lazy algorithms actually run."""
    if isinstance(x, (str, bytes, dict, int, float, bool)) or x is None:
        return x
    if hasattr(x, "__iter__") and not isinstance(x, (list, tuple, set, frozenset, nx.Graph)):
        return list(itertools.islice(x, limit))
    return x


def odd_graph(g, e):
    """Why an exception is the harness's doing rather than the algorithm's, or None: networkx lets
    attributes and labels be anything, but weighted algorithms need numeric (and, for spectra,
    non-negative) weights, and algorithms that sort nodes need labels of one comparable type."""
    ws = [d.get("weight") for *_, d in g.edges(data=True)]
    if isinstance(e, KeyError) and e.args == ("weight",) and any(w is None for w in ws):
        return "edges without a weight"
    if isinstance(e, KeyError):
        return None
    if any(w is not None and (isinstance(w, bool) or not isinstance(w, (int, float))) for w in ws):
        return "non-numeric weights"
    if any(isinstance(w, (int, float)) and (w < 0 or w != w) for w in ws):
        return "negative or NaN weights"
    if isinstance(e, TypeError) and len({type(n) for n in g}) > 1 and "not supported between instances" in str(e):
        return "node labels of several types"
    return None


def _register(name, call, ann, doc=None):
    """Register a stub named `name` whose positional parameters are the keys of `ann` (the runtime
    passes arguments positionally, in annotation order) and that calls call(**kwargs)."""
    params = [k for k in ann if k != "return"]
    ns = {"call": call}
    exec(f"def {name}({', '.join(params)}):\n    return call({', '.join(f'{p}={p}' for p in params)})\n", ns)
    f = ns[name]
    f.__annotations__ = dict(ann)
    f.__doc__ = doc
    stitch.endpoint(name=name)(f)


def _guarded(fn, bails, slow):
    try:
        if slow:
            with stitch.time_limit(slow):
                return materialize(fn())
        return materialize(fn())
    except NXERR + tuple(bails) as e:
        stitch.bail(type(e).__name__)


def algo(name, fn, pre=(), bails=(), slow=None, src=0):
    """Register `name(g)` (or `name(g, s)` with src > 0: fn(g, s) picks nodes from the Source).

    pre: precondition helpers applied to g; bails: extra exception types that are documented
    failures; slow: a time budget in seconds (bails when exceeded)."""
    def call(g, s=None):
        for p in pre:
            p(g)
        try:
            return _guarded((lambda: fn(g, s)) if src else (lambda: fn(g)), bails, slow)
        except (TypeError, ValueError, ArithmeticError, KeyError) as e:
            why = odd_graph(g, e)
            if why:
                stitch.bail(f"{type(e).__name__} on a graph with {why}")
            raise
    ann = {"g": Borrow[G], "s": Src[src]} if src else {"g": Borrow[G]}
    _register(name, call, ann, fn.__doc__)


def builder(name, fn, ann, bails=()):
    """Register a stub that returns a new graph built by fn(**params); NetworkX errors bail."""
    def call(**kw):
        g = _guarded(lambda: fn(**kw), bails, 1.0)
        stitch.require(isinstance(g, nx.Graph), "no graph")
        small(g, 200)
        return g
    _register(name, call, {**ann, "return": New[G]}, fn.__doc__)
