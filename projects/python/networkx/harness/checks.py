"""Oracles: algorithms that must agree with each other, results that must satisfy their definition,
and writers whose output must read back to the same graph."""
import itertools
import math

from harness.common import nx
import stitch
isomorphism = nx.algorithms.isomorphism
from networkx.algorithms.flow import (boykov_kolmogorov, dinitz, edmonds_karp, preflow_push,
                                      shortest_augmenting_path)
from stitch import Borrow, Src
from harness.build import FORMATS
from harness.common import (G, NXERR, close, connected, nodes, pick_node, same_graph, simple, simple_undirected,
                            small, weighted)


def ok(fn, *a, **k):
    """fn(...) or bail on networkx's own errors."""
    try:
        return fn(*a, **k)
    except NXERR as e:
        stitch.bail(type(e).__name__)


@stitch.endpoint(bails_on=NXERR)
def max_flow_algorithms_agree(g: Borrow[G], s: Src[64]):
    small(g, 25)
    stitch.require(g.is_directed() and not g.is_multigraph(), "needs a simple digraph")
    h = g.copy()
    for u, v in h.edges:
        h[u][v]["capacity"] = s.range(0, 9)
    a, b = pick_node(h, s), pick_node(h, s)
    stitch.require(a != b, "same node")
    values = {}
    for f in (edmonds_karp, preflow_push, shortest_augmenting_path, boykov_kolmogorov, dinitz):
        values[f.__name__] = ok(nx.maximum_flow_value, h, a, b, flow_func=f)
    cut = ok(nx.minimum_cut_value, h, a, b)
    stitch.log(f"flows {values} cut {cut}")
    stitch.check(len(set(values.values())) == 1, "max-flow algorithms agree")
    stitch.check(cut == next(iter(values.values())), "max flow == min cut")
    value, flow = ok(nx.maximum_flow, h, a, b)
    for u in flow:
        for v, x in flow[u].items():
            stitch.check(0 <= x <= h[u][v]["capacity"], "flow within capacity")
    for n in h:
        if n not in (a, b):
            stitch.check(sum(flow[p][n] for p in h.predecessors(n)) == sum(flow[n].values()), "flow is conserved")


@stitch.endpoint(bails_on=NXERR)
def min_cost_flow_agree(g: Borrow[G], s: Src[64]):
    small(g, 16)
    stitch.require(g.is_directed() and not g.is_multigraph(), "needs a simple digraph")
    h = g.copy()
    for u, v in h.edges:
        h[u][v]["capacity"] = s.range(1, 5)
        h[u][v]["weight"] = s.range(0, 5)
    a, b = pick_node(h, s), pick_node(h, s)
    stitch.require(a != b, "same node")
    flow = ok(nx.max_flow_min_cost, h, a, b)
    cost = nx.cost_of_flow(h, flow)
    mx = ok(nx.maximum_flow_value, h, a, b)
    out = sum(flow[a].values()) - sum(flow[p][a] for p in h.predecessors(a))
    stitch.check(out == mx, "max_flow_min_cost sends the maximum flow")
    h.nodes[a]["demand"], h.nodes[b]["demand"] = -mx, mx
    c2 = ok(nx.min_cost_flow_cost, h)
    stitch.log(f"cost {cost} network simplex {c2}")
    stitch.check(c2 == cost, "network simplex and capacity scaling find the same minimum cost")


