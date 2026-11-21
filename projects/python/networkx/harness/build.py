"""Graph construction: classic and random generators, parsers of the text formats networkx reads,
and graph operators. Every builder returns a new graph object."""
import io
import json
import xml.etree.ElementTree as ET

from harness.common import nx
import stitch
from stitch import Bool, Borrow, F64, Range, New, OneOf, Src, Str
from harness.common import CLASSES, G, KINDS, NXERR, builder, small

N = Range[0, 24]
SEED = Range[0, 99]
P = OneOf[0.0, 0.1, 0.3, 0.5, 0.9, 1.0]

# ---- empty graphs and edge lists ----
EdgeText = stitch.datatype("EdgeText")


@stitch.decoder(EdgeText)
def edge_text(s):
    lines = []
    while s.more():
        u, v = s.range(0, 12), s.range(0, 12)
        tail = s.pick(["", f" {s.range(0, 9)}", " {'weight': 2}", " {}", " x", f" {{'weight': {s.range(-3, 9)}, 'c': 'r'}}"])
        lines.append(f"{u} {v}{tail}" if s.coin(0.9) else s.str(8))
    return "\n".join(lines)


def _from_edges(kind, src):
    g = CLASSES[kind]()
    while src.more() and g.number_of_edges() < 80:
        u, v = src.range(0, 15), src.range(0, 15)
        g.add_edge(u, v)
    while src.more() and g.number_of_nodes() < 18:
        g.add_node(src.range(0, 20))
    return g


builder("from_edges", lambda kind, src: _from_edges(kind, src), {"kind": KINDS, "src": Src[128]})
builder("empty", lambda kind, n: nx.empty_graph(n, create_using=CLASSES[kind]), {"kind": KINDS, "n": N})

# ---- classic generators ----
CLASSIC = {
    "path_graph": (lambda n: nx.path_graph(n), {"n": N}),
    "cycle_graph": (lambda n: nx.cycle_graph(n), {"n": N}),
    "complete_graph": (lambda n: nx.complete_graph(min(n, 12)), {"n": N}),
    "star_graph": (lambda n: nx.star_graph(n), {"n": N}),
    "wheel_graph": (lambda n: nx.wheel_graph(n), {"n": N}),
    "ladder_graph": (lambda n: nx.ladder_graph(n), {"n": Range[0, 12]}),
    "circular_ladder_graph": (lambda n: nx.circular_ladder_graph(n), {"n": Range[0, 12]}),
    "grid_2d_graph": (lambda m, n: nx.grid_2d_graph(m, n), {"m": Range[0, 6], "n": Range[0, 6]}),
    "hypercube_graph": (lambda n: nx.hypercube_graph(n), {"n": Range[0, 5]}),
    "balanced_tree": (lambda r, h: nx.balanced_tree(r, h), {"r": Range[1, 3], "h": Range[0, 3]}),
    "barbell_graph": (lambda m1, m2: nx.barbell_graph(m1, m2), {"m1": Range[2, 7], "m2": Range[0, 6]}),
    "lollipop_graph": (lambda m, n: nx.lollipop_graph(m, n), {"m": Range[2, 7], "n": Range[0, 6]}),
    "complete_bipartite_graph": (lambda a, b: nx.complete_bipartite_graph(a, b), {"a": Range[0, 6], "b": Range[0, 6]}),
    "complete_multipartite_graph": (lambda a, b, c: nx.complete_multipartite_graph(a, b, c), {"a": Range[0, 4], "b": Range[0, 4], "c": Range[0, 4]}),
    "turan_graph": (lambda n, r: nx.turan_graph(n, r), {"n": Range[1, 12], "r": Range[1, 5]}),
    "circulant_graph": (lambda n, a, b: nx.circulant_graph(n, [a, b]), {"n": Range[1, 14], "a": Range[1, 5], "b": Range[1, 5]}),
    "dorogovtsev_goltsev_mendes_graph": (lambda n: nx.dorogovtsev_goltsev_mendes_graph(n), {"n": Range[0, 3]}),
    "full_rary_tree": (lambda r, n: nx.full_rary_tree(r, n), {"r": Range[1, 4], "n": N}),
    "binomial_tree": (lambda n: nx.binomial_tree(n), {"n": Range[0, 4]}),
    "petersen_graph": (lambda: nx.petersen_graph(), {}),
    "karate_club_graph": (lambda: nx.karate_club_graph(), {}),
    "florentine_families_graph": (lambda: nx.florentine_families_graph(), {}),
    "davis_southern_women_graph": (lambda: nx.davis_southern_women_graph(), {}),
    "krackhardt_kite_graph": (lambda: nx.krackhardt_kite_graph(), {}),
    "tutte_graph": (lambda: nx.tutte_graph(), {}),
    "heawood_graph": (lambda: nx.heawood_graph(), {}),
    "frucht_graph": (lambda: nx.frucht_graph(), {}),
    "chvatal_graph": (lambda: nx.chvatal_graph(), {}),
    "les_miserables_graph": (lambda: nx.les_miserables_graph(), {}),
    "LCF_graph": (lambda n, a, b, r: nx.LCF_graph(n, [a, b], r), {"n": Range[0, 16], "a": Range[-8, 8], "b": Range[-8, 8], "r": Range[0, 4]}),
    "mycielski_graph": (lambda n: nx.mycielski_graph(n), {"n": Range[1, 5]}),
    "triangular_lattice_graph": (lambda m, n: nx.triangular_lattice_graph(m, n), {"m": Range[0, 4], "n": Range[0, 4]}),
    "hexagonal_lattice_graph": (lambda m, n: nx.hexagonal_lattice_graph(m, n), {"m": Range[0, 3], "n": Range[0, 3]}),
}
for name, (fn, ann) in CLASSIC.items():
    builder(name, fn, ann)

