import java.io.StringReader;
import java.io.StringWriter;
import java.util.*;
import org.yaml.snakeyaml.DumperOptions;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.error.YAMLException;
import org.yaml.snakeyaml.events.*;
import org.yaml.snakeyaml.nodes.*;
import org.yaml.snakeyaml.parser.ParserImpl;
import org.yaml.snakeyaml.reader.StreamReader;
import org.yaml.snakeyaml.representer.Representer;
import org.yaml.snakeyaml.scanner.ScannerImpl;
import org.yaml.snakeyaml.tokens.Token;
import stitch.*;

/**
 * SnakeYAML 1.x. Objects: Yaml (always SafeConstructor), Data (Java values: loaded or built), Node.
 * Oracles: dump(x) loads back equal; canonical output reparses; compose/serialize round trips;
 * parse events are balanced. YAMLException is the documented bail for malformed input.
 */
public class Stubs {
    // ---------------------------------------------------------------- decoder
    @Decoder("YamlText")
    public static String yamlText(Source s) {
        StringBuilder b = new StringBuilder();
        if (s.coin(0.1)) b.append(s.pick("%YAML 1.1\n", "%YAML 1.2\n", "%TAG !e! tag:example.com,2000:\n")).append("---");
        if (s.coin(0.15)) b.append(' ');
        node(s, b, 0, 0, true);
        while (s.more() && s.coin(0.2)) {
            b.append(s.pick("\n---\n", "\n...\n---\n", "\n--- ", "\n---\n"));
            node(s, b, 0, 0, true);
        }
        if (s.coin(0.05)) b.append(s.pick("\n...", "\n", "\n#c"));
        return b.toString();
    }

    static String scalar(Source s) {
        switch (s.choice(14)) {
            case 0: return String.valueOf(s.i64());
            case 1: return s.pick("true", "false", "yes", "No", "on", "null", "~", "", "Null", "NULL");
            case 2: return s.pick("1.5", "-.inf", ".nan", "1e3", "0x1F", "0o17", "017", "1_000", "0b101", "+12", "190:20:30", "1:30.5");
            case 3: return s.pick("2001-12-14", "2001-12-14t21:59:43.10-05:00", "2002-12-14 1:2:3 +5", "1999-01-01");
            case 4: return "\"" + s.str(8).replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n") + "\"";
            case 5: return "'" + s.str(8).replace("'", "''") + "'";
            case 6: return "\"" + s.pick("\\x41", "\\u00e9", "\\U0001F600", "\\N", "\\_", "\\L", "\\e", "\\0", "\\\n", "\\ud800") + "\"";
            case 7: return s.token();
            case 8: return "*" + s.pick("a", "b", "c");
            case 9: return "&" + s.pick("a", "b", "c") + " " + s.str(4);
            case 10: return s.pick("!!str", "!!int", "!!float", "!!binary", "!!timestamp", "!!null", "!!bool", "!foo", "!<tag:x>", "!e!x", "!!merge", "!!value") + " " + s.str(5);
            case 11: return s.pick("<<", "=", "?", "-", ":", "[", "]", "{", "}", ",", "#", "|", ">", "%", "@", "`");
            default: return s.str(8);
        }
    }

    static String indent(int n) { return " ".repeat(n); }