@stitch.endpoint(bails_on=NXERR)
def shortest_paths_agree(g: Borrow[G], s: Src[64]):
    small(g, 25)
    h = weighted(g.copy(), s) if s.coin() else g
    a = pick_node(h, s)
    d1 = ok(nx.single_source_dijkstra_path_length, h, a)
    d2 = ok(nx.single_source_bellman_ford_path_length, h, a)
    pred, d3 = ok(nx.goldberg_radzik, h, a)
    fw = ok(nx.floyd_warshall, h)
    stitch.log(f"dijkstra {d1} bellman-ford {d2}")
    stitch.check(close(d1, d2), "Dijkstra and Bellman-Ford agree")
    stitch.check(close(d1, {k: v for k, v in d3.items() if v != math.inf}), "Dijkstra and Goldberg-Radzik agree")
    stitch.check(close(d1, {k: v for k, v in fw[a].items() if v != math.inf}), "Dijkstra and Floyd-Warshall agree")
    if h is g and not any("weight" in d for *_, d in g.edges(data=True)):
        stitch.check(close(d1, nx.single_source_shortest_path_length(g, a)), "unweighted: Dijkstra == BFS")
    b = pick_node(h, s)
    if b in d1:
        p = ok(nx.dijkstra_path, h, a, b)
        stitch.check(p[0] == a and p[-1] == b and nx.is_path(h, p), "dijkstra_path is a path from a to b")
        stitch.check(close(nx.path_weight(h, p, "weight") if any("weight" in d for *_, d in h.edges(data=True)) else len(p) - 1, d1[b]), "path weight == distance")
        l, p2 = ok(nx.bidirectional_dijkstra, h, a, b)
        stitch.check(close(l, d1[b]), "bidirectional Dijkstra finds the same distance")
        p3 = ok(nx.astar_path_length, h, a, b)
        stitch.check(close(p3, d1[b]), "A* (zero heuristic) finds the same distance")


@stitch.endpoint(bails_on=NXERR)
def johnson_agrees(g: Borrow[G], s: Src[64]):
    small(g, 16)
    h = weighted(g.copy(), s)
    stitch.require(h.number_of_edges() > 0, "no edges")
    j = ok(nx.johnson, h)
    for a in list(h)[:4]:
        d = ok(nx.single_source_dijkstra_path_length, h, a)
        stitch.check(set(j[a]) == set(d), "Johnson reaches the same nodes as Dijkstra")
        for b, path in j[a].items():
            stitch.check(close(nx.path_weight(h, path, "weight"), d[b]), "Johnson paths are shortest")


@stitch.endpoint(bails_on=NXERR)
def spanning_trees_agree(g: Borrow[G], s: Src[64]):
    small(g, 30)
    simple_undirected(g)
    h = weighted(g.copy(), s)
    w = {}
    for alg in ("kruskal", "prim", "boruvka"):
        t = ok(nx.minimum_spanning_tree, h, algorithm=alg)
        stitch.check(nx.is_forest(t) and set(t) == set(h), f"{alg}: a spanning forest")
        stitch.check(nx.number_connected_components(t) == nx.number_connected_components(h), f"{alg}: spans every component")
        w[alg] = t.size(weight="weight")
    stitch.log(f"weights {w}")
    stitch.check(len(set(w.values())) == 1, "Kruskal, Prim and Boruvka find the same weight")


@stitch.endpoint(bails_on=NXERR)
def components_agree(g: Borrow[G]):
    if g.is_directed():
        a = sorted(map(sorted, map(lambda c: list(map(str, c)), nx.strongly_connected_components(g))))
        b = sorted(map(sorted, map(lambda c: list(map(str, c)), nx.kosaraju_strongly_connected_components(g))))
        c = sorted(map(sorted, map(lambda c: list(map(str, c)), nx.strongly_connected_components_recursive(g))))
        stitch.check(a == b == c, "Tarjan, Kosaraju and recursive SCCs agree")
        cond = nx.condensation(g)
        stitch.check(nx.is_directed_acyclic_graph(cond), "the condensation is a DAG")
        stitch.check(cond.number_of_nodes() == len(a), "one condensation node per SCC")
    else:
        comps = list(nx.connected_components(g))
        stitch.check(len(comps) == nx.number_connected_components(g), "component count")
        stitch.check(sum(map(len, comps)) == g.number_of_nodes(), "components partition the nodes")
        if not g.is_multigraph() and g.number_of_nodes():
            basis = nx.cycle_basis(g)
            loops = nx.number_of_selfloops(g)
            stitch.check(len(basis) == g.number_of_edges() - g.number_of_nodes() + len(comps), "cycle basis size == m - n + c")