# ---- random generators (seeded) ----
RANDOM = {
    "gnp_random_graph": (lambda n, p, seed, directed: nx.gnp_random_graph(n, p, seed=seed, directed=directed), {"n": N, "p": P, "seed": SEED, "directed": Bool}),
    "gnm_random_graph": (lambda n, m, seed, directed: nx.gnm_random_graph(n, m, seed=seed, directed=directed), {"n": N, "m": Range[0, 60], "seed": SEED, "directed": Bool}),
    "watts_strogatz_graph": (lambda n, k, p, seed: nx.watts_strogatz_graph(n, k, p, seed=seed), {"n": N, "k": Range[0, 6], "p": P, "seed": SEED}),
    "barabasi_albert_graph": (lambda n, m, seed: nx.barabasi_albert_graph(n, m, seed=seed), {"n": N, "m": Range[1, 5], "seed": SEED}),
    "powerlaw_cluster_graph": (lambda n, m, p, seed: nx.powerlaw_cluster_graph(n, m, p, seed=seed), {"n": N, "m": Range[1, 5], "p": P, "seed": SEED}),
    "random_regular_graph": (lambda d, n, seed: nx.random_regular_graph(d, n, seed=seed), {"d": Range[0, 5], "n": N, "seed": SEED}),
    "random_tree": (lambda n, seed: nx.random_tree(n, seed=seed), {"n": N, "seed": SEED}),
    "random_powerlaw_tree": (lambda n, seed: nx.random_powerlaw_tree(n, seed=seed, tries=200), {"n": Range[1, 12], "seed": SEED}),
    "scale_free_graph": (lambda n, seed: nx.scale_free_graph(n, seed=seed), {"n": N, "seed": SEED}),
    "random_k_out_graph": (lambda n, k, seed: nx.random_k_out_graph(n, k, 1.0, seed=seed), {"n": N, "k": Range[0, 4], "seed": SEED}),
    "gn_graph": (lambda n, seed: nx.gn_graph(n, seed=seed), {"n": N, "seed": SEED}),
    "random_geometric_graph": (lambda n, r, seed: nx.random_geometric_graph(n, r, seed=seed), {"n": N, "r": P, "seed": SEED}),
    "configuration_model": (lambda a, b, c, d, seed: nx.configuration_model([a, b, c, d], seed=seed), {"a": Range[0, 4], "b": Range[0, 4], "c": Range[0, 4], "d": Range[0, 4], "seed": SEED}),
    "directed_configuration_model": (lambda a, b, seed: nx.directed_configuration_model([a, b, 1], [b, a, 1], seed=seed), {"a": Range[0, 4], "b": Range[0, 4], "seed": SEED}),
    "havel_hakimi_graph": (lambda a, b, c, d: nx.havel_hakimi_graph([a, b, c, d]), {"a": Range[0, 4], "b": Range[0, 4], "c": Range[0, 4], "d": Range[0, 4]}),
    "random_partition_graph": (lambda a, b, p, seed: nx.random_partition_graph([a, b], p, 0.1, seed=seed), {"a": Range[0, 8], "b": Range[0, 8], "p": P, "seed": SEED}),
    "stochastic_block_model": (lambda a, b, p, seed: nx.stochastic_block_model([a, b], [[p, 0.1], [0.1, p]], seed=seed), {"a": Range[0, 8], "b": Range[0, 8], "p": P, "seed": SEED}),
    "connected_caveman_graph": (lambda l, k: nx.connected_caveman_graph(l, k), {"l": Range[1, 5], "k": Range[2, 5]}),
    "random_lobster": (lambda n, seed: nx.random_lobster(n, 0.5, 0.5, seed=seed), {"n": Range[1, 10], "seed": SEED}),
    "duplication_divergence_graph": (lambda n, p, seed: nx.duplication_divergence_graph(n, p, seed=seed), {"n": Range[2, 20], "p": P, "seed": SEED}),
    "random_cograph": (lambda n, seed: nx.random_cograph(n, seed=seed), {"n": Range[0, 4], "seed": SEED}),
    "prefix_tree": (lambda a, b, c: nx.prefix_tree([a, b, c]), {"a": Str[6], "b": Str[6], "c": Str[6]}),
    "from_prufer_sequence": (lambda a, b, c: nx.from_prufer_sequence([a, b, c]), {"a": Range[0, 5], "b": Range[0, 5], "c": Range[0, 5]}),
    "from_nested_tuple": (lambda d: nx.from_nested_tuple(((), ((), ()), ((),) * d)), {"d": Range[0, 4]}),
    "sudoku_graph": (lambda n: nx.sudoku_graph(n), {"n": Range[0, 2]}),
    "margulis_gabber_galil_graph": (lambda n: nx.margulis_gabber_galil_graph(n), {"n": Range[1, 4]}),
    "random_degree_sequence_graph": (lambda a, b, c, d, seed: nx.random_degree_sequence_graph([a, b, c, d], seed=seed, tries=5), {"a": Range[0, 3], "b": Range[0, 3], "c": Range[0, 3], "d": Range[0, 3], "seed": SEED}),
}
for name, (fn, ann) in RANDOM.items():
    builder(name, fn, ann, bails=(ValueError,) if name in ("random_regular_graph", "configuration_model") else ())


