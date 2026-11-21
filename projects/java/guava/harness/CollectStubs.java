import com.google.common.collect.*;
import java.util.*;
import stitch.Endpoint;
import stitch.Max;
import stitch.OneOf;
import stitch.Source;
import stitch.Stitch;

/**
 * Guava collections, each checked against a JDK model after every operation: multisets,
 * multimaps, bimaps, tables, range sets / maps, min-max priority queues, immutable collections,
 * and the static utilities of Lists, Sets, Iterables, Iterators, Maps, Ordering, Range.
 */
public class CollectStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    static final String[] KEYS = {"a", "b", "c", "d", "e"};

    static String key(Source s) { return s.pick(KEYS); }

    // ---------------- Multiset ----------------
    public static class MS {
        final Multiset<String> g;
        final Map<String, Integer> model = new TreeMap<>();
        MS(Multiset<String> g) { this.g = g; }
        void check() {
            for (String k : KEYS) Stitch.check(g.count(k) == model.getOrDefault(k, 0), "count matches the model");
            Stitch.check(g.size() == model.values().stream().mapToInt(Integer::intValue).sum(), "size is the sum of counts");
            Stitch.check(g.elementSet().size() == model.size(), "elementSet size");
            int n = 0;
            for (Multiset.Entry<String> e : g.entrySet()) { n += e.getCount(); Stitch.check(e.getCount() > 0, "entries have positive counts"); }
            Stitch.check(n == g.size(), "entrySet covers the size");
            Stitch.check(Iterators.size(g.iterator()) == g.size(), "iterator yields size elements");
        }
        void set(String k, int c) { if (c == 0) model.remove(k); else model.put(k, c); }
    }

    @Endpoint
    public static MS multiset(@OneOf(ints = {0, 1, 2, 3}) int kind) {
        Multiset<String> m = kind == 0 ? HashMultiset.create() : kind == 1 ? TreeMultiset.create() : kind == 2 ? LinkedHashMultiset.create() : ConcurrentHashMultiset.create();
        return new MS(m);
    }
    @Endpoint public static void multiset_add(MS m, @stitch.Max(8) Source s) { String k = key(s); int n = ri(s, 0, 5); m.g.add(k, n); m.set(k, m.model.getOrDefault(k, 0) + n); m.check(); }
    @Endpoint public static void multiset_remove(MS m, @stitch.Max(8) Source s) { String k = key(s); int n = ri(s, 0, 5); int before = m.g.remove(k, n); Stitch.check(before == m.model.getOrDefault(k, 0), "remove returns the old count"); m.set(k, Math.max(0, before - n)); m.check(); }
    @Endpoint public static void multiset_set_count(MS m, @stitch.Max(8) Source s) { String k = key(s); int n = ri(s, 0, 5); m.g.setCount(k, n); m.set(k, n); m.check(); }
    @Endpoint public static void multiset_set_count_if(MS m, @stitch.Max(8) Source s) { String k = key(s); int old = ri(s, 0, 3), n = ri(s, 0, 3); boolean ok = m.g.setCount(k, old, n); Stitch.check(ok == (m.model.getOrDefault(k, 0) == old), "conditional setCount"); if (ok) m.set(k, n); m.check(); }
    @Endpoint public static void multiset_remove_all(MS m, @stitch.Max(8) Source s) { String k = key(s); m.g.removeAll(Collections.singleton(k)); m.model.remove(k); m.check(); }
    @Endpoint public static void multiset_element_set_remove(MS m, @stitch.Max(8) Source s) { String k = key(s); m.g.elementSet().remove(k); m.model.remove(k); m.check(); }
    @Endpoint public static void multiset_iterator_remove(MS m, @stitch.Max(8) Source s) {
        Iterator<String> it = m.g.iterator();
        int skip = ri(s, 0, 4);
        while (it.hasNext()) { String x = it.next(); if (skip-- == 0) { it.remove(); m.set(x, m.model.get(x) - 1); break; } }
        m.check();
    }
    @Endpoint public static void multiset_entry_iterator_remove(MS m, @stitch.Max(8) Source s) {
        Iterator<Multiset.Entry<String>> it = m.g.entrySet().iterator();
        int skip = ri(s, 0, 3);
        while (it.hasNext()) { Multiset.Entry<String> e = it.next(); if (skip-- == 0) { String k = e.getElement(); it.remove(); m.model.remove(k); break; } }
        m.check();
    }
    @Endpoint public static void multiset_views(MS m) {
        Multiset<String> copy = ImmutableMultiset.copyOf(m.g);
        Stitch.check(copy.equals(m.g) && copy.hashCode() == m.g.hashCode(), "ImmutableMultiset.copyOf equals the multiset");
        Multiset<String> hc = Multisets.copyHighestCountFirst(m.g);
        int prev = Integer.MAX_VALUE;
        for (Multiset.Entry<String> e : hc.entrySet()) { Stitch.check(e.getCount() <= prev, "copyHighestCountFirst orders by count"); prev = e.getCount(); }
        Stitch.check(Multisets.containsOccurrences(m.g, Multisets.difference(m.g, ImmutableMultiset.of("a"))), "containsOccurrences(difference)");
        Stitch.check(Multisets.union(m.g, m.g).equals(m.g), "union with itself");
        Stitch.check(Multisets.intersection(m.g, m.g).equals(m.g), "intersection with itself");
        Stitch.check(Multisets.sum(m.g, m.g).size() == 2 * m.g.size(), "sum doubles the size");
        if (m.g instanceof SortedMultiset) {
            SortedMultiset<String> sm = (SortedMultiset<String>) m.g;
            SortedMultiset<String> d = sm.descendingMultiset();
            Stitch.check(Iterables.elementsEqual(d, Lists.reverse(ImmutableList.copyOf(sm))), "descendingMultiset reverses");
            SortedMultiset<String> head = sm.headMultiset("c", BoundType.OPEN);
            Stitch.check(head.count("c") == 0 && head.count("a") == sm.count("a"), "headMultiset");
            if (!sm.isEmpty()) Stitch.check(sm.firstEntry().getElement().equals(Collections.min(sm.elementSet())), "firstEntry");
        }
    }

    // ---------------- Multimap ----------------
    public static class MM {
        final Multimap<String, Integer> g;
        final boolean set;  // set semantics (no duplicate key-value pairs)
        final List<Map.Entry<String, Integer>> model = new ArrayList<>();
        MM(Multimap<String, Integer> g, boolean set) { this.g = g; this.set = set; }
        void put(String k, int v) {
            Map.Entry<String, Integer> e = new AbstractMap.SimpleEntry<>(k, v);
            if (!set || !model.contains(e)) model.add(e);
        }
        void check() {
            Stitch.check(g.size() == model.size(), "size matches the model (" + g.size() + " vs " + model.size() + ")");
            for (String k : KEYS) {
                long n = model.stream().filter(e -> e.getKey().equals(k)).count();
                Stitch.check(g.get(k).size() == n, "get(k) size");
                Stitch.check(g.containsKey(k) == (n > 0), "containsKey");
                Stitch.check(g.keys().count(k) == n, "keys() count");
                Stitch.check(n == 0 || g.asMap().get(k).size() == n, "asMap");
            }
            Stitch.check(g.entries().size() == g.size() && g.values().size() == g.size(), "entries / values size");
        }
    }

    @Endpoint
    public static MM multimap(@OneOf(ints = {0, 1, 2, 3, 4}) int kind) {
        switch (kind) {
            case 0: return new MM(ArrayListMultimap.create(), false);
            case 1: return new MM(LinkedListMultimap.create(), false);
            case 2: return new MM(HashMultimap.create(), true);
            case 3: return new MM(LinkedHashMultimap.create(), true);
            default: return new MM(TreeMultimap.create(), true);
        }
    }
    @Endpoint public static void multimap_put(MM m, @stitch.Max(8) Source s) { String k = key(s); int v = ri(s, 0, 4); boolean ch = m.g.put(k, v); m.put(k, v); m.check(); }
    @Endpoint public static void multimap_put_all(MM m, @stitch.Max(16) Source s) { String k = key(s); List<Integer> vs = new ArrayList<>(); for (int i = ri(s, 0, 4); i > 0; i--) vs.add(ri(s, 0, 4)); m.g.putAll(k, vs); for (int v : vs) m.put(k, v); m.check(); }
    @Endpoint public static void multimap_remove(MM m, @stitch.Max(8) Source s) { String k = key(s); int v = ri(s, 0, 4); m.g.remove(k, v); m.model.remove(new AbstractMap.SimpleEntry<>(k, v)); m.check(); }
    @Endpoint public static void multimap_remove_all(MM m, @stitch.Max(8) Source s) { String k = key(s); Collection<Integer> old = m.g.removeAll(k); long n = m.model.stream().filter(e -> e.getKey().equals(k)).count(); Stitch.check(old.size() == n, "removeAll returns the values"); m.model.removeIf(e -> e.getKey().equals(k)); m.check(); }
    @Endpoint public static void multimap_replace_values(MM m, @stitch.Max(16) Source s) {
        String k = key(s);
        List<Integer> vs = new ArrayList<>();
        for (int i = ri(s, 0, 4); i > 0; i--) vs.add(ri(s, 0, 4));
        m.g.replaceValues(k, vs);
        m.model.removeIf(e -> e.getKey().equals(k));
        for (int v : vs) m.put(k, v);
        m.check();
    }
    @Endpoint public static void multimap_get_view_add(MM m, @stitch.Max(8) Source s) { String k = key(s); int v = ri(s, 0, 4); boolean ch = m.g.get(k).add(v); if (ch) m.put(k, v); m.check(); }
    @Endpoint public static void multimap_get_view_clear(MM m, @stitch.Max(8) Source s) { String k = key(s); m.g.get(k).clear(); m.model.removeIf(e -> e.getKey().equals(k)); m.check(); }
    @Endpoint public static void multimap_as_map_remove(MM m, @stitch.Max(8) Source s) { String k = key(s); m.g.asMap().remove(k); m.model.removeIf(e -> e.getKey().equals(k)); m.check(); }
    @Endpoint public static void multimap_entries_iterator_remove(MM m, @stitch.Max(8) Source s) {
        Iterator<Map.Entry<String, Integer>> it = m.g.entries().iterator();
        int skip = ri(s, 0, 5);
        while (it.hasNext()) { Map.Entry<String, Integer> e = it.next(); if (skip-- == 0) { Map.Entry<String, Integer> c = new AbstractMap.SimpleEntry<>(e.getKey(), e.getValue()); it.remove(); m.model.remove(c); break; } }
        m.check();
    }
    @Endpoint public static void multimap_utilities(MM m) {
        Multimap<Integer, String> inv = Multimaps.invertFrom(m.g, ArrayListMultimap.create());
        Stitch.check(inv.size() == m.g.size(), "invertFrom keeps the size");
        ImmutableMultimap<String, Integer> im = ImmutableMultimap.copyOf(m.g);
        Stitch.check(im.size() == m.g.size() && im.inverse().inverse().size() == im.size(), "ImmutableMultimap.copyOf / inverse");
        Stitch.check(Multimaps.filterKeys(m.g, k -> k.equals("a")).size() == m.g.get("a").size(), "filterKeys");
        Multimaps.index(m.g.values(), Object::toString);
        Stitch.check(ImmutableSetMultimap.copyOf(m.g).size() == new HashSet<>(m.g.entries()).size(), "ImmutableSetMultimap dedups");
    }

    // ---------------- BiMap ----------------
    public static class BM {
        final BiMap<String, Integer> g = HashBiMap.create();
        final Map<String, Integer> model = new HashMap<>();
        void check() {
            Stitch.check(g.equals(model), "bimap equals the model");
            Stitch.check(g.inverse().size() == g.size(), "inverse size");
            for (Map.Entry<String, Integer> e : model.entrySet()) Stitch.check(e.getKey().equals(g.inverse().get(e.getValue())), "inverse maps back");
            Stitch.check(g.inverse().inverse() == g, "inverse of inverse is the bimap");
        }
    }
    @Endpoint public static BM bimap() { return new BM(); }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void bimap_put(BM m, @stitch.Max(8) Source s) {
        String k = key(s); int v = ri(s, 0, 4);
        if (m.model.containsValue(v) && !Integer.valueOf(v).equals(m.model.get(k))) { try { m.g.put(k, v); Stitch.check(false, "put of a bound value throws"); } catch (IllegalArgumentException e) { m.check(); return; } }
        m.g.put(k, v); m.model.put(k, v); m.check();
    }
    @Endpoint public static void bimap_force_put(BM m, @stitch.Max(8) Source s) { String k = key(s); int v = ri(s, 0, 4); m.g.forcePut(k, v); m.model.values().remove(v); m.model.put(k, v); m.check(); }
    @Endpoint public static void bimap_inverse_put(BM m, @stitch.Max(8) Source s) { String k = key(s); int v = ri(s, 0, 4); m.g.inverse().forcePut(v, k); m.model.values().remove(v); m.model.remove(k); m.model.put(k, v); m.check(); }
    @Endpoint public static void bimap_remove(BM m, @stitch.Max(8) Source s) { String k = key(s); m.g.remove(k); m.model.remove(k); m.check(); }
    @Endpoint public static void bimap_inverse_remove(BM m, @stitch.Max(8) Source s) { int v = ri(s, 0, 4); m.g.inverse().remove(v); m.model.values().remove(v); m.check(); }
    @Endpoint public static void bimap_values_iterator_remove(BM m, @stitch.Max(8) Source s) {
        Iterator<Integer> it = m.g.values().iterator();
        int skip = ri(s, 0, 4);
        while (it.hasNext()) { Integer v = it.next(); if (skip-- == 0) { it.remove(); m.model.values().remove(v); break; } }
        m.check();
    }

    // ---------------- Table ----------------
    public static class TB {
        final Table<String, Integer, String> g;
        final Map<List<Object>, String> model = new HashMap<>();
        TB(Table<String, Integer, String> g) { this.g = g; }
        void check() {
            Stitch.check(g.size() == model.size(), "size");
            for (Map.Entry<List<Object>, String> e : model.entrySet()) Stitch.check(e.getValue().equals(g.get(e.getKey().get(0), e.getKey().get(1))), "get");
            Stitch.check(g.cellSet().size() == g.size(), "cellSet size");
            int n = 0;
            for (Map<Integer, String> row : g.rowMap().values()) n += row.size();
            Stitch.check(n == g.size(), "rowMap covers the cells");
            n = 0;
            for (Map<String, String> col : g.columnMap().values()) n += col.size();
            Stitch.check(n == g.size(), "columnMap covers the cells");
        }
    }
    @Endpoint public static TB table(boolean tree) { return new TB(tree ? TreeBasedTable.create() : HashBasedTable.create()); }
    @Endpoint public static void table_put(TB t, @stitch.Max(8) Source s) { String r = key(s); int c = ri(s, 0, 3); String v = key(s); t.g.put(r, c, v); t.model.put(Arrays.asList(r, c), v); t.check(); }
    @Endpoint public static void table_remove(TB t, @stitch.Max(8) Source s) { String r = key(s); int c = ri(s, 0, 3); t.g.remove(r, c); t.model.remove(Arrays.asList(r, c)); t.check(); }
    @Endpoint public static void table_row_view(TB t, @stitch.Max(8) Source s) { String r = key(s); int c = ri(s, 0, 3); String v = key(s); t.g.row(r).put(c, v); t.model.put(Arrays.asList(r, c), v); t.check(); }
    @Endpoint public static void table_column_remove(TB t, @stitch.Max(8) Source s) { int c = ri(s, 0, 3); t.g.column(c).clear(); t.model.keySet().removeIf(k -> k.get(1).equals(c)); t.check(); }
    @Endpoint public static void table_transpose_copy(TB t) {
        Table<Integer, String, String> tr = Tables.transpose(t.g);
        Stitch.check(tr.size() == t.g.size(), "transpose keeps the cells");
        Stitch.check(ImmutableTable.copyOf(t.g).equals(t.g), "ImmutableTable.copyOf equals");
        if (!t.g.isEmpty()) {
            ArrayTable<String, Integer, String> at = ArrayTable.create(t.g);
            Stitch.check(at.size() >= t.g.size(), "ArrayTable.create holds every cell");
        }
    }

    // ---------------- RangeSet / RangeMap (integers, checked by enumeration) ----------------
    static Range<Integer> range(Source s) {
        int a = ri(s, -8, 8), b = ri(s, -8, 8);
        int lo = Math.min(a, b), hi = Math.max(a, b);
        switch (s.choice(7)) {
            case 0: return Range.closed(lo, hi);
            case 1: return lo < hi ? Range.open(lo, hi) : Range.closed(lo, hi);
            case 2: return Range.closedOpen(lo, hi);
            case 3: return Range.openClosed(lo, hi);
            case 4: return Range.atLeast(lo);
            case 5: return Range.lessThan(hi);
            default: return Range.singleton(lo);
        }
    }
    public static class RS {
        final RangeSet<Integer> g = TreeRangeSet.create();
        final Set<Integer> model = new TreeSet<>();  // the integers in [-12, 12] it contains
        static final int LO = -12, HI = 12;
        void check() {
            for (int i = LO; i <= HI; i++) Stitch.check(g.contains(i) == model.contains(i), "contains(" + i + ") matches the model");
            Range<Integer> prev = null;
            for (Range<Integer> r : g.asRanges()) {
                Stitch.check(!r.isEmpty(), "asRanges has no empty ranges");
                if (prev != null) Stitch.check(!prev.isConnected(r) || prev.intersection(r).isEmpty() && !(prev.hasUpperBound() && r.hasLowerBound() && prev.upperEndpoint().equals(r.lowerEndpoint()) && (prev.upperBoundType() == BoundType.CLOSED || r.lowerBoundType() == BoundType.CLOSED)), "asRanges are disconnected");
                prev = r;
            }
            RangeSet<Integer> c = g.complement();
            for (int i = LO; i <= HI; i++) Stitch.check(c.contains(i) == !model.contains(i), "complement");
        }
        void apply(Range<Integer> r, boolean add) { for (int i = LO; i <= HI; i++) if (r.contains(i)) { if (add) model.add(i); else model.remove(i); } }
    }
    @Endpoint public static RS range_set() { return new RS(); }
    @Endpoint public static void range_set_add(RS m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); m.g.add(r); m.apply(r, true); m.check(); }
    @Endpoint public static void range_set_remove(RS m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); m.g.remove(r); m.apply(r, false); m.check(); }
    @Endpoint public static void range_set_complement_add(RS m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); m.g.complement().add(r); m.apply(r, false); m.check(); }
    @Endpoint public static void range_set_sub_view(RS m, @stitch.Max(16) Source s) {
        Range<Integer> r = range(s);
        RangeSet<Integer> sub = m.g.subRangeSet(r);
        for (int i = RS.LO; i <= RS.HI; i++) Stitch.check(sub.contains(i) == (m.model.contains(i) && r.contains(i)), "subRangeSet");
        Range<Integer> q = range(s);
        sub.remove(q);
        for (int i = RS.LO; i <= RS.HI; i++) if (q.contains(i) && r.contains(i)) m.model.remove(i);
        m.check();
    }
    @Endpoint public static void range_set_queries(RS m, @stitch.Max(16) Source s) {
        Range<Integer> r = range(s);
        boolean enc = m.g.encloses(r);
        boolean all = true, any = false;
        for (int i = RS.LO; i <= RS.HI; i++) if (r.contains(i)) { all &= m.model.contains(i); any |= m.model.contains(i); }
        if (enc) Stitch.check(all, "encloses implies every integer of the range is in the set");
        if (m.g.intersects(r)) Stitch.check(true, "");
        if (any) Stitch.check(m.g.intersects(r), "a shared integer means intersects");
        Range<Integer> rc = m.g.rangeContaining(ri(s, RS.LO, RS.HI));
        if (!m.g.isEmpty()) m.g.span();
        Stitch.check(ImmutableRangeSet.copyOf(m.g).equals(m.g), "ImmutableRangeSet.copyOf");
        Stitch.check(TreeRangeSet.create(m.g.asDescendingSetOfRanges()).equals(m.g), "rebuilding from descending ranges");
    }
    public static class RM {
        final RangeMap<Integer, String> g = TreeRangeMap.create();
        final Map<Integer, String> model = new HashMap<>();
        void check() {
            for (int i = RS.LO; i <= RS.HI; i++) Stitch.check(Objects.equals(g.get(i), model.get(i)), "get(" + i + ") matches the model");
        }
    }
    @Endpoint public static RM range_map() { return new RM(); }
    @Endpoint public static void range_map_put(RM m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); String v = key(s); m.g.put(r, v); for (int i = RS.LO; i <= RS.HI; i++) if (r.contains(i)) m.model.put(i, v); m.check(); }
    @Endpoint public static void range_map_put_coalescing(RM m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); String v = key(s); m.g.putCoalescing(r, v); for (int i = RS.LO; i <= RS.HI; i++) if (r.contains(i)) m.model.put(i, v); m.check(); }
    @Endpoint public static void range_map_remove(RM m, @stitch.Max(16) Source s) { Range<Integer> r = range(s); m.g.remove(r); for (int i = RS.LO; i <= RS.HI; i++) if (r.contains(i)) m.model.remove(i); m.check(); }
    @Endpoint public static void range_map_sub_view(RM m, @stitch.Max(16) Source s) {
        Range<Integer> r = range(s);
        RangeMap<Integer, String> sub = m.g.subRangeMap(r);
        for (int i = RS.LO; i <= RS.HI; i++) Stitch.check(Objects.equals(sub.get(i), r.contains(i) ? m.model.get(i) : null), "subRangeMap");
        Range<Integer> q = range(s);
        sub.remove(q);
        for (int i = RS.LO; i <= RS.HI; i++) if (q.contains(i) && r.contains(i)) m.model.remove(i);
        m.check();
        ImmutableRangeMap.copyOf(m.g);
        m.g.asMapOfRanges();
        m.g.asDescendingMapOfRanges();
    }

    // ---------------- MinMaxPriorityQueue ----------------
    public static class PQ {
        final MinMaxPriorityQueue<Integer> g;
        final List<Integer> model = new ArrayList<>();
        final int max;
        PQ(int max) { this.max = max; this.g = max > 0 ? MinMaxPriorityQueue.maximumSize(max).create() : MinMaxPriorityQueue.create(); }
        void check() {
            List<Integer> m = new ArrayList<>(model);
            Collections.sort(m);
            Stitch.check(g.size() == m.size(), "size");
            if (!m.isEmpty()) Stitch.check(g.peekFirst().equals(m.get(0)) && g.peekLast().equals(m.get(m.size() - 1)), "peekFirst / peekLast are the min and max");
            List<Integer> all = new ArrayList<>(g);
            Collections.sort(all);
            Stitch.check(all.equals(m), "the queue holds the model's elements");
        }
    }
    @Endpoint public static PQ min_max_queue(@stitch.Range(lo = 0, hi = 5) int max) { return new PQ(max); }
    @Endpoint public static void mmq_offer(PQ q, @stitch.Max(8) Source s) {
        int v = ri(s, -10, 10);
        q.g.offer(v);
        q.model.add(v);
        if (q.max > 0 && q.model.size() > q.max) { q.model.remove(Collections.max(q.model)); }
        q.check();
    }
    @Endpoint public static void mmq_poll_first(PQ q) { Integer x = q.g.pollFirst(); if (!q.model.isEmpty()) { Integer m = Collections.min(q.model); Stitch.check(m.equals(x), "pollFirst is the min"); q.model.remove(m); } q.check(); }
    @Endpoint public static void mmq_poll_last(PQ q) { Integer x = q.g.pollLast(); if (!q.model.isEmpty()) { Integer m = Collections.max(q.model); Stitch.check(m.equals(x), "pollLast is the max"); q.model.remove(m); } q.check(); }
    @Endpoint public static void mmq_remove_object(PQ q, @stitch.Max(8) Source s) { Integer v = ri(s, -10, 10); boolean a = q.g.remove(v), b = q.model.remove(v); Stitch.check(a == b, "remove(Object)"); q.check(); }
    @Endpoint public static void mmq_iterator_remove(PQ q, @stitch.Max(8) Source s) {
        Iterator<Integer> it = q.g.iterator();
        int skip = ri(s, 0, 5);
        while (it.hasNext()) { Integer v = it.next(); if (skip-- == 0) { it.remove(); q.model.remove(v); break; } }
        q.check();
    }
    @Endpoint public static void mmq_remove_last_then_iterate(PQ q) {
        int n = 0;
        while (!q.g.isEmpty() && n++ < 3) { q.g.removeLast(); q.model.remove(Collections.max(q.model)); }
        q.check();
    }

    // ---------------- immutable collections and static utilities ----------------
    static List<Integer> ints(Source s) { List<Integer> l = new ArrayList<>(); for (int i = ri(s, 0, 8); i > 0; i--) l.add(ri(s, -5, 5)); return l; }

    @Endpoint public static void immutable_list(@stitch.Max(16) Source s) {
        List<Integer> l = ints(s);
        ImmutableList<Integer> il = ImmutableList.copyOf(l);
        Stitch.check(il.equals(l) && il.hashCode() == l.hashCode(), "ImmutableList equals the list");
        Stitch.check(il.reverse().reverse().equals(il), "reverse twice");
        if (!l.isEmpty()) Stitch.check(il.subList(0, l.size() / 2).equals(l.subList(0, l.size() / 2)), "subList");
        Stitch.check(ImmutableList.sortedCopyOf(l).equals(l.stream().sorted().collect(java.util.stream.Collectors.toList())), "sortedCopyOf");
        ImmutableList.Builder<Integer> b = ImmutableList.builderWithExpectedSize(ri(s, 0, 3));
        b.addAll(l);
        Stitch.check(b.build().equals(il), "builder");
        Stitch.check(il.indexOf(3) == l.indexOf(3) && il.lastIndexOf(3) == l.lastIndexOf(3), "indexOf");
    }
    @Endpoint public static void immutable_sets(@stitch.Max(16) Source s) {
        List<Integer> l = ints(s);
        ImmutableSet<Integer> is = ImmutableSet.copyOf(l);
        Stitch.check(is.equals(new HashSet<>(l)), "ImmutableSet equals the set");
        Stitch.check(ImmutableList.copyOf(is).equals(new ArrayList<>(new LinkedHashSet<>(l))), "ImmutableSet keeps insertion order");
        ImmutableSortedSet<Integer> ss = ImmutableSortedSet.copyOf(l);
        Stitch.check(ImmutableList.copyOf(ss).equals(new ArrayList<>(new TreeSet<>(l))), "ImmutableSortedSet is sorted");
        if (!ss.isEmpty()) {
            int k = ri(s, -6, 6);
            TreeSet<Integer> ts = new TreeSet<>(l);
            Stitch.check(Objects.equals(ss.ceiling(k), ts.ceiling(k)) && Objects.equals(ss.floor(k), ts.floor(k)) && Objects.equals(ss.higher(k), ts.higher(k)) && Objects.equals(ss.lower(k), ts.lower(k)), "navigation");
            Stitch.check(ImmutableList.copyOf(ss.headSet(k, s.coin())).size() <= ss.size(), "headSet");
            Stitch.check(ss.descendingSet().descendingSet().equals(ss), "descendingSet twice");
            Stitch.check(ImmutableList.copyOf(ss.tailSet(k)).equals(new ArrayList<>(ts.tailSet(k))), "tailSet");
        }
    }
    @Endpoint public static void immutable_maps(@stitch.Max(32) Source s) {
        Map<String, Integer> m = new LinkedHashMap<>();
        for (int i = ri(s, 0, 6); i > 0; i--) m.put(key(s), ri(s, 0, 9));
        ImmutableMap<String, Integer> im = ImmutableMap.copyOf(m);
        Stitch.check(im.equals(m) && ImmutableList.copyOf(im.keySet()).equals(new ArrayList<>(m.keySet())), "ImmutableMap keeps entries and order");
        ImmutableSortedMap<String, Integer> sm = ImmutableSortedMap.copyOf(m);
        Stitch.check(sm.equals(new TreeMap<>(m)), "ImmutableSortedMap");
        if (!sm.isEmpty()) Stitch.check(sm.headMap("c").equals(new TreeMap<>(m).headMap("c")) && sm.descendingMap().descendingMap().equals(sm), "sorted map views");
        ImmutableBiMap<Integer, String> inv = ImmutableBiMap.copyOf(Maps.filterValues(m, v -> true).entrySet().stream().collect(java.util.stream.Collectors.toMap(Map.Entry::getValue, Map.Entry::getKey, (a, b) -> a))).inverse().inverse();
        Stitch.check(inv.inverse().size() == inv.size(), "ImmutableBiMap inverse");
        Stitch.check(im.asMultimap().size() == im.size(), "asMultimap");
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void lists_utilities(@stitch.Max(32) Source s) {
        List<Integer> l = ints(s);
        int size = ri(s, 1, 4);
        List<List<Integer>> parts = Lists.partition(l, size);
        Stitch.check(parts.stream().mapToInt(List::size).sum() == l.size(), "partition covers the list");
        Stitch.check(parts.size() == (l.size() + size - 1) / size, "partition count");
        Stitch.check(Lists.reverse(Lists.reverse(l)).equals(l), "reverse view twice");
        List<List<Integer>> cp = Lists.cartesianProduct(l.subList(0, Math.min(2, l.size())), l.subList(0, Math.min(3, l.size())));
        Stitch.check(cp.size() == Math.min(2, l.size()) * Math.min(3, l.size()), "cartesianProduct size");
        Stitch.check(Lists.charactersOf("abc").size() == 3, "charactersOf");
        List<Integer> t = Lists.transform(l, x -> x * 2);
        Stitch.check(t.size() == l.size(), "transform");
        Stitch.check(Lists.newArrayList(Iterables.concat(l, l)).size() == 2 * l.size(), "concat");
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void sets_utilities(@stitch.Max(32) Source s) {
        Set<Integer> a = new HashSet<>(ints(s)), b = new HashSet<>(ints(s));
        Set<Integer> u = new HashSet<>(a); u.addAll(b);
        Set<Integer> in = new HashSet<>(a); in.retainAll(b);
        Set<Integer> df = new HashSet<>(a); df.removeAll(b);
        Stitch.check(Sets.union(a, b).equals(u) && Sets.union(a, b).size() == u.size(), "union");
        Stitch.check(Sets.intersection(a, b).equals(in) && Sets.intersection(a, b).size() == in.size(), "intersection");
        Stitch.check(Sets.difference(a, b).equals(df) && Sets.difference(a, b).size() == df.size(), "difference");
        Stitch.check(Sets.symmetricDifference(a, b).size() == u.size() - in.size(), "symmetricDifference");
        if (a.size() <= 8) {
            Set<Set<Integer>> ps = Sets.powerSet(a);
            Stitch.check(ps.size() == 1 << a.size(), "powerSet size");
            Stitch.check(ps.contains(a) && ps.contains(Collections.emptySet()), "powerSet contains the set and the empty set");
            int k = ri(s, 0, a.size());
            Stitch.check(Sets.combinations(ImmutableSet.copyOf(a), k).size() == com.google.common.math.IntMath.binomial(a.size(), k), "combinations size");
            for (Set<Integer> c : Sets.combinations(ImmutableSet.copyOf(a), k)) Stitch.check(c.size() == k && a.containsAll(c), "a combination is a k-subset");
        }
        Stitch.check(Sets.cartesianProduct(ImmutableSet.copyOf(a), ImmutableSet.copyOf(b)).size() == a.size() * b.size(), "cartesianProduct size");
        NavigableSet<Integer> ts = new TreeSet<>(a);
        Stitch.check(Sets.subSet(ts, com.google.common.collect.Range.closed(-2, 2)).equals(ts.subSet(-2, true, 2, true)), "subSet(range)");
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void permutations(@stitch.Max(16) Source s) {
        List<Integer> l = ints(s).subList(0, 0);
        List<Integer> src = new ArrayList<>();
        for (int i = ri(s, 0, 5); i > 0; i--) src.add(ri(s, 0, 3));
        Collection<List<Integer>> op = Collections2.orderedPermutations(src);
        Set<List<Integer>> distinct = new HashSet<>(op);
        Stitch.check(distinct.size() == op.size(), "orderedPermutations has no duplicates");
        Stitch.check(op.size() == Iterables.size(op), "orderedPermutations size == iteration count");
        Collection<List<Integer>> p = Collections2.permutations(src);
        Stitch.check(p.size() == com.google.common.math.IntMath.factorial(src.size()), "permutations size");
        Stitch.check(new HashSet<>(p).equals(distinct), "permutations and orderedPermutations give the same set");
        void_(l);
    }
    static void void_(Object o) {}
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void iterables_utilities(@stitch.Max(32) Source s) {
        List<Integer> l = ints(s);
        int n = ri(s, 0, 10);
        Stitch.check(ImmutableList.copyOf(Iterables.limit(l, n)).equals(l.subList(0, Math.min(n, l.size()))), "limit");
        Stitch.check(ImmutableList.copyOf(Iterables.skip(l, n)).equals(l.subList(Math.min(n, l.size()), l.size())), "skip");
        int size = ri(s, 1, 4);
        Stitch.check(Iterables.size(Iterables.paddedPartition(l, size)) == (l.size() + size - 1) / size, "paddedPartition count");
        Stitch.check(Iterables.frequency(l, 1) == Collections.frequency(l, 1), "frequency");
        Stitch.check(ImmutableList.copyOf(Iterables.limit(Iterables.cycle(l), 2 * l.size())).size() == 2 * l.size(), "cycle");
        List<Integer> a = new ArrayList<>(l), b = ints(s);
        Collections.sort(a);
        Collections.sort(b);
        List<Integer> merged = ImmutableList.copyOf(Iterables.mergeSorted(Arrays.<Iterable<Integer>>asList(a, b), Comparator.<Integer>naturalOrder()));
        List<Integer> ref = new ArrayList<>(a); ref.addAll(b); Collections.sort(ref);
        Stitch.check(merged.equals(ref), "mergeSorted");
        Stitch.check(Iterables.getLast(l, null) == (l.isEmpty() ? null : l.get(l.size() - 1)), "getLast");
        Stitch.check(Iterators.elementsEqual(Iterators.consumingIterator(new ArrayList<>(l).iterator()), l.iterator()), "consumingIterator");
        Stitch.check(Iterators.size(Iterators.filter(l.iterator(), x -> x > 0)) == (int) l.stream().filter(x -> x > 0).count(), "filter");
        PeekingIterator<Integer> pi = Iterators.peekingIterator(l.iterator());
        while (pi.hasNext()) { Integer p = pi.peek(); Stitch.check(p.equals(pi.next()), "peek == next"); }
    }
    @Endpoint public static void ordering_utilities(@stitch.Max(32) Source s) {
        List<Integer> l = ints(s);
        int k = ri(s, 0, 10);
        List<Integer> sorted = new ArrayList<>(l);
        Collections.sort(sorted);
        Stitch.check(Ordering.natural().leastOf(l, k).equals(sorted.subList(0, Math.min(k, l.size()))), "leastOf");
        Stitch.check(Ordering.natural().greatestOf(l, k).equals(Lists.reverse(sorted).subList(0, Math.min(k, l.size()))), "greatestOf");
        Stitch.check(Ordering.natural().isOrdered(sorted) && Comparators.isInOrder(sorted, Comparator.naturalOrder()), "isOrdered");
        Stitch.check(Ordering.natural().sortedCopy(l).equals(sorted), "sortedCopy");
        Stitch.check(Ordering.natural().reverse().reverse().sortedCopy(l).equals(sorted), "reverse twice");
        Stitch.check(l.stream().collect(Comparators.least(k, Comparator.<Integer>naturalOrder())).equals(sorted.subList(0, Math.min(k, l.size()))), "Comparators.least");
        Stitch.check(Ordering.natural().nullsFirst().compare(null, 1) < 0, "nullsFirst");
        Ordering.<Integer>natural().lexicographical().compare(l, sorted);
        if (!l.isEmpty()) Stitch.check(Ordering.natural().min(l).equals(sorted.get(0)) && Ordering.natural().max(l).equals(sorted.get(sorted.size() - 1)), "min / max");
        Stitch.check(Ordering.natural().binarySearch(sorted, 2) == Collections.binarySearch(sorted, 2), "binarySearch");
    }
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void range_operations(@stitch.Max(32) Source s) {
        Range<Integer> a = range(s), b = range(s);
        for (int i = -10; i <= 10; i++) {
            if (a.isConnected(b)) {
                Range<Integer> in = a.intersection(b);
                Stitch.check(in.contains(i) == (a.contains(i) && b.contains(i)), "intersection");
            }
            Range<Integer> sp = a.span(b);
            Stitch.check(!(a.contains(i) || b.contains(i)) || sp.contains(i), "span contains both");
        }
        if (a.encloses(b)) for (int i = -10; i <= 10; i++) Stitch.check(!b.contains(i) || a.contains(i), "encloses");
        Range<Integer> ca = a.canonical(DiscreteDomain.integers());
        for (int i = -10; i <= 10; i++) Stitch.check(ca.contains(i) == a.contains(i), "canonical keeps the integers");
        if (a.hasLowerBound() && a.hasUpperBound()) {
            ContiguousSet<Integer> cs = ContiguousSet.create(a, DiscreteDomain.integers());
            int n = 0;
            for (int i = a.lowerEndpoint() - 1; i <= a.upperEndpoint() + 1; i++) if (a.contains(i)) n++;
            Stitch.check(cs.size() == n, "ContiguousSet size");
            Stitch.check(ImmutableList.copyOf(cs.descendingSet()).size() == n, "descending ContiguousSet");
        }
        try {
            Range<Integer> g = a.gap(b);
            for (int i = -10; i <= 10; i++) Stitch.check(!(g.contains(i) && (a.contains(i) || b.contains(i))), "gap is between the ranges");
        } catch (IllegalArgumentException e) {
            Stitch.check(a.isConnected(b), "gap throws only for connected ranges");
        }
        Stitch.check(Range.<Integer>encloseAll(Arrays.asList(1, 5, -3)).equals(Range.closed(-3, 5)), "encloseAll");
    }
    @Endpoint public static void maps_utilities(@stitch.Max(32) Source s) {
        Map<String, Integer> a = new HashMap<>(), b = new HashMap<>();
        for (int i = ri(s, 0, 5); i > 0; i--) a.put(key(s), ri(s, 0, 3));
        for (int i = ri(s, 0, 5); i > 0; i--) b.put(key(s), ri(s, 0, 3));
        MapDifference<String, Integer> d = Maps.difference(a, b);
        Stitch.check(d.entriesOnlyOnLeft().size() + d.entriesInCommon().size() + d.entriesDiffering().size() == a.size(), "difference partitions the left map");
        Stitch.check(d.areEqual() == a.equals(b), "areEqual");
        Stitch.check(Maps.filterKeys(a, k -> k.compareTo("c") < 0).size() == a.keySet().stream().filter(k -> k.compareTo("c") < 0).count(), "filterKeys");
        Stitch.check(Maps.transformValues(a, v -> v + 1).size() == a.size(), "transformValues");
        NavigableMap<String, Integer> nm = new TreeMap<>(a);
        Stitch.check(Maps.subMap(nm, com.google.common.collect.Range.closed("b", "d")).equals(nm.subMap("b", true, "d", true)), "subMap(range)");
        Maps.uniqueIndex(a.keySet(), k -> k);
        Maps.asMap(a.keySet(), String::length).entrySet().size();
    }
    @Endpoint public static void evicting_queue(@stitch.Max(32) Source s) {
        int max = ri(s, 0, 4);
        EvictingQueue<Integer> q = EvictingQueue.create(max);
        Deque<Integer> model = new ArrayDeque<>();
        while (s.more()) {
            int v = ri(s, 0, 9);
            q.add(v);
            if (max > 0) { model.addLast(v); if (model.size() > max) model.removeFirst(); }
            Stitch.check(ImmutableList.copyOf(q).equals(new ArrayList<>(model)), "EvictingQueue keeps the last maxSize elements");
            Stitch.check(q.remainingCapacity() == max - q.size(), "remainingCapacity");
        }
    }
    @Endpoint public static void interners_and_streams(@stitch.Max(32) Source s) {
        Interner<String> in = Interners.newWeakInterner();
        String a = new String(key(s)), b = new String(a);
        Stitch.check(in.intern(a) == in.intern(b), "interned strings are the same object");
        List<Integer> l = ints(s);
        List<String> z = com.google.common.collect.Streams.zip(l.stream(), l.stream(), (x, y) -> x + ":" + y).collect(java.util.stream.Collectors.toList());
        Stitch.check(z.size() == l.size(), "Streams.zip");
        List<Long> idx = com.google.common.collect.Streams.mapWithIndex(l.stream(), (x, i) -> i).collect(java.util.stream.Collectors.toList());
        for (int i = 0; i < idx.size(); i++) Stitch.check(idx.get(i) == i, "mapWithIndex");
        Stitch.check(com.google.common.collect.Streams.findLast(l.stream()).orElse(null) == (l.isEmpty() ? null : l.get(l.size() - 1)), "findLast");
    }
}