@stitch.endpoint(bails_on=NXERR)
def topo_sorts_valid(g: Borrow[G]):
    stitch.require(g.is_directed() and nx.is_directed_acyclic_graph(g), "needs a DAG")
    for order in (list(nx.topological_sort(g)), list(nx.lexicographical_topological_sort(g, key=str))):
        pos = {n: i for i, n in enumerate(order)}
        stitch.check(len(pos) == g.number_of_nodes(), "every node once")
        stitch.check(all(pos[u] < pos[v] for u, v, *_ in g.edges), "edges go forward")
    gens = list(nx.topological_generations(g))
    stitch.check(sum(map(len, gens)) == g.number_of_nodes(), "generations cover the nodes")
    if not g.is_multigraph():
        tr = nx.transitive_reduction(g)
        tc = nx.transitive_closure_dag(g)
        stitch.check(set(nx.transitive_closure_dag(tr).edges) == set(tc.edges), "the reduction has the same closure")
        stitch.check(nx.dag_longest_path_length(g) == max(len(gens) - 1, 0), "longest path length == generations - 1")


@stitch.endpoint(bails_on=NXERR)
def colorings_valid(g: Borrow[G]):
    small(g, 30)
    stitch.require(not g.is_directed(), "undirected")
    for strategy in ("largest_first", "random_sequential", "smallest_last", "independent_set", "connected_sequential_bfs",
                     "connected_sequential_dfs", "saturation_largest_first"):
        c = ok(nx.greedy_color, g, strategy)
        stitch.check(set(c) == set(g), f"{strategy}: every node colored")
        stitch.check(all(c[u] != c[v] for u, v, *_ in g.edges if u != v), f"{strategy}: a proper coloring")