# ---- text formats: decoders write a random graph with networkx's writer, then the fuzzer mutates ----
def _rand_graph(s, directed=False, multi=False):
    g = (nx.MultiDiGraph if directed and multi else nx.DiGraph if directed else nx.MultiGraph if multi else nx.Graph)()
    while s.more() and g.number_of_edges() < 25:
        u, v = s.range(0, 9), s.range(0, 9)
        g.add_edge(u, v, weight=s.range(0, 5)) if not multi else g.add_edge(u, v)
    return g


FORMATS = {}


def _json(t):
    """The JSON readers take parsed data: text that is not a JSON object bails."""
    try:
        d = json.loads(t)
    except ValueError:
        stitch.bail("not JSON")
    stitch.require(isinstance(d, dict), "not a JSON object")
    return d


def _root(g):
    """The root of an arborescence (the node nothing points to)."""
    return next(n for n, d in g.in_degree() if d == 0)


def fmt(name, write, read, directed=False, multi=False, binary=False, bails=()):
    dt = stitch.datatype(name)

    def dec(s):
        g = _rand_graph(s, directed, multi)
        try:
            out = write(g)
        except Exception:  # noqa: BLE001 - a writer that cannot write this graph: empty text
            out = b"" if binary else ""
        return out
    stitch.decoder(dt)(dec)
    FORMATS[name] = (write, read)
    t = (stitch.Bytes if binary else Str)[2048, dt]
    builder(f"parse_{name.lower()}", lambda text, _r=read: _r(text), {"text": t}, bails=bails)


