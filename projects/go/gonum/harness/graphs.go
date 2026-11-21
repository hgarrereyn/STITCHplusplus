package harness

import (
	"bytes"
	"math"
	"reflect"
	"sort"

	"golang.org/x/exp/rand"
	"gonum.org/v1/gonum/graph"
	"gonum.org/v1/gonum/graph/coloring"
	"gonum.org/v1/gonum/graph/community"
	"gonum.org/v1/gonum/graph/encoding/dot"
	"gonum.org/v1/gonum/graph/encoding/graph6"
	"gonum.org/v1/gonum/graph/flow"
	"gonum.org/v1/gonum/graph/graphs/gen"
	"gonum.org/v1/gonum/graph/network"
	"gonum.org/v1/gonum/graph/path"
	"gonum.org/v1/gonum/graph/product"
	"gonum.org/v1/gonum/graph/simple"
	"gonum.org/v1/gonum/graph/spectral"
	"gonum.org/v1/gonum/graph/topo"
	"gonum.org/v1/gonum/graph/traverse"
	"stitch"
)

// Graph: a weighted simple graph, directed or not.
type Graph struct {
	d *simple.WeightedDirectedGraph
	u *simple.WeightedUndirectedGraph
}

func (g *Graph) g() graph.Weighted {
	if g.d != nil {
		return g.d
	}
	return g.u
}

func ids(g graph.Graph) []int64 {
	var out []int64
	for it := g.Nodes(); it.Next(); {
		out = append(out, it.Node().ID())
	}
	sort.Slice(out, func(i, j int) bool { return out[i] < out[j] })
	return out
}

func pickNode(g graph.Graph, s *stitch.Source) graph.Node {
	ns := ids(g)
	stitch.Require(len(ns) > 0, "empty graph")
	return g.Node(stitch.Pick(s, ns...))
}

func newGraph(directed bool, n int, s *stitch.Source, neg bool) *Graph {
	var gr *Graph
	if directed {
		gr = &Graph{d: simple.NewWeightedDirectedGraph(0, math.Inf(1))}
	} else {
		gr = &Graph{u: simple.NewWeightedUndirectedGraph(0, math.Inf(1))}
	}
	add := func(e graph.WeightedEdge) {
		if gr.d != nil {
			gr.d.SetWeightedEdge(e)
		} else {
			gr.u.SetWeightedEdge(e)
		}
	}
	for i := 0; i < n; i++ {
		if gr.d != nil {
			gr.d.AddNode(simple.Node(i))
		} else {
			gr.u.AddNode(simple.Node(i))
		}
	}
	lo := int64(0)
	if neg {
		lo = -3
	}
	for s.More() {
		a, b := s.Range(0, int64(n-1)), s.Range(0, int64(n-1))
		if a == b || n < 2 {
			continue
		}
		add(simple.WeightedEdge{F: simple.Node(a), T: simple.Node(b), W: float64(s.Range(lo, 9))})
	}
	return gr
}

