"""Algorithms, registered from tables. Each runs on a borrowed graph; oracles live in checks.py,
which compares algorithms that must agree."""
import stitch
from harness.common import nx
approx = nx.algorithms.approximation
bipartite, community, isomorphism = nx.algorithms.bipartite, nx.algorithms.community, nx.algorithms.isomorphism
from harness.common import (algo, connected, directed, no_selfloops, pick_edge, pick_node, simple, simple_directed,
                            simple_undirected, small, undirected)


def tiny(n):
    return lambda g: small(g, n)


S = [simple]
SU = [simple_undirected]
SD = [simple_directed]
U = [undirected]
D = [directed]
CU = [simple_undirected, connected]

# whole-graph measures and predicates
TABLE = {
    # structure
    "density": (nx.density, []), "is_directed_acyclic_graph": (nx.is_directed_acyclic_graph, []),
    "number_connected_components": (nx.number_connected_components, U), "is_connected": (nx.is_connected, U),
    "connected_components": (nx.connected_components, U), "is_biconnected": (nx.is_biconnected, SU),
    "biconnected_components": (nx.biconnected_components, SU), "articulation_points": (nx.articulation_points, SU),
    "bridges": (nx.bridges, U), "local_bridges": (lambda g: nx.local_bridges(g, with_span=True), SU), "has_bridges": (nx.has_bridges, U),
    "strongly_connected_components": (nx.strongly_connected_components, D),
    "kosaraju_strongly_connected_components": (nx.kosaraju_strongly_connected_components, D),
    "strongly_connected_components_recursive": (nx.strongly_connected_components_recursive, D),
    "attracting_components": (nx.attracting_components, D), "is_semiconnected": (nx.is_semiconnected, D),
    "weakly_connected_components": (nx.weakly_connected_components, D),
    "is_strongly_connected": (nx.is_strongly_connected, D),
    "node_connectivity": (nx.node_connectivity, [tiny(16)]), "edge_connectivity": (nx.edge_connectivity, [tiny(16)]),
    "minimum_node_cut": (nx.minimum_node_cut, [tiny(14)]), "minimum_edge_cut": (nx.minimum_edge_cut, [tiny(14)]),
    "k_components": (nx.k_components, SU + [tiny(10)]), "all_node_cuts": (nx.all_node_cuts, CU + [tiny(10)]),
    "k_edge_components": (lambda g: nx.k_edge_components(g, 2), S + [tiny(16)]),

    # cycles
    "cycle_basis": (nx.cycle_basis, SU), "minimum_cycle_basis": (nx.minimum_cycle_basis, SU + [tiny(14)]),
    "simple_cycles": (nx.simple_cycles, SD + [tiny(12)]), "recursive_simple_cycles": (nx.recursive_simple_cycles, SD + [tiny(10)]),
    "find_cycle": (nx.find_cycle, []),
    # DAGs
    "topological_sort": (nx.topological_sort, D), "lexicographical_topological_sort": (lambda g: nx.lexicographical_topological_sort(g, key=str), D),
    "all_topological_sorts": (nx.all_topological_sorts, D + [tiny(8)]), "topological_generations": (nx.topological_generations, D),
    "dag_longest_path": (nx.dag_longest_path, D), "dag_longest_path_length": (nx.dag_longest_path_length, D),
    "antichains": (nx.antichains, D + [tiny(10)]), "is_aperiodic": (nx.is_aperiodic, D),
    "is_branching": (nx.is_branching, D), "is_arborescence": (nx.is_arborescence, D),
    # trees
    "is_tree": (nx.is_tree, []), "is_forest": (nx.is_forest, []),
    "maximum_branching": (nx.maximum_branching, D), "minimum_branching": (nx.minimum_branching, D),
    "maximum_spanning_arborescence": (nx.maximum_spanning_arborescence, D + [tiny(14)]),
    "minimum_spanning_arborescence": (nx.minimum_spanning_arborescence, D + [tiny(14)]),
    "to_prufer_sequence": (lambda g: _documented(KeyError, nx.to_prufer_sequence, g), SU), "to_nested_tuple": (lambda g: nx.to_nested_tuple(g, min(g, key=str)), SU),
    "junction_tree": (nx.junction_tree, [tiny(12)]),
    "minimum_spanning_edges_boruvka": (lambda g: nx.minimum_spanning_edges(g, algorithm="boruvka"), SU),
    # distances
    "diameter": (nx.diameter, CU), "radius": (nx.radius, CU), "center": (nx.center, CU), "periphery": (nx.periphery, CU),
    "eccentricity": (nx.eccentricity, CU), "barycenter": (nx.barycenter, CU), "wiener_index": (nx.wiener_index, []),
    "average_shortest_path_length": (nx.average_shortest_path_length, [connected]),
    "global_efficiency": (nx.global_efficiency, SU), "local_efficiency": (nx.local_efficiency, SU),
    "floyd_warshall": (nx.floyd_warshall, [tiny(25)]), "floyd_warshall_predecessor_and_distance": (nx.floyd_warshall_predecessor_and_distance, [tiny(25)]),
    "all_pairs_shortest_path_length": (lambda g: dict(nx.all_pairs_shortest_path_length(g)), []),
    "all_pairs_dijkstra_path_length": (lambda g: dict(nx.all_pairs_dijkstra_path_length(g)), []),
    "all_pairs_bellman_ford_path_length": (lambda g: dict(nx.all_pairs_bellman_ford_path_length(g)), []),
    "johnson": (nx.johnson, [tiny(20)]), "negative_edge_cycle": (nx.negative_edge_cycle, []),
    "resistance_distance_all": (lambda g: [nx.resistance_distance(g, a, b) for a in list(g)[:2] for b in list(g)[:3] if a != b], CU + [tiny(14)]),
    # centrality
    "degree_centrality": (nx.degree_centrality, []), "closeness_centrality": (nx.closeness_centrality, []),
    "betweenness_centrality": (nx.betweenness_centrality, [tiny(30)]), "edge_betweenness_centrality": (nx.edge_betweenness_centrality, [tiny(30)]),
    "load_centrality": (nx.load_centrality, [tiny(25)]), "harmonic_centrality": (nx.harmonic_centrality, []),
    "eigenvector_centrality": (lambda g: nx.eigenvector_centrality(g, max_iter=200), []),
    "eigenvector_centrality_numpy": (nx.eigenvector_centrality_numpy, [tiny(30)]), "katz_centrality": (lambda g: nx.katz_centrality(g, max_iter=200), []),
    "katz_centrality_numpy": (nx.katz_centrality_numpy, [tiny(30)]), "pagerank": (nx.pagerank, []), "hits": (nx.hits, []),
    "communicability_betweenness_centrality": (nx.communicability_betweenness_centrality, SU + [tiny(14)]),
    "current_flow_closeness_centrality": (nx.current_flow_closeness_centrality, CU + [tiny(20)]),
    "current_flow_betweenness_centrality": (nx.current_flow_betweenness_centrality, CU + [tiny(20)]),
    "information_centrality": (nx.information_centrality, CU + [tiny(20)]),
    "subgraph_centrality": (nx.subgraph_centrality, SU + [tiny(20)]), "estrada_index": (nx.estrada_index, SU + [tiny(20)]),
    "second_order_centrality": (nx.second_order_centrality, CU + [tiny(16)]), "trophic_levels": (nx.trophic_levels, SD),
    "voterank": (nx.voterank, []), "percolation_centrality": (nx.percolation_centrality, [tiny(20)]),
    "dispersion": (lambda g: nx.dispersion(g), SU + [tiny(16)]), "global_reaching_centrality": (nx.global_reaching_centrality, [tiny(20)]),
    "group_degree_centrality": (lambda g: nx.group_degree_centrality(g, list(g)[:2]), [tiny(30)]),
    "group_betweenness_centrality": (lambda g: nx.group_betweenness_centrality(g, list(g)[:2]), [tiny(20)]),
    # clustering, cores, assortativity
    "clustering": (nx.clustering, S), "average_clustering": (nx.average_clustering, S), "transitivity": (nx.transitivity, S),
    "triangles": (nx.triangles, SU), "square_clustering": (nx.square_clustering, SU), "generalized_degree": (nx.generalized_degree, SU),
    "core_number": (nx.core_number, [simple, no_selfloops]), "onion_layers": (nx.onion_layers, SU + [no_selfloops]),
    "degree_assortativity_coefficient": (nx.degree_assortativity_coefficient, []), "degree_pearson_correlation_coefficient": (nx.degree_pearson_correlation_coefficient, []),
    "average_neighbor_degree": (nx.average_neighbor_degree, []), "average_degree_connectivity": (nx.average_degree_connectivity, []),
    "rich_club_coefficient": (lambda g: nx.rich_club_coefficient(g, normalized=False), SU), "s_metric": (lambda g: nx.s_metric(g, normalized=False), []),
    "degree_histogram": (nx.degree_histogram, []), "reciprocity": (lambda g: nx.overall_reciprocity(g), D),
    "flow_hierarchy": (nx.flow_hierarchy, D), "number_of_isolates": (nx.number_of_isolates, []),
    "effective_size": (nx.effective_size, [tiny(25)]), "constraint": (nx.constraint, [tiny(25)]),
    # cliques, independence, covers, coloring
    "find_cliques": (nx.find_cliques, SU + [tiny(25)]), "find_cliques_recursive": (nx.find_cliques_recursive, SU + [tiny(25)]),
    "enumerate_all_cliques": (nx.enumerate_all_cliques, SU + [tiny(16)]), "graph_clique_number": (lambda g: max((len(c) for c in nx.find_cliques(g)), default=0), SU + [tiny(25)]),
    "node_clique_number": (nx.node_clique_number, SU + [tiny(20)]), "max_weight_clique": (lambda g: nx.max_weight_clique(g, weight=None), SU + [tiny(18)]),
    "maximal_independent_set": (lambda g: nx.maximal_independent_set(g, seed=1), SU),
    "greedy_color_largest_first": (lambda g: nx.greedy_color(g, "largest_first"), U), "greedy_color_dsatur": (lambda g: nx.greedy_color(g, "DSATUR"), U),
    "greedy_color_smallest_last": (lambda g: nx.greedy_color(g, "smallest_last"), U), "greedy_color_independent_set": (lambda g: nx.greedy_color(g, "independent_set"), U),
    "greedy_color_connected_sequential_bfs": (lambda g: nx.greedy_color(g, "connected_sequential_bfs"), U),
    "greedy_color_saturation_interchange": (lambda g: nx.greedy_color(g, "largest_first", interchange=True), U),
    "equitable_color": (lambda g: nx.equitable_color(g, max(dict(g.degree).values(), default=0) + 1), SU),
    "min_weighted_vertex_cover": (approx.min_weighted_vertex_cover, U), "min_weighted_dominating_set": (approx.min_weighted_dominating_set, U),
    "dominating_set": (nx.dominating_set, []), "maximum_independent_set_approx": (approx.maximum_independent_set, SU),
    "max_clique_approx": (approx.max_clique, SU), "large_clique_size": (approx.large_clique_size, SU),
    "treewidth_min_degree": (approx.treewidth_min_degree, SU), "treewidth_min_fill_in": (approx.treewidth_min_fill_in, SU),
    "average_clustering_approx": (lambda g: approx.average_clustering(g, trials=50, seed=1), SU),
    "traveling_salesman_christofides": (lambda g: approx.traveling_salesman_problem(g, cycle=True), CU + [tiny(12)]),
    "randomized_partitioning": (lambda g: approx.randomized_partitioning(g, seed=1), SU),
    "one_exchange": (lambda g: approx.one_exchange(g, seed=1), SU + [tiny(20)]),
    # matching
    "max_weight_matching": (nx.max_weight_matching, SU), "maximal_matching": (nx.maximal_matching, SU),
    "min_weight_matching": (nx.min_weight_matching, SU), "max_cardinality_matching": (lambda g: nx.max_weight_matching(g, maxcardinality=True), SU),
    # euler, planarity, chordal, bipartite
    "is_eulerian": (nx.is_eulerian, []), "has_eulerian_path": (nx.has_eulerian_path, []), "is_semieulerian": (nx.is_semieulerian, []),
    "eulerian_circuit": (nx.eulerian_circuit, []), "eulerian_path": (nx.eulerian_path, []), "eulerize": (nx.eulerize, U),
    "check_planarity": (lambda g: nx.check_planarity(g)[0], [tiny(30)]), "is_chordal": (nx.is_chordal, SU),
    "chordal_graph_cliques": (nx.chordal_graph_cliques, SU), "chordal_graph_treewidth": (nx.chordal_graph_treewidth, SU),
    "complete_to_chordal_graph": (lambda g: nx.complete_to_chordal_graph(g)[1], SU),
    "is_bipartite": (nx.is_bipartite, []), "bipartite_sets": (bipartite.sets, CU), "bipartite_color": (bipartite.color, []),
    "bipartite_hopcroft_karp": (bipartite.hopcroft_karp_matching, CU), "bipartite_eppstein": (bipartite.eppstein_matching, CU),
    "bipartite_density_top": (lambda g: bipartite.density(g, bipartite.sets(g)[0]), CU),
    "is_distance_regular": (nx.is_distance_regular, SU), "is_regular": (nx.is_regular, []), "is_k_regular": (lambda g: nx.is_k_regular(g, 3), U),
    "intersection_array": (nx.intersection_array, SU), "is_strongly_regular": (nx.is_strongly_regular, SU),
    "is_at_free": (nx.is_at_free, SU + [tiny(16)]), "find_asteroidal_triple": (nx.find_asteroidal_triple, SU + [tiny(16)]),
    "is_tournament": (nx.tournament.is_tournament, D), "tournament_hamiltonian_path": (nx.tournament.hamiltonian_path, D),
    "chain_decomposition": (nx.chain_decomposition, SU),
    # communities
    "label_propagation_communities": (community.label_propagation_communities, U),
    "asyn_lpa_communities": (lambda g: community.asyn_lpa_communities(g, seed=1), []),
    "greedy_modularity_communities": (community.greedy_modularity_communities, [tiny(30)]),
    "louvain_communities": (lambda g: community.louvain_communities(g, seed=1), [tiny(30)]),
    "girvan_newman": (lambda g: next(community.girvan_newman(g), None), SU + [tiny(16)]),
    "kernighan_lin_bisection": (lambda g: community.kernighan_lin_bisection(g, seed=1), SU),
    "asyn_fluidc": (lambda g: community.asyn_fluidc(g, 2, seed=1), CU), "k_clique_communities": (lambda g: community.k_clique_communities(g, 3), SU + [tiny(20)]),
    "lukes_partitioning": (lambda g: community.lukes_partitioning(g, 4), [tiny(16)]),
    # isomorphism-ish, similarity, misc
    "could_be_isomorphic_self": (lambda g: nx.could_be_isomorphic(g, g), []),
    "weisfeiler_lehman_graph_hash": (nx.weisfeiler_lehman_graph_hash, []),
    "weisfeiler_lehman_subgraph_hashes": (nx.weisfeiler_lehman_subgraph_hashes, []),
    "triadic_census": (nx.triadic_census, SD + [tiny(25)]), "all_triads_count": (lambda g: sum(1 for _ in nx.all_triads(g)), SD + [tiny(10)]),
    "non_randomness": (lambda g: nx.non_randomness(g, 2), CU + [no_selfloops]),
    "sigma": (lambda g: nx.sigma(g, niter=1, nrand=1, seed=1), CU + [tiny(12)]),
    "dominance_frontiers": (lambda g: nx.dominance_frontiers(g, min(g, key=str)), D),
    "immediate_dominators": (lambda g: nx.immediate_dominators(g, min(g, key=str)), D),
    "is_graphical_hh": (lambda g: nx.is_graphical([d for _, d in g.degree], "hh"), []),
    "is_graphical_eg": (lambda g: nx.is_graphical([d for _, d in g.degree], "eg"), []),
    "is_digraphical": (lambda g: nx.is_digraphical([d for _, d in g.in_degree], [d for _, d in g.out_degree]), D),
    "is_multigraphical": (lambda g: nx.is_multigraphical([d for _, d in g.degree]), []),
    "estrada_index_sum": (lambda g: sum(nx.subgraph_centrality_exp(g).values()), SU + [tiny(14)]),
    "laplacian_spectrum": (nx.laplacian_spectrum, [tiny(30)]), "adjacency_spectrum": (nx.adjacency_spectrum, [tiny(30)]),
    "normalized_laplacian_spectrum": (nx.normalized_laplacian_spectrum, U + [tiny(30)]),
    "algebraic_connectivity": (lambda g: nx.algebraic_connectivity(g, seed=1), CU + [tiny(30)]),
    "fiedler_vector": (lambda g: nx.fiedler_vector(g, seed=1), CU + [tiny(30)]),
    "spectral_ordering": (lambda g: nx.spectral_ordering(g, seed=1), U + [tiny(30)]),
    "bethe_hessian_spectrum": (nx.bethe_hessian_spectrum, U + [tiny(30)]),
    "spring_layout": (lambda g: nx.spring_layout(g, seed=1, iterations=20), [tiny(30)]),
    "kamada_kawai_layout": (nx.kamada_kawai_layout, [tiny(16)]), "spectral_layout": (nx.spectral_layout, [tiny(30)]),
    "planar_layout": (nx.planar_layout, [tiny(30)]), "shell_layout": (nx.shell_layout, []), "bipartite_layout": (lambda g: nx.bipartite_layout(g, list(g)[: len(g) // 2]), []),
    "multipartite_layout_default": (lambda g: nx.circular_layout(g), []), "spiral_layout": (nx.spiral_layout, []),
    "to_numpy_array": (nx.to_numpy_array, [tiny(40)]), "to_scipy_sparse_array": (lambda g: nx.to_scipy_sparse_array(g).nnz, [tiny(40)]),
    "to_dict_of_dicts": (nx.to_dict_of_dicts, []), "to_dict_of_lists": (nx.to_dict_of_lists, []), "to_edgelist": (nx.to_edgelist, []),
    "incidence_matrix": (lambda g: nx.incidence_matrix(g).shape, [tiny(40)]), "modularity_matrix": (nx.modularity_matrix, U + [tiny(30)]),
    "node_redundancy": (bipartite.node_redundancy, SU + [tiny(16)]), "stoer_wagner": (nx.stoer_wagner, CU + [tiny(16)]),
    "gomory_hu_tree": (nx.gomory_hu_tree, CU + [tiny(14)]),
    "volume_all": (lambda g: nx.volume(g, list(g)), []), "boundary_expansion": (lambda g: nx.boundary_expansion(g, list(g)[: len(g) // 2]), []),
    "communicability": (nx.communicability, SU + [tiny(16)]), "communicability_exp": (nx.communicability_exp, SU + [tiny(16)]),
    "simrank_similarity": (lambda g: nx.simrank_similarity(g, max_iterations=20), [tiny(16)]),
    "panther_similarity": (lambda g: nx.panther_similarity(g, min(g, key=str), k=3, path_length=3), SU + [tiny(16)]),
    "is_isolate_all": (lambda g: [nx.is_isolate(g, n) for n in g], []),
    "lowest_common_ancestors_all": (lambda g: dict(nx.all_pairs_lowest_common_ancestor(g)), D + [tiny(16)]),
    "tree_all_pairs_lca": (lambda g: dict(nx.tree_all_pairs_lowest_common_ancestor(g)), D + [tiny(16)]),
    "hamiltonian_path_tournament_check": (lambda g: nx.is_strongly_connected(g) and nx.tournament.is_strongly_connected(g), D),
    "summarization_dedensify": (lambda g: nx.dedensify(g, threshold=2)[0], D),
    "snap_aggregation": (lambda g: nx.snap_aggregation(g, node_attributes=()), [tiny(30)]),
    "double_edge_swap": (lambda g: nx.double_edge_swap(g.copy(), nswap=2, max_tries=50, seed=1), SU),
    "connected_double_edge_swap": (lambda g: nx.connected_double_edge_swap(g.copy(), nswap=2, seed=1), CU),
    "random_reference": (lambda g: nx.random_reference(g, niter=1, seed=1), CU + [tiny(20)]),
    "average_node_connectivity": (nx.average_node_connectivity, [tiny(10)]),
    "all_pairs_node_connectivity": (nx.all_pairs_node_connectivity, [tiny(10)]),
    "edge_disjoint_paths_first_two": (lambda g: list(nx.edge_disjoint_paths(g, *list(g)[:2])) if len(g) > 1 else None, [tiny(20)]),
    "node_disjoint_paths_first_two": (lambda g: list(nx.node_disjoint_paths(g, *list(g)[:2])) if len(g) > 1 else None, [tiny(20)]),
}
# exceptions networkx documents for bad input to these (not its own exception classes)
EXTRA_BAILS = {
    "eigenvector_centrality": (nx.PowerIterationFailedConvergence,), "katz_centrality": (nx.PowerIterationFailedConvergence,),
    "hits": (nx.PowerIterationFailedConvergence,), "pagerank": (nx.PowerIterationFailedConvergence,),
    # raises a bare Exception on graphs with self-loops, by design
    "rich_club_coefficient": (Exception,),
}
def _documented(exc, fn, *a):
    """fn(*a), where `exc` is a documented failure (bails)."""
    try:
        return fn(*a)
    except exc as e:
        stitch.bail(f"documented {type(e).__name__}")


for name, (fn, pre) in TABLE.items():
    algo(name, fn, pre=pre, bails=EXTRA_BAILS.get(name, ()), slow=0.5)

# algorithms that take nodes of the graph
SRC = {
    "shortest_path": lambda g, s: nx.shortest_path(g, pick_node(g, s), pick_node(g, s)),
    "dijkstra_path": lambda g, s: nx.dijkstra_path(g, pick_node(g, s), pick_node(g, s)),
    "bellman_ford_path": lambda g, s: nx.bellman_ford_path(g, pick_node(g, s), pick_node(g, s)),
    "astar_path": lambda g, s: nx.astar_path(g, pick_node(g, s), pick_node(g, s)),
    "bidirectional_dijkstra": lambda g, s: nx.bidirectional_dijkstra(g, pick_node(g, s), pick_node(g, s)),
    "bidirectional_shortest_path": lambda g, s: nx.bidirectional_shortest_path(g, pick_node(g, s), pick_node(g, s)),
    "all_shortest_paths": lambda g, s: nx.all_shortest_paths(g, pick_node(g, s), pick_node(g, s)),
    "all_simple_paths": lambda g, s: nx.all_simple_paths(g, pick_node(g, s), pick_node(g, s), cutoff=6),
    "all_simple_edge_paths": lambda g, s: nx.all_simple_edge_paths(g, pick_node(g, s), pick_node(g, s), cutoff=6),
    "shortest_simple_paths": lambda g, s: nx.shortest_simple_paths(g, pick_node(g, s), pick_node(g, s)),
    "single_source_dijkstra": lambda g, s: nx.single_source_dijkstra(g, pick_node(g, s)),
    "single_source_bellman_ford": lambda g, s: nx.single_source_bellman_ford(g, pick_node(g, s)),
    "single_source_shortest_path_length": lambda g, s: nx.single_source_shortest_path_length(g, pick_node(g, s), cutoff=s.range(0, 5)),
    "multi_source_dijkstra": lambda g, s: nx.multi_source_dijkstra(g, {pick_node(g, s), pick_node(g, s)}),
    "goldberg_radzik": lambda g, s: nx.goldberg_radzik(g, pick_node(g, s)),
    "predecessor": lambda g, s: nx.predecessor(g, pick_node(g, s)),
    "dijkstra_predecessor_and_distance": lambda g, s: nx.dijkstra_predecessor_and_distance(g, pick_node(g, s)),
    "has_path": lambda g, s: nx.has_path(g, pick_node(g, s), pick_node(g, s)),
    "bfs_edges": lambda g, s: nx.bfs_edges(g, pick_node(g, s), depth_limit=s.range(0, 6)),
    "bfs_layers": lambda g, s: nx.bfs_layers(g, [pick_node(g, s)]),
    "bfs_beam_edges": lambda g, s: nx.bfs_beam_edges(g, pick_node(g, s), lambda n: hash(str(n)) % 7, width=2),
    "dfs_preorder_nodes": lambda g, s: nx.dfs_preorder_nodes(g, pick_node(g, s)),
    "dfs_postorder_nodes": lambda g, s: nx.dfs_postorder_nodes(g, pick_node(g, s)),
    "dfs_labeled_edges": lambda g, s: nx.dfs_labeled_edges(g, pick_node(g, s), depth_limit=s.range(0, 6)),
    "edge_dfs": lambda g, s: nx.edge_dfs(g, pick_node(g, s), orientation=s.pick([None, "original", "reverse", "ignore"]) if g.is_directed() else None),
    "edge_bfs": lambda g, s: nx.edge_bfs(g, pick_node(g, s), orientation=s.pick([None, "original", "reverse", "ignore"]) if g.is_directed() else None),
    "ancestors": lambda g, s: nx.ancestors(g, pick_node(g, s)), "descendants": lambda g, s: nx.descendants(g, pick_node(g, s)),
    "descendants_at_distance": lambda g, s: nx.descendants_at_distance(g, pick_node(g, s), s.range(0, 4)),
    "node_connected_component": lambda g, s: nx.node_connected_component(g, pick_node(g, s)),
    "local_node_connectivity": lambda g, s: nx.connectivity.local_node_connectivity(g, *_two(g, s)),
    "local_edge_connectivity": lambda g, s: nx.connectivity.local_edge_connectivity(g, *_two(g, s)),
    "minimum_st_node_cut": lambda g, s: nx.minimum_node_cut(g, *_two_nonadjacent(g, s)),
    "minimum_st_edge_cut": lambda g, s: nx.minimum_edge_cut(g, *_two(g, s)),
    "lowest_common_ancestor": lambda g, s: nx.lowest_common_ancestor(g, pick_node(g, s), pick_node(g, s)),
    "common_neighbors": lambda g, s: list(nx.common_neighbors(g, pick_node(g, s), pick_node(g, s))),
    "jaccard_coefficient": lambda g, s: nx.jaccard_coefficient(g, [_two(g, s)]),
    "adamic_adar_index": lambda g, s: nx.adamic_adar_index(g, [_two(g, s)]),
    "resource_allocation_index": lambda g, s: nx.resource_allocation_index(g, [_two(g, s)]),
    "preferential_attachment": lambda g, s: nx.preferential_attachment(g, [_two(g, s)]),
    "simrank_pair": lambda g, s: nx.simrank_similarity(g, pick_node(g, s), pick_node(g, s), max_iterations=20),
    "resistance_distance": lambda g, s: nx.resistance_distance(g, *_two(g, s)),
    "voronoi_cells": lambda g, s: nx.voronoi_cells(g, {pick_node(g, s), pick_node(g, s)}),
    "dominating_check": lambda g, s: nx.is_dominating_set(g, [n for n in g if s.coin()]),
    "is_simple_path": lambda g, s: nx.is_simple_path(g, [pick_node(g, s) for _ in range(s.range(0, 5))]),
    "is_path": lambda g, s: nx.is_path(g, [pick_node(g, s) for _ in range(s.range(0, 5))]),
    "path_weight": lambda g, s: nx.path_weight(g, [pick_node(g, s) for _ in range(s.range(0, 5))], "weight"),
    "cut_size": lambda g, s: nx.cut_size(g, [n for n in g if s.coin()]),
    "normalized_cut_size": lambda g, s: nx.normalized_cut_size(g, [n for n in g if s.coin()]),
    "conductance": lambda g, s: nx.conductance(g, [n for n in g if s.coin()]),
    "edge_expansion": lambda g, s: nx.edge_expansion(g, [n for n in g if s.coin()]),
    "node_boundary": lambda g, s: nx.node_boundary(g, [n for n in g if s.coin()]),
    "edge_boundary": lambda g, s: list(nx.edge_boundary(g, [n for n in g if s.coin()])),
    "is_d_separator": lambda g, s: nx.d_separated(g, {pick_node(g, s)}, {pick_node(g, s)}, {n for n in g if s.coin(0.2)}),
    "contracted_nodes": lambda g, s: nx.contracted_nodes(g, *_two(g, s), self_loops=s.coin()),
    "identified_nodes": lambda g, s: nx.identified_nodes(g, *_two(g, s), copy=True),
    "ego_degree": lambda g, s: nx.ego_graph(g, pick_node(g, s), radius=s.range(0, 3), undirected=s.coin()).number_of_nodes(),
    "k_edge_augmentation": lambda g, s: list(nx.k_edge_augmentation(g, k=s.range(1, 2))),
    "edge_current_flow_betweenness_subset": lambda g, s: nx.edge_current_flow_betweenness_centrality_subset(g, [pick_node(g, s)], [pick_node(g, s)]),
    "betweenness_subset": lambda g, s: nx.betweenness_centrality_subset(g, [pick_node(g, s)], [pick_node(g, s)]),
    "remove_edge_check": lambda g, s: _remove_edge(g, s),
}


def _two(g, s):
    ns = list(g)
    stitch_require(len(ns) >= 2, "needs two nodes")
    a = s.pick(ns)
    b = s.pick([n for n in ns if n != a])
    return a, b


def _two_nonadjacent(g, s):
    a, b = _two(g, s)
    stitch_require(not g.has_edge(a, b), "adjacent")
    return a, b


def _remove_edge(g, s):
    h = g.copy()
    e = pick_edge(h, s)
    h.remove_edge(*e)
    return h.number_of_edges() == g.number_of_edges() - 1


def stitch_require(c, why):
    import stitch
    stitch.require(c, why)


SRC_PRE = {
    "local_node_connectivity": [tiny(20)], "local_edge_connectivity": [tiny(20)], "minimum_st_node_cut": [tiny(20)],
    "minimum_st_edge_cut": [tiny(20)], "simrank_pair": [tiny(16)], "resistance_distance": CU + [tiny(16)],
    "lowest_common_ancestor": D, "is_d_separator": D, "all_simple_paths": [tiny(20)], "all_simple_edge_paths": [tiny(20)],
    "shortest_simple_paths": [simple, tiny(20)], "k_edge_augmentation": SU + [tiny(12)], "edge_current_flow_betweenness_subset": CU + [tiny(16)],
    "jaccard_coefficient": SU, "adamic_adar_index": SU, "resource_allocation_index": SU, "preferential_attachment": SU, "common_neighbors": S,
    "node_connected_component": U, "voronoi_cells": [], "ancestors": D, "descendants": D, "descendants_at_distance": [],
}
for name, fn in SRC.items():
    algo(name, fn, pre=SRC_PRE.get(name, []), slow=0.5, src=32)