def _lines(g, fn):
    return "\n".join(fn(g))


# from_graph6_bytes documents ValueError for bytes it cannot parse
fmt("Graph6", lambda g: nx.to_graph6_bytes(nx.Graph(g), header=False).strip(), lambda t: nx.from_graph6_bytes(t), binary=True, bails=(ValueError,))
fmt("Sparse6", lambda g: nx.to_sparse6_bytes(g, header=False).strip(), lambda t: nx.from_sparse6_bytes(t), multi=True, binary=True)
fmt("GML", lambda g: _lines(g, nx.generate_gml), lambda t: nx.parse_gml(t, label=None))
fmt("GraphML", lambda g: _lines(g, nx.generate_graphml), lambda t: nx.parse_graphml(t), bails=(ET.ParseError,))  # malformed XML
fmt("AdjList", lambda g: _lines(g, nx.generate_adjlist), lambda t: nx.parse_adjlist(t.split("\n"), nodetype=int), bails=(TypeError, IndexError))  # documented conversion errors
fmt("MultilineAdjList", lambda g: _lines(g, nx.generate_multiline_adjlist), lambda t: nx.parse_multiline_adjlist(iter(t.split("\n")), nodetype=int), bails=(TypeError, IndexError))  # documented conversion errors
fmt("EdgeList", lambda g: _lines(g, nx.generate_edgelist), lambda t: nx.parse_edgelist(t.split("\n"), nodetype=int), bails=(TypeError, IndexError))  # documented conversion errors
fmt("Pajek", lambda g: _lines(g, nx.generate_pajek), lambda t: nx.parse_pajek(t), directed=True, multi=True)
fmt("Leda", lambda g: "LEDA.GRAPH\nstring\nint\n-1\n" + str(g.number_of_nodes()) + "\n" + "".join("|{%s}|\n" % n for n in g) + str(g.number_of_edges()) + "\n" + "".join(f"{list(g).index(u) + 1} {list(g).index(v) + 1} 0 |{{1}}|\n" for u, v in g.edges()),
    lambda t: nx.parse_leda(t), directed=True)
fmt("NodeLinkJson", lambda g: json.dumps(nx.node_link_data(g)), lambda t: nx.node_link_graph(_json(t)))
fmt("AdjacencyJson", lambda g: json.dumps(nx.adjacency_data(g)), lambda t: nx.adjacency_graph(_json(t)))
fmt("CytoscapeJson", lambda g: json.dumps(nx.cytoscape_data(g)), lambda t: nx.cytoscape_graph(_json(t)))
fmt("TreeJson", lambda g: json.dumps(nx.tree_data(g, _root(g))), lambda t: nx.tree_graph(_json(t)), directed=False)
EdgeTextG = stitch.datatype("EdgeText")
builder("parse_weighted_edgelist", lambda text: nx.parse_edgelist(text.split("\n"), nodetype=int, data=(("weight", float),)), {"text": Str[1024, EdgeText]},
        bails=(TypeError, IndexError))  # documented: conversion failures, data/data_keys length mismatch
builder("parse_edgelist_data", lambda text: nx.parse_edgelist(text.split("\n"), nodetype=int, data=True), {"text": Str[1024, EdgeText]},
        bails=(TypeError, IndexError))

# ---- operators ----
OPS2 = {
    "union": lambda a, b: nx.union(a, b, rename=("a-", "b-")),
    "disjoint_union": nx.disjoint_union,
    "compose": nx.compose,
    "intersection": nx.intersection,
    "difference": nx.difference,
    "symmetric_difference": nx.symmetric_difference,
    "cartesian_product": nx.cartesian_product,
    "tensor_product": nx.tensor_product,
    "strong_product": nx.strong_product,
    "lexicographic_product": nx.lexicographic_product,
    "rooted_product": lambda a, b: nx.rooted_product(a, b, next(iter(b))) if len(b) else stitch.bail("empty root graph"),
}


