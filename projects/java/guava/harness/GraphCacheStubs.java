import com.google.common.cache.*;
import com.google.common.graph.*;
import com.google.common.util.concurrent.AtomicLongMap;
import java.util.*;
import stitch.*;

/** Guava common.graph (mutable graphs and networks against a model, Graphs utilities, Traverser) and caches. */
public class GraphCacheStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    public static class G {
        final MutableGraph<Integer> g;
        final Set<Integer> nodes = new TreeSet<>();
        final Set<List<Integer>> edges = new HashSet<>();
        G(MutableGraph<Integer> g) { this.g = g; }
        List<Integer> e(int a, int b) { return g.isDirected() || a <= b ? Arrays.asList(a, b) : Arrays.asList(b, a); }
        void check() {
            Stitch.check(g.nodes().equals(nodes), "nodes match the model");
            Stitch.check(g.edges().size() == edges.size(), "edge count matches the model");
            for (List<Integer> x : edges) Stitch.check(g.hasEdgeConnecting(x.get(0), x.get(1)), "hasEdgeConnecting");
            int deg = 0;
            for (int n : g.nodes()) deg += g.degree(n);
            int loops = (int) edges.stream().filter(x -> x.get(0).equals(x.get(1))).count();
            Stitch.check(deg == 2 * edges.size(), "degrees sum to twice the edges (self-loops count twice)");
            for (int n : g.nodes()) {
                Stitch.check(g.adjacentNodes(n).containsAll(g.successors(n)), "successors are adjacent");
                Stitch.check(g.incidentEdges(n).size() == (int) edges.stream().filter(x -> x.contains(n)).count(), "incidentEdges");
                if (g.isDirected()) Stitch.check(g.inDegree(n) + g.outDegree(n) == g.degree(n), "in + out == degree");
            }
            void_(loops);
        }
    }
    static void void_(Object o) {}

    @Endpoint
    public static G graph(boolean directed, boolean selfLoops) {
        return new G((directed ? GraphBuilder.directed() : GraphBuilder.undirected()).allowsSelfLoops(selfLoops).build());
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void graph_put_edge(G g, @stitch.Range(lo = 0, hi = 6) int a, @stitch.Range(lo = 0, hi = 6) int b) {
        if (a == b && !g.g.allowsSelfLoops()) { try { g.g.putEdge(a, b); Stitch.check(false, "a self-loop is refused"); } catch (IllegalArgumentException e) { g.check(); return; } }
        g.g.putEdge(a, b); g.nodes.add(a); g.nodes.add(b); g.edges.add(g.e(a, b)); g.check();
    }
    @Endpoint public static void graph_remove_edge(G g, @stitch.Range(lo = 0, hi = 6) int a, @stitch.Range(lo = 0, hi = 6) int b) { g.g.removeEdge(a, b); g.edges.remove(g.e(a, b)); g.check(); }
    @Endpoint public static void graph_remove_node(G g, @stitch.Range(lo = 0, hi = 6) int a) { g.g.removeNode(a); g.nodes.remove(a); g.edges.removeIf(x -> x.contains(a)); g.check(); }
    @Endpoint public static void graph_add_node(G g, @stitch.Range(lo = 0, hi = 6) int a) { g.g.addNode(a); g.nodes.add(a); g.check(); }
    @Endpoint public static void graph_utilities(G g, @stitch.Range(lo = 0, hi = 6) int a) {
        Graph<Integer> tc = Graphs.transitiveClosure(g.g);
        for (int x : g.g.nodes()) for (int y : Graphs.reachableNodes(g.g, x)) Stitch.check(tc.hasEdgeConnecting(x, y) || x == y, "the closure has an edge to every reachable node");
        Stitch.check(Graphs.copyOf(g.g).equals(g.g), "copyOf equals");
        Stitch.check(ImmutableGraph.copyOf(g.g).equals(g.g), "ImmutableGraph.copyOf equals");
        if (g.g.isDirected()) {
            Graph<Integer> t = Graphs.transpose(g.g);
            for (EndpointPair<Integer> e : g.g.edges()) Stitch.check(t.hasEdgeConnecting(e.target(), e.source()), "transpose reverses edges");
            Stitch.check(Graphs.transpose(t).equals(g.g), "transpose twice");
        }
        Graphs.hasCycle(g.g);
        if (g.g.nodes().contains(a)) {
            Set<Integer> bfs = new HashSet<>();
            Traverser.forGraph(g.g).breadthFirst(a).forEach(bfs::add);
            Set<Integer> dfs = new HashSet<>();
            Traverser.forGraph(g.g).depthFirstPreOrder(a).forEach(dfs::add);
            Set<Integer> post = new HashSet<>();
            Traverser.forGraph(g.g).depthFirstPostOrder(a).forEach(post::add);
            Stitch.check(bfs.equals(dfs) && dfs.equals(post) && bfs.equals(Graphs.reachableNodes(g.g, a)), "BFS, DFS and reachableNodes agree");
            Stitch.check(Graphs.inducedSubgraph(g.g, bfs).nodes().equals(bfs), "inducedSubgraph");
        }
    }

    public static class N {
        final MutableNetwork<Integer, String> g;
        final Map<String, List<Integer>> edges = new HashMap<>();
        N(MutableNetwork<Integer, String> g) { this.g = g; }
        void check() {
            Stitch.check(g.edges().equals(edges.keySet()), "edges match the model");
            for (Map.Entry<String, List<Integer>> e : edges.entrySet()) {
                EndpointPair<Integer> p = g.incidentNodes(e.getKey());
                Stitch.check(p.nodeU().equals(e.getValue().get(0)) && p.nodeV().equals(e.getValue().get(1)) || !g.isDirected() && p.nodeU().equals(e.getValue().get(1)) && p.nodeV().equals(e.getValue().get(0)), "incidentNodes");
            }
            for (int n : g.nodes()) Stitch.check(g.incidentEdges(n).size() == (int) edges.values().stream().filter(x -> x.contains(n)).count(), "incidentEdges");
            Stitch.check(g.asGraph().nodes().equals(g.nodes()), "asGraph nodes");
        }
    }
    @Endpoint public static N network(boolean directed, boolean parallel) { return new N((directed ? NetworkBuilder.directed() : NetworkBuilder.undirected()).allowsParallelEdges(parallel).allowsSelfLoops(true).build()); }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void network_add_edge(N n, @stitch.Range(lo = 0, hi = 5) int a, @stitch.Range(lo = 0, hi = 5) int b, @stitch.Range(lo = 0, hi = 9) int id) {
        String e = "e" + id;
        boolean ch = n.g.addEdge(a, b, e);
        if (ch) n.edges.put(e, Arrays.asList(a, b));
        n.check();
    }
    @Endpoint public static void network_remove_edge(N n, @stitch.Range(lo = 0, hi = 9) int id) { n.g.removeEdge("e" + id); n.edges.remove("e" + id); n.check(); }
    @Endpoint public static void network_remove_node(N n, @stitch.Range(lo = 0, hi = 5) int a) { n.g.removeNode(a); n.edges.values().removeIf(x -> x.contains(a)); n.check(); }
    @Endpoint public static void network_utilities(N n) {
        Stitch.check(ImmutableNetwork.copyOf(n.g).equals(n.g), "ImmutableNetwork.copyOf");
        Stitch.check(Graphs.copyOf(n.g).equals(n.g), "Graphs.copyOf(network)");
        if (n.g.isDirected()) Stitch.check(Graphs.transpose(Graphs.transpose(n.g)).equals(n.g), "transpose twice");
        for (int a : n.g.nodes()) for (int b : n.g.nodes()) Stitch.check(n.g.edgesConnecting(a, b).size() >= (n.g.hasEdgeConnecting(a, b) ? 1 : 0), "edgesConnecting");
    }

    public static class VG {
        final MutableValueGraph<Integer, Integer> g;
        final Map<List<Integer>, Integer> model = new HashMap<>();
        VG(MutableValueGraph<Integer, Integer> g) { this.g = g; }
        List<Integer> e(int a, int b) { return g.isDirected() || a <= b ? Arrays.asList(a, b) : Arrays.asList(b, a); }
    }
    @Endpoint public static VG value_graph(boolean directed) { return new VG((directed ? ValueGraphBuilder.directed() : ValueGraphBuilder.undirected()).allowsSelfLoops(true).build()); }
    @Endpoint public static void value_graph_ops(VG v, @stitch.Max(32) Source s) {
        while (s.more()) {
            int a = ri(s, 0, 5), b = ri(s, 0, 5);
            if (s.coin(0.7)) { int w = ri(s, 0, 9); Integer old = v.g.putEdgeValue(a, b, w); Stitch.check(Objects.equals(old, v.model.put(v.e(a, b), w)), "putEdgeValue returns the old value"); }
            else { Integer old = v.g.removeEdge(a, b); Stitch.check(Objects.equals(old, v.model.remove(v.e(a, b))), "removeEdge returns the value"); }
        }
        for (Map.Entry<List<Integer>, Integer> e : v.model.entrySet()) Stitch.check(e.getValue().equals(v.g.edgeValueOrDefault(e.getKey().get(0), e.getKey().get(1), null)), "edgeValue");
        Stitch.check(v.g.edges().size() == v.model.size(), "edge count");
        Stitch.check(ImmutableValueGraph.copyOf(v.g).equals(v.g), "ImmutableValueGraph.copyOf");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void cache_ops(@stitch.Max(64) Source s, @stitch.Range(lo = 0, hi = 4) int max) {
        Cache<String, Integer> c = CacheBuilder.newBuilder().maximumSize(max).recordStats().build();
        Map<String, Integer> model = new LinkedHashMap<>();
        while (s.more()) {
            String k = s.pick("a", "b", "c", "d", "e");
            switch (s.choice(4)) {
                case 0: { int v = ri(s, 0, 9); c.put(k, v); model.put(k, v); break; }
                case 1: { Integer got = c.getIfPresent(k); Stitch.check(got == null || got.equals(model.get(k)), "a present value is the last one put"); break; }
                case 2: c.invalidate(k); model.remove(k); break;
                default: try { int v = c.get(k, () -> 42); Stitch.check(v == 42 || Integer.valueOf(v).equals(model.get(k)), "get(k, loader)"); model.put(k, v); } catch (java.util.concurrent.ExecutionException e) { Stitch.check(false, "loader does not fail"); }
            }
            Stitch.check(c.size() <= Math.max(max, 0) || max == 0 && c.size() == 0, "size <= maximumSize");
            for (Map.Entry<String, Integer> e : c.asMap().entrySet()) Stitch.check(e.getValue().equals(model.get(e.getKey())), "cached entries hold the model's values");
        }
        CacheStats st = c.stats();
        Stitch.check(st.requestCount() == st.hitCount() + st.missCount(), "requests == hits + misses");
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class, com.google.common.util.concurrent.UncheckedExecutionException.class})
    public static void loading_cache(@stitch.Max(32) Source s) throws Exception {
        LoadingCache<Integer, Integer> c = CacheBuilder.newBuilder().maximumWeight(ri(s, 0, 50)).weigher((Integer k, Integer v) -> k % 7).build(CacheLoader.from(k -> k * k));
        while (s.more()) {
            int k = ri(s, 0, 20);
            Stitch.check(c.get(k) == k * k, "LoadingCache.get loads k * k");
            if (s.coin(0.2)) c.refresh(k);
            if (s.coin(0.1)) c.invalidateAll();
        }
        Stitch.check(c.getAll(Arrays.asList(1, 2, 3)).values().containsAll(Arrays.asList(1, 4, 9)), "getAll");
        c.cleanUp();
    }
    @Endpoint public static void cache_builder_spec(@Str(max = 64) String spec) {
        try {
            CacheBuilderSpec p = CacheBuilderSpec.parse(spec);
            Stitch.check(CacheBuilderSpec.parse(p.toParsableString()).equals(p), "parse(toParsableString()) == spec");
            CacheBuilder.from(p).build();
        } catch (IllegalArgumentException e) {
            Stitch.bail("invalid spec");
        }
    }
    @Endpoint public static void atomic_long_map(@stitch.Max(64) Source s) {
        AtomicLongMap<String> m = AtomicLongMap.create();
        Map<String, Long> model = new HashMap<>();
        while (s.more()) {
            String k = s.pick("a", "b", "c");
            long d = ri(s, -5, 5);
            switch (s.choice(4)) {
                case 0: m.addAndGet(k, d); model.merge(k, d, Long::sum); break;
                case 1: m.put(k, d); model.put(k, d); break;
                case 2: m.remove(k); model.remove(k); break;
                default: m.removeAllZeros(); model.values().removeIf(v -> v == 0); break;
            }
            for (Map.Entry<String, Long> e : model.entrySet()) Stitch.check(m.get(e.getKey()) == e.getValue(), "AtomicLongMap matches the model");
        }
        Stitch.check(m.sum() == model.values().stream().mapToLong(Long::longValue).sum(), "sum");
    }
    @Endpoint public static void type_token(@stitch.Range(lo = 0, hi = 5) int which) {
        com.google.common.reflect.TypeToken<?>[] ts = {new com.google.common.reflect.TypeToken<List<String>>() {}, new com.google.common.reflect.TypeToken<Map<String, ? extends Number>>() {},
            com.google.common.reflect.TypeToken.of(int[].class), com.google.common.reflect.TypeToken.of(ArrayList.class), new com.google.common.reflect.TypeToken<Comparable<Integer>>() {}, com.google.common.reflect.TypeToken.of(Integer.class)};
        com.google.common.reflect.TypeToken<?> t = ts[which];
        t.getTypes().rawTypes();
        t.getRawType();
        t.isArray();
        Stitch.check(t.isSupertypeOf(t), "a type is its own supertype");
        t.wrap().unwrap();
        t.toString();
    }
}