@stitch.endpoint(bails_on=NXERR)
def matchings_valid(g: Borrow[G], s: Src[64]):
    small(g, 30)
    simple_undirected(g)
    no_loops = nx.number_of_selfloops(g) == 0
    h = weighted(g.copy(), s)
    m1 = ok(nx.max_weight_matching, h)
    stitch.check(nx.is_matching(h, m1), "max_weight_matching is a matching")
    m2 = ok(nx.max_weight_matching, h, maxcardinality=True)
    stitch.check(nx.is_matching(h, m2), "maxcardinality matching is a matching")
    m3 = nx.maximal_matching(h)
    stitch.check(nx.is_maximal_matching(h, m3), "maximal_matching is maximal")
    if no_loops and nx.is_bipartite(h) and nx.is_connected(h) and len(h) > 1:
        hk = nx.bipartite.hopcroft_karp_matching(h)
        stitch.check(len(hk) // 2 == len(m2), "Hopcroft-Karp and blossom agree on cardinality")


@stitch.endpoint(bails_on=NXERR)
def euler_valid(g: Borrow[G]):
    small(g, 30)
    if nx.is_eulerian(g):
        circ = list(nx.eulerian_circuit(g, keys=g.is_multigraph()))
        stitch.check(len(circ) == g.number_of_edges(), "an Eulerian circuit uses every edge once")
        stitch.check(not circ or circ[0][0] == circ[-1][1], "the circuit is closed")
    if nx.has_eulerian_path(g):
        path = list(nx.eulerian_path(g, keys=g.is_multigraph()))
        stitch.check(len(path) == g.number_of_edges(), "an Eulerian path uses every edge once")
        stitch.check(all(path[i][1] == path[i + 1][0] for i in range(len(path) - 1)), "the path is contiguous")


@stitch.endpoint(bails_on=NXERR)
def planarity_valid(g: Borrow[G]):
    small(g, 30)
    is_planar, emb = nx.check_planarity(g)
    if is_planar:
        emb.check_structure()
        stitch.check(set(emb.nodes) == set(g.nodes), "the embedding has every node")
        stitch.check(nx.Graph(emb.to_undirected()).number_of_edges() == nx.Graph(g).number_of_edges() - nx.number_of_selfloops(nx.Graph(g)), "the embedding has every edge")
    else:
        stitch.check(nx.Graph(g).number_of_edges() > 0, "a non-planar graph has edges")
        if not g.is_directed() and not g.is_multigraph():
            k = nx.check_planarity(g, counterexample=True)[1]
            stitch.check(not nx.check_planarity(k)[0], "the counterexample is not planar")


@stitch.endpoint(bails_on=NXERR)
def isomorphism_relabel(g: Borrow[G], s: Src[64]):
    small(g, 18)
    ns = list(g)
    perm = ns[:]
    for i in range(len(perm) - 1, 0, -1):
        j = s.range(0, i)
        perm[i], perm[j] = perm[j], perm[i]
    h = nx.relabel_nodes(g, dict(zip(ns, [f"r{i}" for i in range(len(ns))])))
    h2 = nx.relabel_nodes(h, dict(zip([f"r{i}" for i in range(len(ns))], perm)))
    with stitch.time_limit(1.0):
        stitch.check(nx.is_isomorphic(g, h2), "a relabeled graph is isomorphic")
        stitch.check(nx.faster_could_be_isomorphic(g, h2), "faster_could_be_isomorphic agrees")
        m = (isomorphism.MultiDiGraphMatcher if g.is_multigraph() and g.is_directed() else isomorphism.MultiGraphMatcher if g.is_multigraph()
             else isomorphism.DiGraphMatcher if g.is_directed() else isomorphism.GraphMatcher)(g, h2)
        stitch.check(m.is_isomorphic(), "VF2 agrees")
        if not g.is_multigraph():
            stitch.check(nx.weisfeiler_lehman_graph_hash(g) == nx.weisfeiler_lehman_graph_hash(h2), "WL hash is label-independent")
            if not g.is_directed() and nx.is_tree(g) and len(g) > 0:
                stitch.check(nx.isomorphism.tree_isomorphism(g, h2) != [] or len(g) == 1, "tree isomorphism finds a mapping")
            stitch.check(isomorphism.vf2pp_is_isomorphic(g, h2) if hasattr(isomorphism, "vf2pp_is_isomorphic") else True, "VF2++ agrees")


@stitch.endpoint(bails_on=NXERR)
def lca_agree(g: Borrow[G]):
    small(g, 16)
    stitch.require(g.is_directed() and nx.is_tree(g) and len(g) > 0 and not g.is_multigraph(), "needs a directed tree")
    a = dict(nx.tree_all_pairs_lowest_common_ancestor(g))
    b = dict(nx.all_pairs_lowest_common_ancestor(g))
    stitch.check(all(b[k] == v for k, v in a.items() if k in b), "tree LCA and DAG LCA agree")


@stitch.endpoint(bails_on=NXERR)
def distance_measures_consistent(g: Borrow[G]):
    small(g, 30)
    stitch.require(g.number_of_nodes() > 0 and not g.is_directed() and nx.is_connected(g), "connected undirected")
    e = nx.eccentricity(g)
    stitch.check(nx.diameter(g) == max(e.values()) and nx.radius(g) == min(e.values()), "diameter/radius from eccentricity")
    stitch.check(set(nx.center(g)) == {n for n, x in e.items() if x == min(e.values())}, "center")
    stitch.check(set(nx.periphery(g)) == {n for n, x in e.items() if x == max(e.values())}, "periphery")
    if not g.is_multigraph():
        w = nx.wiener_index(g)
        stitch.check(close(w, sum(sum(d.values()) for _, d in nx.all_pairs_shortest_path_length(g)) / 2), "Wiener index == half the sum of distances")


@stitch.endpoint(bails_on=NXERR)
def roundtrip_formats(g: Borrow[G], which: stitch.OneOf[tuple(FORMATS)]):
    small(g, 30)
    write, read = FORMATS[which]
    h = nx.convert_node_labels_to_integers(g)
    if which in ("Graph6",):
        h = nx.Graph(h)
        nx.Graph.remove_edges_from(h, list(nx.selfloop_edges(h)))
    if which in ("TreeJson",):
        stitch.require(h.is_directed() and nx.is_arborescence(h) if len(h) else False, "needs a rooted tree")
    if which in ("AdjacencyJson", "NodeLinkJson", "CytoscapeJson") and h.is_multigraph():
        # JSON turns tuple edge keys (e.g. from graph products) into unhashable lists
        stitch.require(all(isinstance(k, (int, str)) for *_, k in h.edges(keys=True)), "multigraph keys JSON cannot round-trip")
    try:
        text = write(h)
    except NXERR + (TypeError,) as e:  # (TypeError: json cannot encode some attribute values)
        stitch.bail(type(e).__name__)
    stitch.log(f"{which}: {text[:300]!r}")
    back = read(text)
    stitch.check(back.number_of_nodes() == h.number_of_nodes() or which in ("EdgeList",), f"{which}: same number of nodes")
    stitch.check(nx.Graph(back).number_of_edges() == nx.Graph(h).number_of_edges() or which in ("Sparse6",), f"{which}: same number of edges")


@stitch.endpoint(bails_on=NXERR)
def mutate_and_check(g: Borrow[G], s: Src[64]):
    """Edits keep the graph's views and counts consistent."""
    for _ in range(4):
        if not s.more():
            break
        op = s.choice(5)
        ns = list(g)
        if op == 0:
            g.add_edge(s.range(0, 30), s.range(0, 30))
        elif op == 1 and ns:
            g.remove_node(s.pick(ns))
        elif op == 2 and g.number_of_edges():
            es = list(g.edges(keys=True) if g.is_multigraph() else g.edges)
            g.remove_edge(*s.pick(es))
        elif op == 3:
            g.add_node(s.range(0, 30), label=s.str(4))
        elif op == 4 and ns:
            g.remove_edges_from(list(g.edges(s.pick(ns))))
    stitch.check(sum(d for _, d in g.degree) == 2 * g.number_of_edges() + 0 * nx.number_of_selfloops(g) if not g.is_directed() else True, "handshake lemma")
    if g.is_directed():
        stitch.check(sum(d for _, d in g.in_degree) == g.number_of_edges() == sum(d for _, d in g.out_degree), "in and out degrees sum to m")
    stitch.check(len(list(g.edges)) == g.number_of_edges(), "edge view length")
    stitch.check(all(g.has_node(n) for e in g.edges for n in e[:2]), "edge endpoints are nodes")


@stitch.endpoint(bails_on=NXERR)
def clique_cover_consistent(g: Borrow[G]):
    small(g, 22)
    simple_undirected(g)
    with stitch.time_limit(1.0):
        cl = list(nx.find_cliques(g))
        rec = list(nx.find_cliques_recursive(g))
        stitch.check(sorted(map(sorted, map(lambda c: list(map(str, c)), cl))) == sorted(map(sorted, map(lambda c: list(map(str, c)), rec))),
                     "find_cliques and find_cliques_recursive agree")
        w, _ = nx.max_weight_clique(g, weight=None)
        stitch.check(len(w) == max((len(c) for c in cl), default=0), "max_weight_clique has the clique number")
        for c in cl:
            stitch.check(all(g.has_edge(u, v) for u, v in itertools.combinations(c, 2)), "a clique is complete")


@stitch.endpoint(bails_on=NXERR)
def core_numbers_consistent(g: Borrow[G]):
    simple(g)
    stitch.require(nx.number_of_selfloops(g) == 0, "self-loops")
    core = nx.core_number(g)
    k = max(core.values(), default=0)
    kc = nx.k_core(g, k)
    stitch.check(set(kc) == {n for n, c in core.items() if c >= k}, "k_core is the nodes with core number >= k")
    if len(kc):
        stitch.check(min(d for _, d in (kc.degree if not g.is_directed() else nx.Graph(kc).degree)) >= k or g.is_directed(), "every node of the k-core has degree >= k")


@stitch.endpoint(bails_on=NXERR)
def chordal_consistent(g: Borrow[G]):
    small(g, 25)
    simple_undirected(g)
    stitch.require(nx.number_of_selfloops(g) == 0, "self-loops")
    h, _ = nx.complete_to_chordal_graph(g)
    stitch.check(nx.is_chordal(h), "complete_to_chordal_graph gives a chordal graph")
    stitch.check(all(h.has_edge(u, v) for u, v in g.edges), "it keeps every edge")


@stitch.endpoint(bails_on=NXERR)
def dominators_consistent(g: Borrow[G], s: Src[8]):
    small(g, 30)
    stitch.require(g.is_directed(), "directed")
    r = pick_node(g, s)
    idom = nx.immediate_dominators(g, r)
    reach = nx.descendants(g, r) | {r}
    stitch.check(set(idom) == reach, "every reachable node has an immediate dominator")
    for n, d in idom.items():
        if n != r:
            h = g.copy()
            h.remove_node(d)
            stitch.check(not nx.has_path(h, r, n) if d != r else True, "removing a node's dominator cuts it off")


@stitch.endpoint(bails_on=NXERR)
def communities_partition(g: Borrow[G]):
    small(g, 30)
    stitch.require(not g.is_directed(), "undirected")
    for name, fn in (("louvain", lambda: nx.community.louvain_communities(g, seed=1)),
                     ("greedy_modularity", lambda: nx.community.greedy_modularity_communities(g)),
                     ("label_propagation", lambda: nx.community.label_propagation_communities(g))):
        try:
            parts = list(fn())
        except NXERR:
            continue
        stitch.check(nx.community.is_partition(g, parts), f"{name}: a partition of the nodes")
        if g.number_of_edges():
            q = nx.community.modularity(g, parts)
            stitch.check(-0.5 - 1e-9 <= q <= 1 + 1e-9, f"{name}: modularity in [-1/2, 1]")


@stitch.endpoint(bails_on=NXERR)
def centrality_ranges(g: Borrow[G]):
    small(g, 25)
    n = g.number_of_nodes()
    stitch.require(n > 2, "needs 3 nodes")
    for name, fn in (("betweenness", nx.betweenness_centrality), ("closeness", nx.closeness_centrality), ("harmonic", lambda h: nx.harmonic_centrality(h))):
        c = fn(g)
        stitch.check(set(c) == set(g), f"{name}: one value per node")
        stitch.check(all(not math.isnan(v) and v >= 0 for v in c.values()), f"{name}: non-negative")
    b = nx.betweenness_centrality(g, normalized=True)
    stitch.check(all(v <= 1 + 1e-9 for v in b.values()), "normalized betweenness <= 1")
    try:
        pr = nx.pagerank(g)
        stitch.check(abs(sum(pr.values()) - 1) < 1e-6, "pagerank sums to 1")
    except nx.PowerIterationFailedConvergence:
        pass


@stitch.endpoint(bails_on=NXERR)
def subgraph_views(g: Borrow[G], s: Src[64]):
    keep = [n for n in g if s.coin()]
    v = g.subgraph(keep)
    c = v.copy()
    stitch.check(same_graph(nx.Graph(v), nx.Graph(c)) if not g.is_directed() else set(v.edges) == set(c.edges), "a subgraph view equals its copy")
    r = nx.restricted_view(g, [n for n in g if n not in keep], [])
    stitch.check(set(r) == set(keep), "restricted_view hides the removed nodes")
    if g.is_directed():
        rv = g.reverse(copy=False)
        stitch.check(set((v, u) for u, v, *_ in g.edges) == set((u, v) for u, v, *_ in rv.edges), "reverse view swaps edges")