def _binop(fn):
    def f(a, b):
        small(a, 20)
        small(b, 20)
        return fn(a, b)
    return f


for name, fn in OPS2.items():
    builder(name, _binop(fn), {"a": Borrow[G], "b": Borrow[G]}, bails=(KeyError,) if name in () else ())

OPS1 = {
    "complement": nx.complement,
    "reverse": lambda g: nx.reverse(g),
    "line_graph": nx.line_graph,
    "to_directed": lambda g: g.to_directed(),
    "to_undirected": lambda g: g.to_undirected(),
    "copy": lambda g: g.copy(),
    "convert_node_labels_to_integers": lambda g: nx.convert_node_labels_to_integers(g, ordering="sorted" if all(isinstance(n, int) for n in g) else "default"),
    "power": lambda g: nx.power(g, 2),
    "transitive_closure": lambda g: nx.transitive_closure(g),
    "transitive_closure_dag": lambda g: nx.transitive_closure_dag(g),
    "transitive_reduction": nx.transitive_reduction,
    "condensation": nx.condensation,
    "moral_graph": nx.moral_graph,
    "minimum_spanning_tree": nx.minimum_spanning_tree,
    "maximum_spanning_tree": nx.maximum_spanning_tree,
    "k_core": nx.k_core,
    "k_shell": nx.k_shell,
    "k_crust": nx.k_crust,
    "k_corona": lambda g: nx.k_corona(g, 1),
    "k_truss": lambda g: nx.k_truss(g, 3),
    "dag_to_branching": nx.dag_to_branching,
    "stochastic_graph": lambda g: nx.stochastic_graph(g, copy=True),
    "inverse_line_graph": nx.inverse_line_graph,
    "contracted_edge": lambda g: nx.contracted_edge(g, next(iter(g.edges())), self_loops=False) if g.number_of_edges() else stitch.bail("no edges"),
    "quotient_by_components": lambda g: nx.quotient_graph(g, list(nx.connected_components(g)) if not g.is_directed() else list(nx.weakly_connected_components(g))),
    "spanner": lambda g: nx.spanner(g, 3, seed=1),
    "random_spanning_tree": lambda g: nx.random_spanning_tree(g, seed=1),
}
for name, fn in OPS1.items():
    builder(name, lambda g, _f=fn: (small(g), _f(g))[1], {"g": Borrow[G]}, bails=(nx.NetworkXException,))

builder("subgraph", lambda g, s: g.subgraph([n for n in list(g) if s.coin()]).copy(), {"g": Borrow[G], "s": Src[64]})
builder("edge_subgraph", lambda g, s: g.edge_subgraph([e for e in list(g.edges(keys=True) if g.is_multigraph() else g.edges()) if s.coin()]).copy(), {"g": Borrow[G], "s": Src[64]})
builder("ego_graph", lambda g, s: nx.ego_graph(g, s.pick(list(g)) if len(g) else stitch.bail("empty"), radius=s.range(0, 3)), {"g": Borrow[G], "s": Src[8]})
builder("relabel_nodes", lambda g, s: nx.relabel_nodes(g, {n: (s.range(0, 30) if s.coin(0.8) else str(n)) for n in g}, copy=True), {"g": Borrow[G], "s": Src[64]})
builder("bfs_tree", lambda g, s: nx.bfs_tree(g, s.pick(list(g)) if len(g) else stitch.bail("empty")), {"g": Borrow[G], "s": Src[8]})
builder("dfs_tree", lambda g, s: nx.dfs_tree(g, s.pick(list(g)) if len(g) else stitch.bail("empty")), {"g": Borrow[G], "s": Src[8]})
builder("induced_subgraph_core", lambda g: nx.k_core(nx.Graph(g), 2) if not g.is_directed() else stitch.bail("directed"), {"g": Borrow[G]})