func init() {
	stitch.Object[*Graph]("Graph", nil)
	stitch.Endpoint("graph_new", func(directed bool, n int, s *stitch.Source) *Graph { return newGraph(directed, n, s, false) },
		"directed", "n:range=0..12", "s:max=96")
	stitch.Endpoint("graph_new_negative", func(n int, s *stitch.Source) *Graph { return newGraph(true, n, s, true) },
		"n:range=0..10", "s:max=96")
	gens := map[string]func(seed uint64, n int, dst *simple.UndirectedGraph) error{
		"gnp":         func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.Gnp(dst, n, 0.3, rand.NewSource(seed)) },
		"gnm":         func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.Gnm(dst, n, n, rand.NewSource(seed)) },
		"small_world": func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.SmallWorldsBB(dst, n, 2, 0.2, rand.NewSource(seed)) },
		"pref_attach": func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.PreferentialAttachment(dst, n, 2, rand.NewSource(seed)) },
		"tunable":     func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.TunableClusteringScaleFree(dst, n, 2, 0.5, rand.NewSource(seed)) },
		"duplication": func(seed uint64, n int, dst *simple.UndirectedGraph) error { return gen.Duplication(dst, n, 0.5, 0.3, 0.2, rand.NewSource(seed)) },
		"complete":    func(_ uint64, n int, dst *simple.UndirectedGraph) error { gen.Complete(dst, gen.IDRange{First: 0, Last: int64(n) - 1}); return nil },
		"wheel":       func(_ uint64, n int, dst *simple.UndirectedGraph) error { gen.Wheel(dst, 0, gen.IDRange{First: 1, Last: int64(n)}); return nil },
		"tree":        func(_ uint64, n int, dst *simple.UndirectedGraph) error { gen.Tree(dst, 2, gen.IDRange{First: 0, Last: int64(n)}); return nil },
	}
	for _, name := range sortedKeys(gens) {
		mk := gens[name]
		stitch.Endpoint("graph_gen_"+name, func(seed, n int) *Graph {
			var gr *Graph
			guard(func() {
				dst := simple.NewUndirectedGraph()
				if err := mk(uint64(seed), n, dst); err != nil {
					stitch.Bail(err.Error())
				}
				gr = &Graph{u: simple.NewWeightedUndirectedGraph(0, math.Inf(1))}
				for it := dst.Nodes(); it.Next(); {
					gr.u.AddNode(it.Node())
				}
				for it := dst.Edges(); it.Next(); {
					e := it.Edge()
					gr.u.SetWeightedEdge(simple.WeightedEdge{F: e.From(), T: e.To(), W: 1})
				}
			})
			return gr
		}, "seed:range=0..100", "n:range=0..14")
	}

	stitch.Endpoint("shortest_paths_agree", func(gr *Graph, s *stitch.Source) {
		guard(func() {
			g := gr.g()
			u := pickNode(g, s)
			dj := path.DijkstraFrom(u, g)
			bf, ok := path.BellmanFordFrom(u, g)
			stitch.Require(ok, "negative cycle")
			fw, ok2 := path.FloydWarshall(g)
			jo, ok3 := path.JohnsonAllPaths(g)
			da := path.DijkstraAllPaths(g)
			for _, v := range ids(g) {
				d1, d2 := dj.WeightTo(v), bf.WeightTo(v)
				stitch.Check(d1 == d2, "Dijkstra and Bellman-Ford agree")
				if ok2 {
					stitch.Check(fw.Weight(u.ID(), v) == d1, "Floyd-Warshall agrees")
				}
				if ok3 {
					stitch.Check(jo.Weight(u.ID(), v) == d1, "Johnson agrees")
				}
				stitch.Check(da.Weight(u.ID(), v) == d1, "DijkstraAllPaths agrees")
				p, w := dj.To(v)
				if !math.IsInf(w, 1) {
					stitch.Check(topo.IsPathIn(g, p), "Dijkstra's path is a path in the graph")
				}
				as, _ := path.AStar(u, g.Node(v), g, nil)
				stitch.Check(as.WeightTo(v) == d1, "A* (null heuristic) agrees")
			}
		})
	}, "g", "s:max=8")
	stitch.Endpoint("negative_weights", func(gr *Graph, s *stitch.Source) {
		guard(func() {
			g := gr.g()
			u := pickNode(g, s)
			bf, ok := path.BellmanFordFrom(u, g)
			_, ok2 := path.FloydWarshall(g)
			_, ok3 := path.JohnsonAllPaths(g)
			stitch.Check(ok2 == ok3, "Floyd-Warshall and Johnson agree on negative cycles")
			if ok && ok2 {
				fw, _ := path.FloydWarshall(g)
				for _, v := range ids(g) {
					stitch.Check(bf.WeightTo(v) == fw.Weight(u.ID(), v), "Bellman-Ford and Floyd-Warshall agree with negative weights")
				}
			}
			all, okA := path.BellmanFordAllFrom(u, g)
			stitch.Check(okA == ok, "BellmanFordAllFrom agrees on negative cycles")
			_ = all
		})
	}, "g", "s:max=8")
	stitch.Endpoint("yen_k_shortest", func(gr *Graph, s *stitch.Source) {
		guard(func() {
			g := gr.g()
			a, b := pickNode(g, s), pickNode(g, s)
			ps := yen(g, int(s.Range(1, 4)), a, b)
			for _, p := range ps {
				stitch.Check(topo.IsPathIn(g, p), "Yen's paths are paths")
				stitch.Check(len(p) > 0 && p[0].ID() == a.ID() && p[len(p)-1].ID() == b.ID(), "Yen's paths go from s to t")
			}
		})
	}, "g", "s:max=8")
	stitch.Endpoint("spanning_trees_agree", func(gr *Graph) {
		stitch.Require(gr.u != nil, "undirected")
		guard(func() {
			k := simple.NewWeightedUndirectedGraph(0, math.Inf(1))
			p := simple.NewWeightedUndirectedGraph(0, math.Inf(1))
			wk := path.Kruskal(k, gr.u)
			wp := path.Prim(p, gr.u)
			stitch.Check(wk == wp, "Kruskal and Prim find the same weight")
		})
	}, "g")
	stitch.Endpoint("components", func(gr *Graph) {
		guard(func() {
			if gr.u != nil {
				cc := topo.ConnectedComponents(gr.u)
				n := 0
				for _, c := range cc {
					n += len(c)
				}
				stitch.Check(n == len(ids(gr.u)), "components partition the nodes")
				for _, c := range cc {
					for _, x := range c {
						stitch.Check(topo.PathExistsIn(gr.u, c[0], x), "nodes of a component are connected")
					}
				}
				_ = topo.BronKerbosch(gr.u)
				_ = topo.UndirectedCyclesIn(gr.u)
				ord, cores := topo.DegeneracyOrdering(gr.u)
				stitch.Check(len(ord) == len(ids(gr.u)), "degeneracy ordering has every node")
				_ = cores
				_ = topo.KCore(2, gr.u)
				cg := simple.NewUndirectedGraph()
				topo.CliqueGraph(cg, gr.u)
			} else {
				scc := topo.TarjanSCC(gr.d)
				n := 0
				for _, c := range scc {
					n += len(c)
				}
				stitch.Check(n == len(ids(gr.d)), "SCCs partition the nodes")
				_ = topo.DirectedCyclesIn(gr.d)
				order, err := topo.Sort(gr.d)
				if err == nil {
					pos := map[int64]int{}
					for i, x := range order {
						pos[x.ID()] = i
					}
					for it := gr.d.Edges(); it.Next(); {
						e := it.Edge()
						stitch.Check(pos[e.From().ID()] < pos[e.To().ID()], "topological order puts edges forward")
					}
				}
				_, _ = topo.SortStabilized(gr.d, nil)
			}
		})
	}, "g")
	stitch.Endpoint("traversals", func(gr *Graph, s *stitch.Source) {
		guard(func() {
			g := gr.g()
			u := pickNode(g, s)
			var b, d []int64
			bf := traverse.BreadthFirst{}
			bf.Walk(g, u, func(n graph.Node, depth int) bool { b = append(b, n.ID()); return false })
			df := traverse.DepthFirst{}
			df.Walk(g, u, func(n graph.Node) bool { d = append(d, n.ID()); return false })
			sort.Slice(b, func(i, j int) bool { return b[i] < b[j] })
			sort.Slice(d, func(i, j int) bool { return d[i] < d[j] })
			stitch.Check(len(b) == len(d), "BFS and DFS reach the same nodes")
			if gr.u != nil {
				var comps [][]graph.Node
				w := traverse.BreadthFirst{}
				w.WalkAll(gr.u, func() { comps = append(comps, nil) }, nil, func(graph.Node) {})
				stitch.Check(len(comps) == len(topo.ConnectedComponents(gr.u)), "WalkAll visits each component once")
			}
		})
	}, "g", "s:max=8")
	stitch.Endpoint("centralities", func(gr *Graph) {
		guard(func() {
			g := gr.g()
			b := network.Betweenness(g)
			paths := path.DijkstraAllPaths(g)
			bw := network.BetweennessWeighted(g, paths)
			_ = network.EdgeBetweenness(g)
			_ = network.EdgeBetweennessWeighted(g, paths)
			_ = network.Closeness(g, paths)
			_ = network.Farness(g, paths)
			_ = network.Harmonic(g, paths)
			_ = network.Residual(g, paths)
			for _, v := range b {
				stitch.Check(v >= 0 && !math.IsNaN(v), "betweenness >= 0")
			}
			_ = bw
		})
	}, "g")
	stitch.Endpoint("pagerank_agree", func(gr *Graph) {
		stitch.Require(gr.d != nil, "directed")
		guard(func() {
			a := network.PageRank(gr.d, 0.85, 1e-10)
			b := network.PageRankSparse(gr.d, 0.85, 1e-10)
			for k, v := range a {
				stitch.Check(math.Abs(v-b[k]) < 1e-6, "PageRank and PageRankSparse agree")
			}
			_ = network.HITS(gr.d, 1e-8)
		})
	}, "g")
	stitch.Endpoint("communities", func(gr *Graph, seed int) {
		guard(func() {
			g := gr.g()
			r := community.Modularize(g, 1, rand.NewSource(uint64(seed)))
			comms := r.Communities()
			n := 0
			for _, c := range comms {
				n += len(c)
			}
			stitch.Check(n == len(ids(g)), "Modularize partitions the nodes")
			q := community.Q(g, comms, 1)
			stitch.Check(math.IsNaN(q) || (q >= -1-1e-9 && q <= 1+1e-9), "modularity in [-1, 1]")
			if gr.u != nil {
				_ = community.KCliqueCommunities(3, gr.u)
			}
		})
	}, "g", "seed:range=0..100")
	stitch.Endpoint("dominators_agree", func(gr *Graph, s *stitch.Source) {
		stitch.Require(gr.d != nil, "directed")
		guard(func() {
			r := pickNode(gr.d, s)
			a := flow.Dominators(r, gr.d)
			b := flow.DominatorsSLT(r, gr.d)
			for _, v := range ids(gr.d) {
				da, db := a.DominatorOf(v), b.DominatorOf(v)
				stitch.Check((da == nil) == (db == nil) && (da == nil || da.ID() == db.ID()), "Dominators and DominatorsSLT agree")
			}
		})
	}, "g", "s:max=8")
	colorings := map[string]func(g *simple.WeightedUndirectedGraph, seed uint64) (int, map[int64]int, error){
		"dsatur": func(g *simple.WeightedUndirectedGraph, _ uint64) (int, map[int64]int, error) { return coloring.Dsatur(g, nil) },
		"welsh_powell": func(g *simple.WeightedUndirectedGraph, _ uint64) (int, map[int64]int, error) { return coloring.WelshPowell(g, nil) },
		"randomized": func(g *simple.WeightedUndirectedGraph, seed uint64) (int, map[int64]int, error) {
			return coloring.Randomized(g, nil, rand.NewSource(seed))
		},
		"rlf": func(g *simple.WeightedUndirectedGraph, _ uint64) (int, map[int64]int, error) { k, c := coloring.RecursiveLargestFirst(g); return k, c, nil },
		"san_segundo": func(g *simple.WeightedUndirectedGraph, _ uint64) (int, map[int64]int, error) { return coloring.SanSegundo(g, nil) },
		"dsatur_exact": func(g *simple.WeightedUndirectedGraph, _ uint64) (int, map[int64]int, error) {
			stitch.Require(len(ids(g)) <= 10, "too large for exact coloring")
			return coloring.DsaturExact(nil, g)
		},
	}
	for _, name := range sortedKeys(colorings) {
		fn := colorings[name]
		stitch.Endpoint("coloring_"+name, func(gr *Graph, seed int) {
			stitch.Require(gr.u != nil, "undirected")
			guard(func() {
				k, c, err := fn(gr.u, uint64(seed))
				if err != nil {
					stitch.Bail(err.Error())
				}
				for it := gr.u.Edges(); it.Next(); {
					e := it.Edge()
					stitch.Check(c[e.From().ID()] != c[e.To().ID()], "a valid coloring: adjacent nodes differ")
				}
				stitch.Check(len(c) == len(ids(gr.u)), "every node is colored")
				used := map[int]bool{}
				for _, x := range c {
					used[x] = true
				}
				stitch.Check(len(used) == k, "k is the number of colors used")
			})
		}, "g", "seed:range=0..100")
	}
	products := map[string]func(dst graph.Builder, a, b graph.Graph){
		"cartesian": product.Cartesian, "tensor": product.Tensor, "lexicographical": product.Lexicographical,
		"strong": product.Strong, "conormal": product.CoNormal, "modular": product.Modular,
	}
	for _, name := range sortedKeys(products) {
		fn := products[name]
		stitch.Endpoint("product_"+name, func(a, b *Graph) {
			stitch.Require(a.u != nil && b.u != nil && len(ids(a.u))*len(ids(b.u)) <= 64, "small undirected graphs")
			guard(func() {
				dst := simple.NewUndirectedGraph()
				fn(dst, a.u, b.u)
				stitch.Check(len(ids(dst)) == len(ids(a.u))*len(ids(b.u)), "a product has |A| x |B| nodes")
			})
		}, "a", "b")
	}
	stitch.Endpoint("laplacians", func(gr *Graph) {
		stitch.Require(gr.u != nil, "undirected")
		guard(func() {
			l := spectral.NewLaplacian(gr.u)
			n := spectral.NewSymNormLaplacian(gr.u)
			r, _ := l.Dims()
			for i := 0; i < r; i++ {
				row := 0.0
				for j := 0; j < r; j++ {
					row += l.At(i, j)
				}
				stitch.Check(math.Abs(row) < 1e-9, "Laplacian rows sum to 0")
			}
			_ = n
		})
	}, "g")
	stitch.Endpoint("dot_roundtrip", func(gr *Graph) {
		guard(func() {
			b, err := dot.Marshal(gr.g(), "g", "", "  ")
			stitch.Require(err == nil, "marshal")
			var back graph.Graph
			if gr.d != nil {
				d := simple.NewDirectedGraph()
				stitch.Check(dot.Unmarshal(b, d) == nil, "Marshal output unmarshals")
				back = d
			} else {
				u := simple.NewUndirectedGraph()
				stitch.Check(dot.Unmarshal(b, u) == nil, "Marshal output unmarshals")
				back = u
			}
			stitch.Check(len(ids(back)) == len(ids(gr.g())), "DOT round trip keeps the nodes")
		})
	}, "g")
	stitch.Endpoint("dot_unmarshal", func(text []byte) {
		guard(func() {
			g := simple.NewDirectedGraph()
			_ = dot.Unmarshal(text, g)
			u := simple.NewUndirectedGraph()
			_ = dot.Unmarshal(text, u)
		})
	}, "text:max=1024,type=Dot")
	stitch.Decoder("Dot", func(s *stitch.Source) string {
		var b bytes.Buffer
		b.WriteString(stitch.Pick(s, "digraph", "graph", "strict digraph", "digraph G", "graph \"x y\""))
		b.WriteString(" {\n")
		for s.More() {
			switch s.Choice(6) {
			case 0:
				b.WriteString(stitch.Pick(s, "a", "b", "1", "\"q\"", "-2.5") + " " + stitch.Pick(s, "->", "--") + " " + stitch.Pick(s, "a", "b", "c", "{x y}", "subgraph s {z}") + ";\n")
			case 1:
				b.WriteString(stitch.Pick(s, "a", "b") + " [label=\"" + s.Str(6) + "\", weight=" + stitch.Pick(s, "1", "x", "-3", "1e9") + "];\n")
			case 2:
				b.WriteString(stitch.Pick(s, "node", "edge", "graph") + " [" + stitch.Pick(s, "a=b", "shape=box", "") + "];\n")
			case 3:
				b.WriteString("subgraph " + s.Str(3) + " { " + stitch.Pick(s, "a", "b -> c") + " }\n")
			case 4:
				b.WriteString(s.Token())
			default:
				b.WriteString(s.Str(8))
			}
		}
		b.WriteString("}\n")
		return b.String()
	})
	stitch.Endpoint("graph6_roundtrip", func(gr *Graph) {
		stitch.Require(gr.u != nil, "undirected")
		guard(func() {
			g6 := graph6.Encode(gr.u)
			stitch.Check(graph6.IsValid(g6), "Encode gives valid graph6")
			stitch.Check(len(ids(g6)) == len(ids(gr.u)), "graph6 keeps the node count")
			e1, e2 := edgeCount(g6), 0
			for it := gr.u.Edges(); it.Next(); {
				e2++
			}
			stitch.Check(e1 == e2, "graph6 keeps the edges")
		})
	}, "g")
	stitch.Endpoint("graph6_decode", func(text string) {
		guard(func() {
			g := graph6.Graph(text)
			if graph6.IsValid(g) {
				_ = ids(g)
				_ = edgeCount(g)
			}
		})
	}, "text:max=64")
	stitch.Endpoint("graph_mutate", func(gr *Graph, s *stitch.Source) {
		guard(func() {
			g := gr.g()
			for s.More() {
				ns := ids(g)
				if len(ns) == 0 {
					break
				}
				a, b := stitch.Pick(s, ns...), stitch.Pick(s, ns...)
				switch s.Choice(3) {
				case 0:
					if gr.d != nil {
						gr.d.RemoveNode(a)
					} else {
						gr.u.RemoveNode(a)
					}
				case 1:
					if a != b {
						if gr.d != nil {
							gr.d.RemoveEdge(a, b)
						} else {
							gr.u.RemoveEdge(a, b)
						}
					}
				default:
					if a != b {
						e := simple.WeightedEdge{F: simple.Node(a), T: simple.Node(b), W: float64(s.Range(0, 9))}
						if gr.d != nil {
							gr.d.SetWeightedEdge(e)
						} else {
							gr.u.SetWeightedEdge(e)
						}
					}
				}
			}
			for it := g.Nodes(); it.Next(); {
				u := it.Node()
				for jt := g.From(u.ID()); jt.Next(); {
					v := jt.Node()
					stitch.Check(g.Node(v.ID()) != nil, "edge endpoints are nodes")
					stitch.Check(g.HasEdgeBetween(u.ID(), v.ID()) && g.Edge(u.ID(), v.ID()) != nil, "HasEdgeBetween and Edge see every edge")
				}
			}
		})
	}, "g", "s:max=32")
}

func edgeCount(g graph.Graph) int {
	n := 0
	for it := g.Nodes(); it.Next(); {
		u := it.Node()
		for jt := g.From(u.ID()); jt.Next(); {
			if jt.Node().ID() > u.ID() {
				n++
			}
		}
	}
	return n
}

// yen calls path.YenKShortestPaths, whose signature gained a cost limit after v0.12
// (g, k, a, b) -> (g, k, cost, a, b); called by reflection so the harness builds at both.
func yen(g graph.Weighted, k int, a, b graph.Node) [][]graph.Node {
	f := reflect.ValueOf(path.YenKShortestPaths)
	var args []reflect.Value
	if f.Type().NumIn() == 5 {
		args = []reflect.Value{reflect.ValueOf(g), reflect.ValueOf(k), reflect.ValueOf(math.Inf(1)), reflect.ValueOf(a), reflect.ValueOf(b)}
	} else {
		args = []reflect.Value{reflect.ValueOf(g), reflect.ValueOf(k), reflect.ValueOf(a), reflect.ValueOf(b)}
	}
	for i := range args {
		args[i] = args[i].Convert(f.Type().In(i))
	}
	return f.Call(args)[0].Interface().([][]graph.Node)
}