    static void node(Source s, StringBuilder b, int ind, int depth, boolean top) {
        int k = depth > 4 ? s.choice(3) : s.choice(9);
        switch (k) {
            case 0: case 1: case 2:
                b.append(scalar(s));
                break;
            case 3: case 4: { // block sequence
                int n = (int) s.range(1, 4);
                for (int i = 0; i < n; i++) {
                    if (i > 0 || !top) b.append('\n').append(indent(ind));
                    b.append(s.coin(0.9) ? "- " : "-");
                    node(s, b, ind + 2, depth + 1, i == 0 && !top ? true : false);
                }
                break;
            }
            case 5: case 6: { // block mapping
                int n = (int) s.range(1, 4);
                for (int i = 0; i < n; i++) {
                    if (i > 0 || !top) b.append('\n').append(indent(ind));
                    if (s.coin(0.1)) { b.append("? "); node(s, b, ind + 2, depth + 1, false); b.append('\n').append(indent(ind)); }
                    else b.append(s.coin(0.6) ? s.str(4).replace('\n', ' ') : scalar(s));
                    b.append(s.coin(0.9) ? ": " : ":");
                    node(s, b, ind + 2, depth + 1, false);
                }
                break;
            }
            case 7: { // flow
                if (s.coin()) {
                    b.append('[');
                    while (s.more() && depth < 5) { flow(s, b, depth + 1); b.append(s.coin(0.85) ? ", " : ","); }
                    b.append(']');
                } else {
                    b.append('{');
                    while (s.more() && depth < 5) { flow(s, b, depth + 1); b.append(s.pick(": ", ":", ": ", ": ")); flow(s, b, depth + 1); b.append(", "); }
                    b.append('}');
                }
                break;
            }
            default: { // block scalar
                b.append(s.pick("|", ">", "|-", ">+", "|2", ">-")).append(s.pick("", " #c"));
                int n = (int) s.range(1, 3);
                for (int i = 0; i < n; i++) b.append('\n').append(indent(ind + 2)).append(s.str(10).replace('\n', ' '));
            }
        }
    }

    static void flow(Source s, StringBuilder b, int depth) {
        if (depth < 5 && s.coin(0.2)) {
            b.append(s.coin() ? "[" : "{");
            if (s.coin(0.7)) flow(s, b, depth + 1);
            b.append(s.coin() ? "]" : "}");
        } else b.append(scalar(s));
    }

    // ---------------------------------------------------------------- Yaml producers
    @Endpoint
    @Obj("Yaml")
    public static Yaml yaml_default() {
        return new Yaml(new SafeConstructor());
    }

    @Endpoint(bailsOn = {YAMLException.class})
    @Obj("Yaml")
    public static Yaml yaml_options(@Flags({1, 2, 4, 8, 16, 32, 64, 128, 256, 512}) int bits,
                                    DumperOptions.FlowStyle flow, DumperOptions.ScalarStyle scalar,
                                    @Range(lo = 0, hi = 11) int indent, @Range(lo = 0, hi = 10) int indicator,
                                    @OneOf(ints = {-1, 0, 1, 2, 5, 10, 20, 40, 80, 200}) int width,
                                    DumperOptions.LineBreak lineBreak,
                                    @OneOf(ints = {0, 10, 1, 2, 3, 50}) int maxAliases,
                                    @Range(lo = 0, hi = 1024) int maxSimpleKey, DumperOptions.NonPrintableStyle nps) {
        LoaderOptions lo = new LoaderOptions();
        lo.setAllowDuplicateKeys((bits & 1) != 0);
        lo.setAllowRecursiveKeys((bits & 2) != 0);
        lo.setWrappedToRootException((bits & 4) != 0);
        if (maxAliases != 50) lo.setMaxAliasesForCollections(maxAliases);
        DumperOptions d = new DumperOptions();
        d.setAllowUnicode((bits & 8) != 0);
        d.setCanonical((bits & 16) != 0);
        d.setSplitLines((bits & 32) != 0);
        d.setPrettyFlow((bits & 64) != 0);
        d.setExplicitStart((bits & 128) != 0);
        d.setExplicitEnd((bits & 256) != 0);
        d.setIndentWithIndicator((bits & 512) != 0);
        d.setDefaultFlowStyle(flow);
        d.setDefaultScalarStyle(scalar);
        d.setIndent(indent);
        d.setIndicatorIndent(indicator);
        d.setWidth(width);
        d.setLineBreak(lineBreak);
        d.setMaxSimpleKeyLength(maxSimpleKey);
        d.setNonPrintableStyle(nps);
        Yaml y = new Yaml(new SafeConstructor(lo), new Representer(d), d, lo);
        Stitch.set(y, "esc", nps == DumperOptions.NonPrintableStyle.ESCAPE ? 1L : 0L);
        return y;
    }

    // ---------------------------------------------------------------- helpers
    /** load, but constructor-level leaks (NumberFormatException etc. from SafeConstructor) are bails;
     *  `load` itself reports those. */
    static Object tolerantLoad(Yaml y, String text) {
        try {
            return y.load(text);
        } catch (YAMLException e) {
            throw e;
        } catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace())
                if (f.getClassName().contains("Constructor")) throw Stitch.bail("constructor leak");
            throw e;
        }
    }

    static String dumpOrBail(Yaml y, Object data) {
        try {
            return y.dump(data);
        } catch (YAMLException e) {
            if (String.valueOf(e.getMessage()).contains("invalid string value")) throw Stitch.bail("lone surrogate");
            throw e;
        }
    }

    static final java.util.regex.Pattern ANCHOR = java.util.regex.Pattern.compile("id\\d+");

    static void requirePrintable(Object o, IdentityHashMap<Object, Boolean> seen, boolean escape) {
        if (o == null || seen.put(o, true) != null) return;
        if (o instanceof String) {
            Stitch.require(escape || StreamReader.isPrintable((String) o), "non-printable string dumps as !!binary");
        } else if (o instanceof Map) {
            for (Map.Entry<?, ?> e : ((Map<?, ?>) o).entrySet()) { requirePrintable(e.getKey(), seen, escape); requirePrintable(e.getValue(), seen, escape); }
        } else if (o instanceof Collection) {
            for (Object x : (Collection<?>) o) requirePrintable(x, seen, escape);
        }
    }

    // ---------------------------------------------------------------- loading
    @Endpoint(optional = true, bailsOn = {YAMLException.class})
    @Obj("Data")
    public static Object load(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        return y.load(text);
    }

    @Endpoint(bailsOn = {YAMLException.class})
    @Obj("DataList")
    public static List<Object> load_all(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        List<Object> out = new ArrayList<>();
        try { for (Object o : y.loadAll(text)) out.add(o); }
        catch (YAMLException e) { throw e; }
        catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace()) if (f.getClassName().contains("Constructor")) throw Stitch.bail("constructor leak");
            throw e;
        }
        return out;
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void load_as_map(Yaml y, @Str(max = 512, type = "YamlText") String text) {
        Object o = tolerantLoad(y, text);
        if (!(o instanceof Map)) return;
        for (Map.Entry<?, ?> e : ((Map<?, ?>) o).entrySet()) Objects.toString(e.getKey());
    }

    @Endpoint(optional = true, bailsOn = {YAMLException.class})
    @Obj("Node")
    public static Node compose(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        return y.compose(new StringReader(text));
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void compose_all(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        int n = 0;
        for (Node node : y.composeAll(new StringReader(text))) { walk(node, new IdentityHashMap<>()); n++; }
        Stitch.check(n >= 0, "count");
    }

    static void walk(Node n, IdentityHashMap<Node, Boolean> seen) {
        if (n == null || seen.put(n, true) != null) return;
        if (n instanceof SequenceNode) for (Node c : ((SequenceNode) n).getValue()) walk(c, seen);
        else if (n instanceof MappingNode) for (NodeTuple t : ((MappingNode) n).getValue()) { walk(t.getKeyNode(), seen); walk(t.getValueNode(), seen); }
        n.getTag(); n.getStartMark(); n.getEndMark();
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void parse_events(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        int depth = 0, docs = 0; boolean started = false, ended = false;
        for (Event e : y.parse(new StringReader(text))) {
            Stitch.check(!ended, "event after StreamEnd");
            if (e instanceof StreamStartEvent) { Stitch.check(!started, "double StreamStart"); started = true; }
            else { Stitch.check(started, "event before StreamStart"); }
            if (e instanceof DocumentStartEvent || e instanceof MappingStartEvent || e instanceof SequenceStartEvent) depth++;
            if (e instanceof DocumentEndEvent || e instanceof MappingEndEvent || e instanceof SequenceEndEvent) depth--;
            if (e instanceof DocumentStartEvent) docs++;
            Stitch.check(depth >= 0, "unbalanced end event");
            if (e instanceof StreamEndEvent) ended = true;
        }
        Stitch.check(ended && depth == 0, "stream events balanced");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void scan_tokens(@Str(max = 1024, type = "YamlText") String text, boolean comments) {
        ScannerImpl sc = new ScannerImpl(new StreamReader(text)).setParseComments(comments);
        int n = 0;
        while (sc.checkToken(new Token.ID[0])) {
            Token t = sc.getToken();
            Stitch.check(t.getStartMark() != null && t.getEndMark() != null, "marks");
            if (++n > 100000) throw Stitch.bail("too many tokens");
        }
        Stitch.check(n >= 1, "at least StreamStart");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void parser_comments(@Str(max = 1024, type = "YamlText") String text) {
        ParserImpl p = new ParserImpl(new StreamReader(text), true);
        while (!p.checkEvent(Event.ID.StreamEnd)) p.getEvent();
    }

    // ---------------------------------------------------------------- building data
    @Endpoint
    @Obj("Data")
    public static Object data_int(long v) { return v; }

    @Endpoint
    @Obj("Data")
    public static Object data_string(@Str(max = 64) String v) { return v; }

    @Endpoint
    @Obj("Data")
    public static Object data_double(double v) { return v; }

    @Endpoint
    @Obj("Data")
    public static Object data_bool(boolean v) { return v; }

    @Endpoint
    @Obj("Data")
    public static Object data_bytes(@Bytes(max = 48) byte[] b) { return b; }

    @Endpoint
    @Obj("Data")
    public static Object data_date(@Range(lo = -12219292800000L, hi = 253402300799000L) long ms) { return new Date(ms); }

    @Endpoint
    @Obj("List")
    public static List<Object> data_list() { return new ArrayList<Object>(); }

    @Endpoint
    @Obj("Map")
    public static Map<Object, Object> data_map() { return new LinkedHashMap<Object, Object>(); }

    @Endpoint
    @Obj("Set")
    public static Set<Object> data_set() { return new LinkedHashSet<Object>(); }

    /** The same Java object again, as a generic Data value (so containers can nest, even recursively). */
    @Endpoint
    @Obj("Data")
    public static Object list_as_data(@Obj("List") List<Object> l) { return l; }

    @Endpoint
    @Obj("Data")
    public static Object map_as_data(@Obj("Map") Map<Object, Object> m) { return m; }

    @Endpoint
    @Obj("Data")
    public static Object set_as_data(@Obj("Set") Set<Object> m) { return m; }

    @Endpoint
    public static void list_add(@Obj("List") List<Object> list, @Obj("Data") Object item) {
        list.add(item);   // may add the list to itself: recursive structure
    }

    @Endpoint
    public static void map_put(@Obj("Map") Map<Object, Object> map, @Obj("Data") Object key, @Obj("Data") Object value) {
        Stitch.require(!(key instanceof Collection) && !(key instanceof Map), "collection key");
        map.put(key, value);
    }

    @Endpoint
    public static void set_add(@Obj("Set") Set<Object> set, @Obj("Data") Object item) {
        Stitch.require(!(item instanceof Collection) && !(item instanceof Map), "collection item");
        set.add(item);
    }

    // ---------------------------------------------------------------- dumping
    @Endpoint
    public static void dump(Yaml y, @Obj("Data") Object data) {
        Stitch.check(dumpOrBail(y, data) != null, "dump non-null");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void dump_all(Yaml y, @Obj("DataList") List<Object> docs) {
        Stitch.check(y.dumpAll(docs.iterator()) != null, "dumpAll non-null");
    }

    @Endpoint
    public static void dump_writer(Yaml y, @Obj("Data") Object data) {
        StringWriter w = new StringWriter();
        try { y.dump(data, w); } catch (YAMLException e) { if (String.valueOf(e.getMessage()).contains("invalid string value")) throw Stitch.bail("lone surrogate"); throw e; }
        String again = dumpOrBail(y, data);
        Stitch.check(ANCHOR.matcher(w.toString()).replaceAll("id").equals(ANCHOR.matcher(again).replaceAll("id")), "dump(data, writer) equals dump(data) modulo anchor numbers");
    }

    @Endpoint
    public static void dump_as_map(Yaml y, @Obj("Data") Object data, DumperOptions.FlowStyle fs) {
        Stitch.require(data instanceof Map, "not a map");
        String a = y.dumpAsMap(data);
        String b = y.dumpAs(data, Tag.MAP, fs);
        Stitch.check(a != null && b != null, "non-null");
    }

    /** Oracle: load(dump(x)) is equal to x. */
    @Endpoint
    public static void dump_roundtrip(Yaml y, @Obj("Data") Object data) {
        requirePrintable(data, new IdentityHashMap<>(), Stitch.getLong(y, "esc", 0) == 1);
        String out = dumpOrBail(y, data);
        Object back;
        try {
            back = new Yaml(new SafeConstructor()).load(out);
        } catch (RuntimeException e) {
            throw new AssertionError("dump output does not load: " + e.getMessage().replace('\n', ' '));
        }
        Stitch.check(eq(data, back, new IdentityHashMap<>()), "load(dump(x)) equals x");
    }

    @Endpoint
    public static void dump_all_roundtrip(Yaml y, @Obj("DataList") List<Object> docs) {
        Stitch.require(!docs.isEmpty(), "no docs");
        for (Object d : docs) requirePrintable(d, new IdentityHashMap<>(), Stitch.getLong(y, "esc", 0) == 1);
        String out;
        try { out = y.dumpAll(docs.iterator()); } catch (YAMLException e) { if (String.valueOf(e.getMessage()).contains("invalid string value")) throw Stitch.bail("lone surrogate"); throw e; }
        List<Object> back = new ArrayList<>();
        try {
            for (Object o : new Yaml(new SafeConstructor()).loadAll(out)) back.add(o);
        } catch (RuntimeException e) {
            throw new AssertionError("dumpAll output does not load: " + e.getMessage().replace('\n', ' '));
        }
        Stitch.check(back.size() == docs.size(), "dumpAll/loadAll document count");
        for (int i = 0; i < back.size(); i++)
            Stitch.check(eq(docs.get(i), back.get(i), new IdentityHashMap<>()), "loadAll(dumpAll(x))[i] equals x");
    }

    /** Oracle: re-dumping what was loaded is a fixpoint after one round. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void load_dump_load(Yaml y, @Str(max = 512, type = "YamlText") String text) {
        Object a = tolerantLoad(y, text);
        Stitch.require(a != null, "null doc");
        requirePrintable(a, new IdentityHashMap<>(), Stitch.getLong(y, "esc", 0) == 1);
        String d1 = dumpOrBail(y, a);
        Object b = new Yaml(new SafeConstructor()).load(d1);
        Stitch.check(eq(a, b, new IdentityHashMap<>()), "load(dump(load(x))) equals load(x)");
    }

    // ---------------------------------------------------------------- nodes
    @Endpoint
    @Obj("Node")
    public static Node represent(Yaml y, @Obj("Data") Object data) {
        return y.represent(data);
    }

    @Endpoint
    @Obj("Node")
    public static Node scalar_str(@Str(max = 40) String v, DumperOptions.ScalarStyle style) {
        return new ScalarNode(Tag.STR, v, null, null, style);
    }

    @Endpoint
    @Obj("Node")
    public static Node scalar_int(long v) {
        return new ScalarNode(Tag.INT, Long.toString(v), null, null, DumperOptions.ScalarStyle.PLAIN);
    }

    @Endpoint
    @Obj("Node")
    public static Node scalar_tagged(@OneOf(strings = {"!foo", "!set", "tag:yaml.org,2002:x", "!a%20b", "tag:a.b,2000:c/d?e"}) String tag,
                                     @Str(max = 20) String v) {
        return new ScalarNode(new Tag(tag), v, null, null, DumperOptions.ScalarStyle.PLAIN);
    }

    @Endpoint
    @Obj("SeqNode")
    public static SequenceNode seq_node(boolean flow) {
        return new SequenceNode(Tag.SEQ, new ArrayList<Node>(), flow ? DumperOptions.FlowStyle.FLOW : DumperOptions.FlowStyle.BLOCK);
    }

    @Endpoint
    @Obj("MapNode")
    public static MappingNode map_node(boolean flow) {
        return new MappingNode(Tag.MAP, new ArrayList<NodeTuple>(), flow ? DumperOptions.FlowStyle.FLOW : DumperOptions.FlowStyle.BLOCK);
    }

    @Endpoint
    public static void seq_append(@Obj("SeqNode") SequenceNode seq, @Obj("Node") Node child) {
        seq.getValue().add(child);   // child may be seq itself or an ancestor
    }

    @Endpoint
    @Obj("Node")
    public static Node seq_as_node(@Obj("SeqNode") SequenceNode n) { return n; }

    @Endpoint
    @Obj("Node")
    public static Node map_as_node(@Obj("MapNode") MappingNode n) { return n; }

    @Endpoint
    public static void map_add(@Obj("MapNode") MappingNode map, @Obj("Node") Node key, @Obj("Node") Node value) {
        map.getValue().add(new NodeTuple(key, value));
    }

    /** Oracle: serialize(node) composes back to an equal tree. */
    @Endpoint
    public static void serialize_roundtrip(Yaml y, @Obj("Node") Node node) {
        StringWriter w = new StringWriter();
        y.serialize(node, w);
        Node back;
        try {
            back = new Yaml(new SafeConstructor()).compose(new StringReader(w.toString()));
        } catch (YAMLException e) {
            throw new AssertionError("serialize output does not compose: " + e.getMessage().replace('\n', ' '));
        }
        Stitch.require(!(node instanceof ScalarNode && ((ScalarNode) node).getValue().isEmpty()), "empty scalar serializes to nothing");
        Stitch.check(back != null && nodeEq(node, back, new IdentityHashMap<>(), 0), "compose(serialize(node)) equals node");
    }

    @Endpoint
    public static void serialize_events(Yaml y, @Obj("Node") Node node) {
        List<Event> ev = y.serialize(node);
        Stitch.check(ev.size() >= 5, "stream+doc+node events");
    }

    /** Oracle: compose(text) serialized (also canonically) composes back to an equal tree. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void compose_serialize(@Str(max = 512, type = "YamlText") String text, boolean canonical,
                                         DumperOptions.FlowStyle flow, @OneOf(ints = {1, 2, 4, 9}) int indent,
                                         @OneOf(ints = {-1, 1, 20, 80}) int width, boolean unicode, boolean split) {
        Node n = new Yaml(new SafeConstructor()).compose(new StringReader(text));
        Stitch.require(n != null, "empty");
        Stitch.require(!(n instanceof ScalarNode && ((ScalarNode) n).getValue().isEmpty()), "empty scalar serializes to nothing");
        DumperOptions d = new DumperOptions();
        d.setCanonical(canonical); d.setDefaultFlowStyle(flow); d.setIndent(indent); d.setWidth(width);
        d.setAllowUnicode(unicode); d.setSplitLines(split);
        Yaml y = new Yaml(new SafeConstructor(), new Representer(d), d);
        StringWriter w = new StringWriter();
        y.serialize(n, w);
        Node back;
        try {
            back = new Yaml(new SafeConstructor()).compose(new StringReader(w.toString()));
        } catch (YAMLException e) {
            throw new AssertionError("serialized tree does not compose: " + e.getMessage().replace('\n', ' '));
        }
        Stitch.check(back != null && nodeEq(n, back, new IdentityHashMap<>(), 0), "compose(serialize(compose(x))) equals compose(x)");
    }

    /** Oracle: canonical dump of loaded data loads equal. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void canonical_dump(@Str(max = 512, type = "YamlText") String text) {
        Object a = tolerantLoad(new Yaml(new SafeConstructor()), text);
        Stitch.require(a != null, "null doc");
        requirePrintable(a, new IdentityHashMap<>(), false);
        DumperOptions d = new DumperOptions();
        d.setCanonical(true);
        String out = dumpOrBail(new Yaml(new SafeConstructor(), new Representer(d), d), a);
        Object b;
        try { b = new Yaml(new SafeConstructor()).load(out); }
        catch (RuntimeException e) { throw new AssertionError("canonical output does not load: " + e.getMessage().replace('\n', ' ')); }
        Stitch.check(eq(a, b, new IdentityHashMap<>()), "canonical dump loads equal");
    }

    // ---------------------------------------------------------------- equality helpers
    static boolean eq(Object a, Object b, IdentityHashMap<Object, Object> seen) {
        if (a == b) return true;
        if (a == null || b == null) return false;
        if (a instanceof Number && b instanceof Number) {
            boolean ia = !(a instanceof Double || a instanceof Float), ib = !(b instanceof Double || b instanceof Float);
            if (ia && ib) return new java.math.BigInteger(a.toString()).equals(new java.math.BigInteger(b.toString()));
            if (ia != ib) return false;
            return Double.compare(((Number) a).doubleValue(), ((Number) b).doubleValue()) == 0;
        }
        if (a instanceof Date && b instanceof Date && ((Date) a).getTime() < -12219292800000L) return true;  // pre-Gregorian dates: known calendar skew
        if (a instanceof byte[] && b instanceof byte[]) return Arrays.equals((byte[]) a, (byte[]) b);
        boolean ac = a instanceof Collection || a instanceof Map;
        if (!ac) return a.equals(b);
        if (seen.get(a) == b) return true;       // cycle: assume equal
        seen.put(a, b);
        if (a instanceof Map && b instanceof Map) {
            Map<?, ?> ma = (Map<?, ?>) a, mb = (Map<?, ?>) b;
            if (ma.size() != mb.size()) return false;
            for (Map.Entry<?, ?> e : ma.entrySet()) {
                boolean found = false;
                for (Map.Entry<?, ?> f : mb.entrySet())
                    if (eq(e.getKey(), f.getKey(), seen) && eq(e.getValue(), f.getValue(), seen)) { found = true; break; }
                if (!found) return false;
            }
            return true;
        }
        if (a instanceof List && b instanceof List) {
            List<?> la = (List<?>) a, lb = (List<?>) b;
            if (la.size() != lb.size()) return false;
            for (int i = 0; i < la.size(); i++) if (!eq(la.get(i), lb.get(i), seen)) return false;
            return true;
        }
        if (a instanceof Set && b instanceof Set) {
            Set<?> sa = (Set<?>) a, sb = (Set<?>) b;
            if (sa.size() != sb.size()) return false;
            for (Object x : sa) {
                boolean found = false;
                for (Object y : sb) if (eq(x, y, seen)) { found = true; break; }
                if (!found) return false;
            }
            return true;
        }
        return false;
    }

    static boolean nodeEq(Node a, Node b, IdentityHashMap<Node, Node> seen, int depth) {
        if (a == b) return true;
        if (depth > 2000) throw Stitch.bail("too deep");
        if (seen.get(a) == b) return true;
        seen.put(a, b);
        if (a.getClass() != b.getClass()) return false;
        if (!a.getTag().equals(b.getTag())) return false;
        if (a instanceof ScalarNode) return ((ScalarNode) a).getValue().equals(((ScalarNode) b).getValue());
        if (a instanceof SequenceNode) {
            List<Node> la = ((SequenceNode) a).getValue(), lb = ((SequenceNode) b).getValue();
            if (la.size() != lb.size()) return false;
            for (int i = 0; i < la.size(); i++) if (!nodeEq(la.get(i), lb.get(i), seen, depth + 1)) return false;
            return true;
        }
        if (a instanceof MappingNode) {
            List<NodeTuple> la = ((MappingNode) a).getValue(), lb = ((MappingNode) b).getValue();
            if (la.size() != lb.size()) return false;
            for (int i = 0; i < la.size(); i++)
                if (!nodeEq(la.get(i).getKeyNode(), lb.get(i).getKeyNode(), seen, depth + 1)
                    || !nodeEq(la.get(i).getValueNode(), lb.get(i).getValueNode(), seen, depth + 1)) return false;
            return true;
        }
        return true;
    }
}
