import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.regex.Pattern;
import org.yaml.snakeyaml.DumperOptions;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.TypeDescription;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.comments.CommentEventsCollector;
import org.yaml.snakeyaml.comments.CommentLine;
import org.yaml.snakeyaml.comments.CommentType;
import org.yaml.snakeyaml.composer.Composer;
import org.yaml.snakeyaml.constructor.Constructor;
import org.yaml.snakeyaml.constructor.CustomClassLoaderConstructor;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.emitter.Emitter;
import org.yaml.snakeyaml.emitter.ScalarAnalysis;
import org.yaml.snakeyaml.env.EnvScalarConstructor;
import org.yaml.snakeyaml.error.Mark;
import org.yaml.snakeyaml.error.MarkedYAMLException;
import org.yaml.snakeyaml.error.MissingEnvironmentVariableException;
import org.yaml.snakeyaml.error.YAMLException;
import org.yaml.snakeyaml.events.*;
import org.yaml.snakeyaml.inspector.TagInspector;
import org.yaml.snakeyaml.inspector.UnTrustedTagInspector;
import org.yaml.snakeyaml.introspector.BeanAccess;
import org.yaml.snakeyaml.introspector.Property;
import org.yaml.snakeyaml.introspector.PropertySubstitute;
import org.yaml.snakeyaml.introspector.PropertyUtils;
import org.yaml.snakeyaml.nodes.*;
import org.yaml.snakeyaml.parser.ParserImpl;
import org.yaml.snakeyaml.reader.ReaderException;
import org.yaml.snakeyaml.reader.StreamReader;
import org.yaml.snakeyaml.reader.UnicodeReader;
import org.yaml.snakeyaml.representer.JsonRepresenter;
import org.yaml.snakeyaml.representer.Representer;
import org.yaml.snakeyaml.resolver.Resolver;
import org.yaml.snakeyaml.scanner.Constant;
import org.yaml.snakeyaml.scanner.ScannerImpl;
import org.yaml.snakeyaml.serializer.NumberAnchorGenerator;
import org.yaml.snakeyaml.serializer.Serializer;
import org.yaml.snakeyaml.tokens.*;
import org.yaml.snakeyaml.util.*;
import stitch.*;

/**
 * The rest of the SnakeYAML API: the low-level pipeline (StreamReader, ScannerImpl, ParserImpl,
 * Composer, constructors, Serializer, Emitter), JavaBeans (Constructor with a TypeDescription,
 * PropertyUtils), the JSON representer, env-variable substitution, comments, error marks, the
 * resolver and the utilities.
 * Oracles: the hand-assembled pipeline loads what Yaml.loadAll loads; for a scalar the resolver
 * types, the matching SafeConstructor construct succeeds with a value of that type; emitting
 * parsed (or generated) events and parsing the output gives the same events; beans dump and load
 * back equal; JSON output loads back equal; UriEncoder round trips; option setters read back.
 */
public class Api {
    static LoaderOptions safeLoader() { return new LoaderOptions(); }

    static boolean printable(String s) {
        return s != null && StreamReader.isPrintable(s) && s.indexOf('\u0085') < 0 && s.indexOf('﻿') < 0
            && s.chars().noneMatch(c -> Character.isSurrogate((char) c));
    }

    // ---------------------------------------------------------------- more Yaml producers
    /** Yaml built from every loader and dumper option, through the different Yaml constructors. */
    @Endpoint(bailsOn = {YAMLException.class, IllegalArgumentException.class})
    @Obj("Yaml")
    public static Yaml yaml_full(@Flags({1, 2, 4, 8, 16, 32, 64, 128}) int bits, @OneOf(ints = {50, 5, 100}) int depth,
                                 @OneOf(ints = {3 * 1024 * 1024, 64, 1024}) int codePoints, @Range(lo = 0, hi = 5) int ctor,
                                 @OneOf(strings = {"", "1.1"}) String version, @OneOf(strings = {"UTC", "GMT+05:30", "America/New_York"}) String tz) {
        LoaderOptions lo = new LoaderOptions();
        lo.setNestingDepthLimit(depth);
        lo.setCodePointLimit(codePoints);
        lo.setMergeOnCompose((bits & 1) != 0);
        lo.setProcessComments((bits & 2) != 0);
        lo.setEnumCaseSensitive((bits & 4) != 0);
        lo.setWarnOnDuplicateKeys((bits & 8) != 0);
        lo.setTagInspector(new UnTrustedTagInspector());
        Stitch.check(lo.getNestingDepthLimit() == depth && lo.getCodePointLimit() == codePoints && lo.isMergeOnCompose() == ((bits & 1) != 0)
            && lo.isProcessComments() == ((bits & 2) != 0) && lo.isEnumCaseSensitive() == ((bits & 4) != 0) && lo.isWarnOnDuplicateKeys() == ((bits & 8) != 0)
            && lo.getTagInspector() != null, "LoaderOptions read back");
        lo.isAllowDuplicateKeys(); lo.isWrappedToRootException(); lo.getMaxAliasesForCollections(); lo.getAllowRecursiveKeys();
        DumperOptions d = new DumperOptions();
        if (version.equals("1.1")) d.setVersion(DumperOptions.Version.V1_1);
        d.setTimeZone(TimeZone.getTimeZone(tz));
        d.setAnchorGenerator(new NumberAnchorGenerator(1));
        d.setProcessComments((bits & 16) != 0);
        d.setDereferenceAliases((bits & 32) != 0);
        d.setAllowReadOnlyProperties((bits & 64) != 0);
        if ((bits & 128) != 0) { Map<String, String> tags = new LinkedHashMap<>(); tags.put("!e!", "tag:example.com,2000:"); d.setTags(tags); }
        Stitch.check(d.isProcessComments() == ((bits & 16) != 0) && d.isDereferenceAliases() == ((bits & 32) != 0) && d.isAllowReadOnlyProperties() == ((bits & 64) != 0), "DumperOptions read back");
        d.getVersion(); d.getTimeZone(); d.getAnchorGenerator(); d.getTags();
        Yaml y;
        switch (ctor) {
            case 0: y = new Yaml(lo, d); break;
            case 1: y = new Yaml(lo); break;
            case 2: y = new Yaml(d); break;
            case 3: y = new Yaml(new SafeConstructor(lo), new Representer(d), d, lo, new Resolver()); break;
            case 4: y = new Yaml(new SafeConstructor(lo), new Representer(d), d, new Resolver()); break;
            default: y = new Yaml(new SafeConstructor(lo), new Representer(d)); break;
        }
        if ((bits & 32) != 0) Stitch.set(y, "deref", 1L);   // dumps expand shared references
        y.setName("y" + ctor);
        Stitch.check(y.getName().equals("y" + ctor), "Yaml name read back");
        y.toString();
        return y;
    }

    @Endpoint
    @Obj("Yaml")
    public static Yaml yaml_plain(@Range(lo = 0, hi = 2) int how) {
        switch (how) {
            case 0: return new Yaml();
            case 1: return new Yaml(new Representer(new DumperOptions()));
            default: return new Yaml(new Representer(new DumperOptions()), new DumperOptions());
        }
    }

    /** Implicit resolvers change how plain scalars are typed when loading. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void add_resolver(@Obj("Yaml") Yaml y, @Range(lo = 0, hi = 3) int which, boolean limit) {
        Tag[] tags = {Tag.INT, Tag.BOOL, Tag.STR, new Tag("!color")};
        Pattern[] pats = {Pattern.compile("^0x[0-9a-f]+$"), Pattern.compile("^(?:ja|nein)$"), Pattern.compile("^[a-z]+$"), Pattern.compile("^#[0-9a-f]{6}$")};
        String[] first = {"0", "jn", null, "#"};
        if (limit) y.addImplicitResolver(tags[which], pats[which], first[which], 16);
        else y.addImplicitResolver(tags[which], pats[which], first[which]);
    }

    // ---------------------------------------------------------------- the low-level pipeline
    /** Oracle: StreamReader -> ParserImpl -> Composer -> SafeConstructor gives what Yaml.loadAll gives. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void pipeline(@Str(max = 1024, type = "YamlText") String text, boolean reader, boolean single) {
        LoaderOptions lo = safeLoader();
        StreamReader sr = reader ? new StreamReader(new StringReader(text)) : new StreamReader(text);
        ParserImpl parser = new ParserImpl(sr, lo);
        Composer composer = new Composer(parser, new Resolver(), lo);
        SafeConstructor sc = new SafeConstructor(lo);
        sc.setComposer(composer);
        List<Object> got = new ArrayList<>();
        try {
            if (single) {
                got.add(sc.getSingleData(Object.class));
            } else {
                while (sc.checkData()) {
                    got.add(sc.getData());
                    if (got.size() > 1000) throw Stitch.bail("too many documents");
                }
            }
        } catch (YAMLException e) {
            throw e;
        } catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace()) if (f.getClassName().contains("Constructor")) throw Stitch.bail("constructor leak");
            throw e;
        }
        List<Object> ref = new ArrayList<>();
        try {
            for (Object o : new Yaml(new SafeConstructor(safeLoader())).loadAll(text)) ref.add(o);
        } catch (RuntimeException e) {
            throw new AssertionError("loadAll fails where the pipeline succeeded: " + e);
        }
        if (single) {
            Stitch.check(ref.size() <= 1, "getSingleData accepts at most one document");
            Stitch.check(Stubs.eq(got.get(0), ref.isEmpty() ? null : ref.get(0), new IdentityHashMap<>()), "pipeline equals loadAll");
        } else {
            Stitch.check(got.size() == ref.size(), "pipeline document count equals loadAll");
            for (int i = 0; i < got.size(); i++) Stitch.check(Stubs.eq(got.get(i), ref.get(i), new IdentityHashMap<>()), "pipeline equals loadAll");
        }
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void composer(@Str(max = 1024, type = "YamlText") String text, boolean single, boolean comments) {
        LoaderOptions lo = safeLoader();
        lo.setProcessComments(comments);
        Composer c = new Composer(new ParserImpl(new StreamReader(text), lo), new Resolver(), lo);
        if (single) {
            Node n = c.getSingleNode();
            if (n != null) node_inspect(n);
        } else {
            int k = 0;
            while (c.checkNode()) {
                node_inspect(c.getNode());
                if (++k > 1000) throw Stitch.bail("too many documents");
            }
        }
    }

    /** Oracle: a scalar the resolver types is constructed by the matching construct, to that type. */
    @Endpoint
    public static void construct_scalar(@Str(max = 48, type = "YamlText") String value, boolean implicit, @Bytes(max = 32) byte[] bin) {
        Resolver r = new Resolver();
        Tag tag = r.resolve(NodeId.scalar, value, implicit);
        SafeConstructor sc = new SafeConstructor(safeLoader());
        ScalarNode n = new ScalarNode(tag, value, null, null, DumperOptions.ScalarStyle.PLAIN);
        Object o;
        if (tag.equals(Tag.INT)) { o = sc.new ConstructYamlInt().construct(n); Stitch.check(o instanceof Integer || o instanceof Long || o instanceof java.math.BigInteger, "!!int constructs an integer"); }
        else if (tag.equals(Tag.FLOAT)) { o = sc.new ConstructYamlFloat().construct(n); Stitch.check(o instanceof Double, "!!float constructs a Double"); }
        else if (tag.equals(Tag.BOOL)) { o = sc.new ConstructYamlBool().construct(n); Stitch.check(o instanceof Boolean, "!!bool constructs a Boolean"); }
        else if (tag.equals(Tag.NULL)) { o = sc.new ConstructYamlNull().construct(n); Stitch.check(o == null, "!!null constructs null"); }
        else if (tag.equals(Tag.TIMESTAMP)) {
            SafeConstructor.ConstructYamlTimestamp ts = new SafeConstructor.ConstructYamlTimestamp();
            o = ts.construct(n);
            Stitch.check(o instanceof Date && ts.getCalendar() != null, "!!timestamp constructs a Date");
        }
        else { o = sc.new ConstructYamlStr().construct(n); Stitch.check(value.equals(o), "!!str constructs the value"); }
        String b64 = Base64.getEncoder().encodeToString(bin);
        Object b = sc.new ConstructYamlBinary().construct(new ScalarNode(Tag.BINARY, b64, null, null, DumperOptions.ScalarStyle.PLAIN));
        Stitch.check(Arrays.equals((byte[]) b, bin), "!!binary decodes base64");
        try {
            new SafeConstructor.ConstructUndefined().construct(n);
            Stitch.check(false, "ConstructUndefined throws");
        } catch (YAMLException expected) { /* documented */ }
    }

    /** The collection constructs, driven through a constructor's normal two-step construction. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void construct_collection(@Str(max = 512, type = "YamlText") String text, @Range(lo = 0, hi = 4) int kind) {
        String[] tags = {"!!seq", "!!map", "!!set", "!!omap", "!!pairs"};
        String doc = tags[kind] + " " + text;
        Object o = Stubs.tolerantLoad(new Yaml(new SafeConstructor(safeLoader())), doc);
        switch (kind) {
            case 0: case 4: Stitch.check(o instanceof List, tags[kind] + " constructs a List"); break;
            case 2: Stitch.check(o instanceof Set, "!!set constructs a Set"); break;
            default: Stitch.check(o instanceof Map, tags[kind] + " constructs a Map");
        }
        SafeConstructor sc = new SafeConstructor(safeLoader());
        sc.new ConstructYamlSeq(); sc.new ConstructYamlMap(); sc.new ConstructYamlSet(); sc.new ConstructYamlOmap(); sc.new ConstructYamlPairs();
    }

    @Endpoint
    public static void constructor_settings(boolean dup, boolean warn, boolean wrap, boolean enumCase) {
        SafeConstructor sc = new SafeConstructor(safeLoader());
        sc.setAllowDuplicateKeys(dup); sc.setWarnOnDuplicateKeys(warn); sc.setWrappedToRootException(wrap); sc.setEnumCaseSensitive(enumCase);
        Stitch.check(sc.isAllowDuplicateKeys() == dup && sc.isWarnOnDuplicateKeys() == warn && sc.isWrappedToRootException() == wrap && sc.isEnumCaseSensitive() == enumCase, "BaseConstructor settings read back");
        sc.getLoadingConfig(); sc.isExplicitPropertyUtils(); sc.getPropertyUtils();
        sc.setPropertyUtils(new PropertyUtils());
        Stitch.check(sc.isExplicitPropertyUtils(), "explicit PropertyUtils");
    }

    // ---------------------------------------------------------------- events in and out
    static boolean eventsEqual(List<Event> a, List<Event> b) {
        if (a.size() != b.size()) return false;
        for (int i = 0; i < a.size(); i++) {
            Event x = a.get(i), y = b.get(i);
            if (x.getEventId() != y.getEventId()) return false;
            if (x instanceof ScalarEvent && !((ScalarEvent) x).getValue().equals(((ScalarEvent) y).getValue())) return false;
        }
        return true;
    }

    static List<Event> parseEvents(String text) {
        List<Event> out = new ArrayList<>();
        for (Event e : new Yaml(new SafeConstructor(safeLoader())).parse(new StringReader(text))) out.add(e);
        return out;
    }

    static String emitAll(List<Event> events, DumperOptions d) throws IOException {
        StringWriter w = new StringWriter();
        Emitter em = new Emitter(w, d);
        for (Event e : events) em.emit(e);
        return w.toString();
    }

    /** Oracle: parse(emit(parse(x))) has the same events as parse(x). */
    @Endpoint(bailsOn = {YAMLException.class, IOException.class})
    public static void emit_parsed(@Str(max = 1024, type = "YamlText") String text, boolean canonical, DumperOptions.FlowStyle flow,
                                   @OneOf(ints = {2, 4, 9}) int indent, @OneOf(ints = {-1, 20, 80}) int width, boolean unicode) throws IOException {
        List<Event> ev = parseEvents(text);
        for (Event e : ev) Stitch.require(!(e instanceof ScalarEvent) || printable(((ScalarEvent) e).getValue()), "non-printable or NEL/BOM scalar");
        DumperOptions d = new DumperOptions();
        d.setCanonical(canonical); d.setDefaultFlowStyle(flow); d.setIndent(indent); d.setWidth(width); d.setAllowUnicode(unicode);
        String out = emitAll(ev, d);
        List<Event> back;
        try { back = parseEvents(out); }
        catch (YAMLException e) { throw new AssertionError("emitted events do not parse: " + e.getMessage().replace('\n', ' ')); }
        Stitch.check(eventsEqual(ev, back), "parse(emit(events)) == events");
    }

    /** Builds a well-formed event stream from fuzzer choices (scalars with any style, tags, anchors). */
    @Decoder("Events")
    public static List<Event> eventsDecoder(Source s) {
        List<Event> ev = new ArrayList<>();
        ev.add(new StreamStartEvent(null, null));
        int docs = (int) s.range(1, 3);
        for (int i = 0; i < docs; i++) {
            boolean explicit = i > 0 || s.coin(0.3);
            // directives only on the first document: the emitter writes them for a later document without
            // closing the previous one with "...", which its parser rejects (reported via emit_parsed)
            DumperOptions.Version v = i == 0 && s.coin(0.1) ? DumperOptions.Version.V1_1 : null;
            Map<String, String> tags = i == 0 && s.coin(0.1) ? Collections.singletonMap("!e!", "tag:example.com,2000:") : null;
            ev.add(new DocumentStartEvent(null, null, explicit || v != null || tags != null, v, tags));
            genNode(s, ev, 0, new ArrayList<>());
            ev.add(new DocumentEndEvent(null, null, s.coin(0.2)));
        }
        ev.add(new StreamEndEvent(null, null));
        return ev;
    }

    static String anchor(Source s, List<String> anchors) {
        if (!s.coin(0.15)) return null;
        String a = "a" + anchors.size();
        anchors.add(a);
        return a;
    }

    static void genNode(Source s, List<Event> ev, int depth, List<String> anchors) {
        int k = depth > 3 ? 0 : s.choice(4);
        if (k == 3 && anchors.isEmpty()) k = 0;
        switch (k) {
            case 0: {
                String v = s.coin(0.3) ? s.pick("", "a b", "- x", "#c", "null", "123", "a: b", "'q'", "\"d\"", " lead", "trail ", "multi\nline", "\ttab", "x\n\n", "é", " ", "[x]", "{y}", "?", "|", ">", "!t") : Stubs.str(s, 12);
                DumperOptions.ScalarStyle style = s.pick(DumperOptions.ScalarStyle.values());
                boolean tagged = s.coin(0.15);
                String tag = tagged ? s.pick("tag:yaml.org,2002:str", "!local", "tag:example.com,2000:x", "!") : null;
                ImplicitTuple it = tagged ? new ImplicitTuple(false, false) : new ImplicitTuple(true, true);
                ev.add(new ScalarEvent(anchor(s, anchors), tag, it, v, null, null, style));
                break;
            }
            case 1: case 2: {
                boolean map = k == 2;
                String a = anchor(s, anchors);
                boolean implicit = !s.coin(0.1);
                String tag = implicit ? null : (map ? "tag:yaml.org,2002:map" : "tag:yaml.org,2002:seq");
                DumperOptions.FlowStyle fs = s.coin() ? DumperOptions.FlowStyle.FLOW : DumperOptions.FlowStyle.BLOCK;
                ev.add(map ? new MappingStartEvent(a, tag, implicit, null, null, fs) : new SequenceStartEvent(a, tag, implicit, null, null, fs));
                int n = (int) s.range(0, 3);
                for (int i = 0; i < n; i++) {
                    if (map) genNode(s, ev, depth + 1, anchors);
                    genNode(s, ev, depth + 1, anchors);
                }
                ev.add(map ? new MappingEndEvent(null, null) : new SequenceEndEvent(null, null));
                break;
            }
            default:
                ev.add(new AliasEvent(s.pick(anchors), null, null));
        }
    }

    /** Oracle: generated events emit to text that parses back to the same events. */
    @Endpoint(bailsOn = {YAMLException.class, IOException.class})
    public static void emit_generated(@Val(value = "Events", max = 512) List<Event> ev, boolean canonical, @OneOf(ints = {-1, 10, 80}) int width,
                                      DumperOptions.LineBreak lb, boolean unicode, @Range(lo = 1, hi = 10) int indent) throws IOException {
        for (Event e : ev) Stitch.require(!(e instanceof ScalarEvent) || printable(((ScalarEvent) e).getValue()), "non-printable or NEL/BOM scalar");
        DumperOptions d = new DumperOptions();
        d.setCanonical(canonical); d.setWidth(width); d.setLineBreak(lb); d.setAllowUnicode(unicode); d.setIndent(indent);
        String out = emitAll(ev, d);
        List<Event> back;
        try { back = parseEvents(out); }
        catch (YAMLException e) { throw new AssertionError("emitted events do not parse: " + e.getMessage().replace('\n', ' ')); }
        Stitch.check(eventsEqual(ev, back), "parse(emit(events)) == events");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void events_inspect(@Obj("Yaml") Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        for (Event e : y.parse(new StringReader(text))) {
            e.getStartMark(); e.getEndMark();
            Stitch.check(e.is(e.getEventId()), "is(getEventId())");
            if (e instanceof NodeEvent) ((NodeEvent) e).getAnchor();
            if (e instanceof CollectionStartEvent) { CollectionStartEvent c = (CollectionStartEvent) e; c.getTag(); c.getImplicit(); c.getFlowStyle(); c.isFlow(); }
            if (e instanceof ScalarEvent) {
                ScalarEvent s = (ScalarEvent) e;
                s.getTag(); s.getValue(); s.getScalarStyle();
                ImplicitTuple it = s.getImplicit();
                it.canOmitTagInPlainScalar(); it.canOmitTagInNonPlainScalar(); it.bothFalse(); it.toString();
                int styles = (s.isPlain() ? 1 : 0) + (s.isLiteral() ? 1 : 0) + (s.isSQuoted() ? 1 : 0) + (s.isDQuoted() ? 1 : 0) + (s.isFolded() ? 1 : 0) + (s.isJson() ? 1 : 0);
                Stitch.check(styles == 1, "a scalar has exactly one style");
            }
            if (e instanceof DocumentStartEvent) { DocumentStartEvent d = (DocumentStartEvent) e; d.getExplicit(); d.getTags(); if (d.getVersion() != null) { d.getVersion().major(); d.getVersion().minor(); d.getVersion().getRepresentation(); } }
            if (e instanceof DocumentEndEvent) ((DocumentEndEvent) e).getExplicit();
            if (e instanceof CommentEvent) { ((CommentEvent) e).getValue(); ((CommentEvent) e).getCommentType(); }
            e.toString();
        }
    }

    /** Comment events: the parser keeps them when asked, and the collector gathers them. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void comments(@Str(max = 1024, type = "YamlText") String text, @Flags({1, 2, 4}) int types, boolean poll) {
        LoaderOptions lo = safeLoader();
        lo.setProcessComments(true);
        ParserImpl p = new ParserImpl(new StreamReader(text), lo);
        List<CommentType> want = new ArrayList<>();
        if ((types & 1) != 0) want.add(CommentType.BLANK_LINE);
        if ((types & 2) != 0) want.add(CommentType.BLOCK);
        if ((types & 4) != 0) want.add(CommentType.IN_LINE);
        CommentEventsCollector col = new CommentEventsCollector(p, want.toArray(new CommentType[0]));
        int n = 0;
        while (n++ < 10000) {
            Event e;
            if (poll) e = col.collectEventsAndPoll(null);
            else { col.collectEvents(); e = p.getEvent(); }
            for (CommentLine l : col.consume()) { l.getValue(); l.getCommentType(); l.getStartMark(); l.getEndMark(); l.toString(); }
            Stitch.check(col.isEmpty(), "consume() empties the collector");
            if (e == null || e instanceof StreamEndEvent) break;
        }
        Queue<Event> q = new ArrayDeque<>();
        q.add(new CommentEvent(CommentType.BLOCK, "c", null, null));
        q.add(new StreamEndEvent(null, null));
        CommentEventsCollector qc = new CommentEventsCollector(q, CommentType.BLOCK);
        Event first = qc.collectEvents(null);
        Stitch.check(first == null && qc.consume().size() == 1 && q.peek() instanceof StreamEndEvent, "queue collector gathers the comment and stops at the next event");
        new CommentLine(new CommentEvent(CommentType.IN_LINE, "x", null, null));
        new CommentLine(null, null, "y", CommentType.BLOCK);
    }

    // ---------------------------------------------------------------- serializer, nodes
    /** Oracle: Serializer with an explicit root tag and anchor generator composes back to an equal tree. */
    @Endpoint(bailsOn = {IOException.class})
    public static void serializer(@Obj("Node") Node node, boolean explicitStart, boolean explicitEnd, @Range(lo = 0, hi = 9) int anchorStart) throws IOException {
        DumperOptions d = new DumperOptions();
        d.setExplicitStart(explicitStart); d.setExplicitEnd(explicitEnd);
        d.setAnchorGenerator(new NumberAnchorGenerator(anchorStart));
        StringWriter w = new StringWriter();
        Serializer ser = new Serializer(new Emitter(w, d), new Resolver(), d, null);
        ser.open();
        try {
            ser.serialize(node);
        } catch (YAMLException e) {
            throw Stubs.dumpBail(e);
        }
        ser.close();
        Stitch.require(!(node instanceof ScalarNode && ((ScalarNode) node).getValue().isEmpty()), "empty scalar serializes to nothing");
        Node back;
        try { back = new Yaml(new SafeConstructor(Stubs.composeLoader())).compose(new StringReader(w.toString())); }
        catch (YAMLException e) { throw new AssertionError("Serializer output does not compose: " + e.getMessage().replace('\n', ' ')); }
        Stitch.check(back != null && Stubs.nodeEq(node, back, new IdentityHashMap<>(), 0), "compose(Serializer(node)) equals node");
    }

    @Endpoint
    public static void node_inspect(@Obj("Node") Node n) {
        n.getTag(); n.getNodeId(); n.getStartMark(); n.getEndMark(); n.getType(); n.isTwoStepsConstruction(); n.useClassConstructor();
        n.getAnchor(); n.getInLineComments(); n.getBlockComments(); n.getEndComments();
        Tag t = n.getTag();
        t.getValue(); t.isSecondary(); t.startsWith("tag:"); t.isCustomGlobal(); t.isCompatible(String.class); t.matches(String.class);
        try { t.getClassName(); } catch (YAMLException e) { /* documented: not a class tag */ }
        if (n instanceof ScalarNode) { ScalarNode s = (ScalarNode) n; s.getScalarStyle(); Stitch.check(s.isPlain() == (s.getScalarStyle() == DumperOptions.ScalarStyle.PLAIN), "isPlain"); }
        if (n instanceof CollectionNode) { CollectionNode<?> c = (CollectionNode<?>) n; c.getFlowStyle(); c.getValue(); }
        if (n instanceof MappingNode) ((MappingNode) n).isMerged();
        if (n instanceof AnchorNode) ((AnchorNode) n).getRealNode();
    }

    @Endpoint
    public static void node_edit(@Obj("Node") Node n, @Range(lo = 0, hi = 8) int op, @OneOf(strings = {"a", "anc", "x1", "id005"}) String anchor) {
        switch (op) {
            case 0: n.setAnchor(anchor); Stitch.check(anchor.equals(n.getAnchor()), "setAnchor read back"); break;
            case 1: n.setTag(new Tag(anchor.equals("a") ? "!local" : "tag:yaml.org,2002:str")); break;
            case 2: n.setType(String.class); n.setTwoStepsConstruction(true); n.setUseClassConstructor(false); break;
            case 3: n.setBlockComments(new ArrayList<>(Collections.singletonList(new CommentLine(null, null, " c", CommentType.BLOCK)))); n.setInLineComments(null); n.setEndComments(new ArrayList<>()); break;
            case 4: if (n instanceof CollectionNode) { ((CollectionNode<?>) n).setFlowStyle(DumperOptions.FlowStyle.FLOW); ((CollectionNode<?>) n).setEndMark(null); } break;
            case 5: if (n instanceof MappingNode) { MappingNode m = (MappingNode) n; m.setMerged(!m.isMerged()); m.setOnlyKeyType(String.class); m.setTypes(String.class, Object.class); m.setValue(new ArrayList<>(m.getValue())); } break;
            case 6: if (n instanceof SequenceNode) ((SequenceNode) n).setListType(String.class); break;
            case 7: { if (n instanceof MappingNode) { MergeUtils mu = new MergeUtils() { public MappingNode asMappingNode(Node x) { if (x instanceof MappingNode) return (MappingNode) x; throw new YAMLException("not a mapping"); } };
                        try { mu.flatten((MappingNode) n); } catch (YAMLException e) { /* a merge value that is not a mapping */ } } break; }
            default: new AnchorNode(n).getRealNode();
        }
    }

    @Endpoint
    @Obj("Node")
    public static Node mapping_node_full(@Obj("Node") Node key, @Obj("Node") Node value, boolean flow) {
        List<NodeTuple> l = new ArrayList<>();
        l.add(new NodeTuple(key, value));
        return new MappingNode(Tag.MAP, true, l, null, null, flow ? DumperOptions.FlowStyle.FLOW : DumperOptions.FlowStyle.BLOCK);
    }

    // ---------------------------------------------------------------- tokens, reader, errors
    @Endpoint(bailsOn = {YAMLException.class})
    public static void tokens_inspect(@Str(max = 1024, type = "YamlText") String text, boolean comments, boolean peek) {
        LoaderOptions lo = safeLoader();
        lo.setProcessComments(comments);
        ScannerImpl sc = new ScannerImpl(new StreamReader(text), lo);
        int n = 0;
        while (sc.checkToken()) {
            if (peek) sc.peekToken();
            Token t = sc.getToken();
            Token.ID id = t.getTokenId();
            if (t instanceof ScalarToken) { ScalarToken s = (ScalarToken) t; s.getValue(); s.getPlain(); s.getStyle(); s.getTokenId(); Stitch.check(id == Token.ID.Scalar, "ScalarToken id"); }
            else if (t instanceof DirectiveToken) { DirectiveToken<?> d = (DirectiveToken<?>) t; d.getName(); d.getValue(); d.getTokenId(); }
            else if (t instanceof CommentToken) { CommentToken c = (CommentToken) t; c.getValue(); c.getCommentType(); c.getTokenId(); }
            else if (t instanceof TagToken) { TagTuple tt = ((TagToken) t).getValue(); tt.getHandle(); tt.getSuffix(); ((TagToken) t).getTokenId(); }
            else if (t instanceof AliasToken) { ((AliasToken) t).getValue(); ((AliasToken) t).getTokenId(); }
            else if (t instanceof AnchorToken) { ((AnchorToken) t).getValue(); ((AnchorToken) t).getTokenId(); }
            else if (t instanceof BlockEndToken) ((BlockEndToken) t).getTokenId();
            else if (t instanceof BlockEntryToken) ((BlockEntryToken) t).getTokenId();
            else if (t instanceof BlockMappingStartToken) ((BlockMappingStartToken) t).getTokenId();
            else if (t instanceof BlockSequenceStartToken) ((BlockSequenceStartToken) t).getTokenId();
            else if (t instanceof DocumentEndToken) ((DocumentEndToken) t).getTokenId();
            else if (t instanceof DocumentStartToken) ((DocumentStartToken) t).getTokenId();
            else if (t instanceof FlowEntryToken) ((FlowEntryToken) t).getTokenId();
            else if (t instanceof FlowMappingEndToken) ((FlowMappingEndToken) t).getTokenId();
            else if (t instanceof FlowMappingStartToken) ((FlowMappingStartToken) t).getTokenId();
            else if (t instanceof FlowSequenceEndToken) ((FlowSequenceEndToken) t).getTokenId();
            else if (t instanceof FlowSequenceStartToken) ((FlowSequenceStartToken) t).getTokenId();
            else if (t instanceof KeyToken) ((KeyToken) t).getTokenId();
            else if (t instanceof ValueToken) ((ValueToken) t).getTokenId();
            else if (t instanceof StreamEndToken) ((StreamEndToken) t).getTokenId();
            else if (t instanceof StreamStartToken) ((StreamStartToken) t).getTokenId();
            Mark m = t.getStartMark();
            if (m != null) { m.getLine(); m.getColumn(); m.getIndex(); m.getName(); m.getPointer(); m.getBuffer(); }
            if (++n > 100000) throw Stitch.bail("too many tokens");
            if (n == 3) sc.resetDocumentIndex();
        }
        Stitch.check(n >= 1, "at least StreamStart");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void parser_peek(@Str(max = 1024, type = "YamlText") String text) {
        ParserImpl p = new ParserImpl(new StreamReader(text), safeLoader());
        int n = 0;
        while (!p.checkEvent(Event.ID.StreamEnd)) {
            Event a = p.peekEvent();
            Event b = p.getEvent();
            Stitch.check(a == b, "peekEvent() is the next getEvent()");
            if (++n > 100000) throw Stitch.bail("too many events");
        }
    }

    /** StreamReader: positions move forward; prefix(n) is what forward(n) skips. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void stream_reader(@Str(max = 512, type = "YamlText") String text, @Max(64) Source s, boolean reader) {
        StreamReader r = reader ? new StreamReader(new StringReader(text)) : new StreamReader(text);
        int steps = 0;
        while (r.peek() != '\0' && s.more() && steps++ < 64) {
            int before = r.getIndex();
            int n = (int) s.range(1, 8);
            switch (s.choice(6)) {
                case 0: r.forward(); break;
                case 1: r.forward(n); break;
                case 2: { String p = r.prefix(n); String q = r.prefixForward(n); Stitch.check(p.equals(q), "prefix(n) == prefixForward(n)"); break; }
                case 3: r.peek(n); r.getMark().get_snippet(); r.getMark().get_snippet(2, 20); r.getMark().toString(); break;
                case 4: r.getLine(); r.getColumn(); r.getDocumentIndex(); break;
                default: r.resetDocumentIndex();
            }
            Stitch.check(r.getIndex() >= before, "StreamReader index never goes back");
        }
        StreamReader.isPrintable(text);
        if (!text.isEmpty()) StreamReader.isPrintable(text.codePointAt(0));
    }

    /** UnicodeReader: BOM detection; decodes what it detects. */
    @Endpoint(bailsOn = {IOException.class})
    public static void unicode_reader(@Bytes(max = 512, type = "YamlText") byte[] data) throws IOException {
        UnicodeReader r = new UnicodeReader(new ByteArrayInputStream(data));
        StringBuilder b = new StringBuilder();
        char[] buf = new char[64];
        int k;
        try {
            while ((k = r.read(buf, 0, buf.length)) != -1) b.append(buf, 0, k);
            String enc = r.getEncoding();
            Stitch.check(enc != null, "getEncoding after reading");
            boolean bom = data.length >= 2 && ((data[0] == (byte) 0xFE && data[1] == (byte) 0xFF) || (data[0] == (byte) 0xFF && data[1] == (byte) 0xFE))
                || data.length >= 3 && data[0] == (byte) 0xEF && data[1] == (byte) 0xBB && data[2] == (byte) 0xBF;
            if (!bom) Stitch.check(b.toString().equals(new String(data, StandardCharsets.UTF_8)), "UTF-8 without a BOM decodes as UTF-8");
        } finally {
            r.close();
        }
    }

    /** Errors on malformed input carry marks that point into the input. */
    @Endpoint
    public static void error_marks(@Str(max = 512, type = "YamlText") String text, boolean compose) {
        try {
            Yaml y = new Yaml(new SafeConstructor(safeLoader()));
            if (compose) { for (Node n : y.composeAll(new StringReader(text))) n.getTag(); }
            else for (Object o : y.loadAll(text)) Objects.hashCode(o);
        } catch (ReaderException e) {
            e.getName(); e.getCodePoint(); e.getPosition(); e.toString();
            Stitch.check(e.getPosition() >= 0 && e.getPosition() <= text.length(), "ReaderException position within the input");
        } catch (MarkedYAMLException e) {
            e.getMessage(); e.toString(); e.getContext(); e.getProblem();
            for (Mark m : new Mark[] {e.getContextMark(), e.getProblemMark()}) {
                if (m == null) continue;
                m.get_snippet(); m.get_snippet(4, 75); m.toString(); m.getLine(); m.getColumn();
                Stitch.check(m.getIndex() >= 0 && m.getIndex() <= text.codePointCount(0, text.length()), "error mark index within the input");
            }
        } catch (YAMLException e) {
            e.getMessage();
        } catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace()) if (f.getClassName().contains("Constructor")) throw Stitch.bail("constructor leak");
            throw e;
        }
    }

    // ---------------------------------------------------------------- JavaBeans
    public enum Color { RED, GREEN, blue }

    public static class Bean {
        private String name;
        private int count;
        private double ratio;
        private boolean flag;
        private Color color;
        private List<String> items;
        private Map<String, Integer> scores;
        private byte[] data;
        private Bean child;
        public String getName() { return name; }
        public void setName(String v) { name = v; }
        public int getCount() { return count; }
        public void setCount(int v) { count = v; }
        public double getRatio() { return ratio; }
        public void setRatio(double v) { ratio = v; }
        public boolean isFlag() { return flag; }
        public void setFlag(boolean v) { flag = v; }
        public Color getColor() { return color; }
        public void setColor(Color v) { color = v; }
        public List<String> getItems() { return items; }
        public void setItems(List<String> v) { items = v; }
        public Map<String, Integer> getScores() { return scores; }
        public void setScores(Map<String, Integer> v) { scores = v; }
        public byte[] getData() { return data; }
        public void setData(byte[] v) { data = v; }
        public Bean getChild() { return child; }
        public void setChild(Bean v) { child = v; }
        @Override public boolean equals(Object o) {
            if (!(o instanceof Bean)) return false;
            Bean b = (Bean) o;
            return Objects.equals(name, b.name) && count == b.count && Double.compare(ratio, b.ratio) == 0 && flag == b.flag && color == b.color
                && Objects.equals(items, b.items) && Objects.equals(scores, b.scores) && Arrays.equals(data, b.data) && Objects.equals(child, b.child);
        }
        @Override public int hashCode() { return Objects.hash(name, count); }
    }

    static final TagInspector ONLY_BEAN = tag -> tag.getValue().endsWith("Api$Bean");

    @Endpoint
    @Obj("Bean")
    public static Bean bean(@Str(max = 24) String name, int count, double ratio, boolean flag, Color color, @Range(lo = 0, hi = 3) int items,
                            @Range(lo = 0, hi = 3) int scores, @Bytes(max = 16) byte[] data, boolean nulls) {
        Bean b = new Bean();
        b.setName(nulls ? null : name); b.setCount(count); b.setRatio(ratio); b.setFlag(flag); b.setColor(nulls ? null : color);
        if (!nulls) {
            List<String> l = new ArrayList<>(); for (int i = 0; i < items; i++) l.add(name.isEmpty() ? "i" + i : name.substring(0, Math.min(i + 1, name.length())));
            b.setItems(l);
            Map<String, Integer> m = new LinkedHashMap<>(); for (int i = 0; i < scores; i++) m.put("k" + i + name, count + i);
            b.setScores(m);
            b.setData(data);
        }
        return b;
    }

    @Endpoint
    public static void bean_nest(@Obj("Bean") Bean parent, @Obj("Bean") Bean child) {
        for (Bean c = child; c != null; c = c.getChild()) Stitch.require(c != parent, "cycle");
        parent.setChild(child);
    }

    /** Oracle: a bean dumps and loads back equal, through the bean constructors and property access styles. */
    @Endpoint(bailsOn = {YAMLException.class})
    public static void bean_roundtrip(@Obj("Bean") Bean b, @Range(lo = 0, hi = 4) int ctor, BeanAccess access, boolean asMap, boolean reader) throws Exception {
        for (Bean c = b; c != null; c = c.getChild()) {
            Stitch.require(c.getName() == null || printable(c.getName()), "non-printable name");
        }
        LoaderOptions lo = new LoaderOptions();
        lo.setTagInspector(ONLY_BEAN);
        TypeDescription td = new TypeDescription(Bean.class);
        td.putListPropertyType("items", String.class);
        td.putMapPropertyType("scores", String.class, Integer.class);
        td.addPropertyParameters("items", String.class);
        Constructor c;
        switch (ctor) {
            case 0: c = new Constructor(Bean.class, lo); break;
            case 1: c = new Constructor(td, lo); break;
            case 2: c = new Constructor(td, Collections.singletonList(new TypeDescription(Bean.class, new Tag("!bean"))), lo); break;
            case 3: c = new CustomClassLoaderConstructor(Bean.class, Api.class.getClassLoader(), lo); break;
            default: c = new Constructor(lo);
        }
        DumperOptions d = new DumperOptions();
        Representer rep = new Representer(d);
        rep.addTypeDescription(new TypeDescription(Bean.class, Tag.MAP));
        Yaml y = new Yaml(c, rep, d, lo);
        y.setBeanAccess(access);
        y.addTypeDescription(new TypeDescription(Bean.class, (Class<?>) null));
        String out = asMap ? y.dumpAsMap(b) : y.dump(b);
        Bean back = reader ? y.loadAs(new StringReader(out), Bean.class) : y.loadAs(new ByteArrayInputStream(out.getBytes(StandardCharsets.UTF_8)), Bean.class);
        Stitch.check(b.equals(back), "loadAs(dump(bean)) equals bean");
        Bean back2 = y.loadAs(out, Bean.class);
        Stitch.check(b.equals(back2), "loadAs(String) equals bean");
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void bean_load(@Str(max = 512, type = "YamlText") String text, boolean skipMissing, boolean readOnly, boolean includes) {
        LoaderOptions lo = new LoaderOptions();
        TypeDescription td = new TypeDescription(Bean.class, "!bean");
        if (includes) td.setIncludes("name", "count", "child"); else td.setExcludes("data");
        td.substituteProperty("alias", String.class, "getName", "setName");
        td.substituteProperty(new PropertySubstitute("label", String.class));
        PropertyUtils pu = new PropertyUtils();
        pu.setSkipMissingProperties(skipMissing); pu.setAllowReadOnlyProperties(readOnly);
        Stitch.check(pu.isSkipMissingProperties() == skipMissing && pu.isAllowReadOnlyProperties() == readOnly, "PropertyUtils read back");
        td.setPropertyUtils(pu);
        Constructor c = new Constructor(td, lo);
        c.setPropertyUtils(pu);
        try {
            Object o = new Yaml(c).loadAs(text, Bean.class);
            if (o != null) Stitch.check(o instanceof Bean, "loadAs returns the bean type");
        } catch (YAMLException e) {
            throw e;
        } catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace()) if (f.getClassName().contains("Constructor") || f.getClassName().contains("introspector")) throw Stitch.bail("constructor leak");
            throw e;
        }
    }

    @Endpoint(bailsOn = {YAMLException.class})
    public static void bean_introspect(@Obj("Bean") Bean b, BeanAccess access, @OneOf(strings = {"name", "count", "child", "items", "nope", "color"}) String prop) throws Exception {
        PropertyUtils pu = new PropertyUtils();
        pu.setBeanAccess(access);
        Set<Property> props = pu.getProperties(Bean.class);
        pu.getProperties(Bean.class, access);
        for (Property p : props) {
            p.getName(); p.getType(); p.isReadable(); p.isWritable(); p.getActualTypeArguments(); p.getAnnotations(); p.getAnnotation(Deprecated.class);
            if (p.isReadable()) p.get(b);
            p.compareTo(p);
        }
        Property p = pu.getProperty(Bean.class, prop);
        pu.getProperty(Bean.class, prop, access);
        if (p.isReadable() && p.isWritable() && prop.equals("count")) { Object v = p.get(b); p.set(b, v); }
        TypeDescription td = new TypeDescription(Bean.class, new Tag("!bean"), Bean.class);
        td.getTag(); td.getType(); td.getProperty("name"); td.getProperties(); td.toString();
        Bean fresh = (Bean) td.newInstance(new MappingNode(new Tag("!bean"), new ArrayList<>(), DumperOptions.FlowStyle.BLOCK));
        if (fresh != null) { td.setProperty(fresh, "name", "x"); td.finalizeConstruction(fresh); }
        td.newInstance("name", new ScalarNode(Tag.STR, "x", null, null, DumperOptions.ScalarStyle.PLAIN));
        td.setupPropertyType("items", new SequenceNode(Tag.SEQ, new ArrayList<>(), DumperOptions.FlowStyle.BLOCK));
        PropertySubstitute ps = new PropertySubstitute("name", String.class, "getName", "setName");
        ps.setTargetType(Bean.class);
        ps.getName(); ps.getType(); ps.isReadable(); ps.isWritable(); ps.getActualTypeArguments(); ps.setActualTypeArguments(String.class); ps.getAnnotations(); ps.getAnnotation(Deprecated.class);
        ps.set(b, b.getName()); ps.get(b);
        new TypeDescription(Bean.class, Bean.class); new TypeDescription(Bean.class, "!b");
    }

    // ---------------------------------------------------------------- JSON, env
    /** Oracle: JSON output (JsonRepresenter) loads back equal (binary and dates come back as strings: skipped). */
    @Endpoint
    public static void dump_json(@Obj("Data") Object data, DumperOptions.FlowStyle flow) {
        Stubs.requirePrintable(data, new IdentityHashMap<>(), true);
        Stitch.require(!containsType(data, new IdentityHashMap<>()), "binary, dates or sets");
        DumperOptions d = new DumperOptions();
        d.setDefaultScalarStyle(DumperOptions.ScalarStyle.JSON_SCALAR_STYLE);
        d.setNonPrintableStyle(DumperOptions.NonPrintableStyle.ESCAPE);
        d.setDefaultFlowStyle(flow);
        Yaml y = new Yaml(new SafeConstructor(safeLoader()), new JsonRepresenter(d), d);
        String out = Stubs.dumpOrBail(y, data);
        Object back;
        try { back = new Yaml(new SafeConstructor(safeLoader())).load(out); }
        catch (RuntimeException e) { throw new AssertionError("JSON output does not load: " + e.getMessage().replace('\n', ' ')); }
        Stitch.check(Stubs.eq(data, back, new IdentityHashMap<>()), "load(dump_json(x)) equals x");
    }

    static boolean containsType(Object o, IdentityHashMap<Object, Boolean> seen) {
        if (o == null || seen.put(o, true) != null) return false;
        if (o instanceof byte[] || o instanceof Date || o instanceof Calendar || o instanceof Set || o instanceof int[]) return true;
        if (o instanceof Map) for (Map.Entry<?, ?> e : ((Map<?, ?>) o).entrySet()) if (containsType(e.getKey(), seen) || containsType(e.getValue(), seen)) return true;
        if (o instanceof Collection) for (Object x : (Collection<?>) o) if (containsType(x, seen)) return true;
        return false;
    }

    @Endpoint(bailsOn = {MissingEnvironmentVariableException.class})
    public static void env(@OneOf(strings = {"HOME", "PATH", "STITCH_NO_SUCH_VAR", "EMPTY"}) String name, @OneOf(strings = {":-", "-", ":?", "?", ""}) String sep,
                           @OneOf(strings = {"dflt", "a:b", "x"}) String value, boolean viaYaml) {
        EnvScalarConstructor c = new EnvScalarConstructor();
        String envVal = c.getEnv(name);
        String applied = c.apply(name, sep.isEmpty() ? null : sep, value, envVal);
        if (envVal != null && !envVal.isEmpty()) Stitch.check(applied.equals(envVal), "a set variable wins");
        else if (sep.equals(":-")) Stitch.check(applied.equals(value), "':-' gives the default for an unset or empty variable");
        if (viaYaml) {
            Yaml y = new Yaml(new EnvScalarConstructor(new TypeDescription(Object.class), Collections.emptyList(), new LoaderOptions()));
            y.addImplicitResolver(EnvScalarConstructor.ENV_TAG, EnvScalarConstructor.ENV_FORMAT, "$");
            Object o = y.load("v: ${" + name + sep + value + "}");
            Stitch.check(o instanceof Map, "loads a map");
        }
    }

    // ---------------------------------------------------------------- utilities and option enums
    @Endpoint(bailsOn = {java.nio.charset.CharacterCodingException.class})
    public static void uri(@Str(max = 64) String s, @Bytes(max = 32) byte[] b) throws java.nio.charset.CharacterCodingException {
        String e = UriEncoder.encode(s);
        if (!s.chars().anyMatch(c -> Character.isSurrogate((char) c))) Stitch.check(UriEncoder.decode(e).equals(s), "decode(encode(s)) == s");
        try { UriEncoder.decode(s); } catch (IllegalArgumentException x) { /* malformed %-escape */ }
        UriEncoder.decode(java.nio.ByteBuffer.wrap(b));
    }

    @Endpoint
    public static void utils(@Str(max = 16) String s, @Range(lo = 0, hi = 8) int n, DumperOptions.ScalarStyle style, DumperOptions.LineBreak lb) {
        ArrayStack<String> st = new ArrayStack<>(2);
        for (int i = 0; i < n; i++) st.push(s + i);
        for (int i = n - 1; i >= 0; i--) Stitch.check(st.pop().equals(s + i), "ArrayStack is LIFO");
        Stitch.check(st.isEmpty(), "empty after popping all");
        st.push(s); st.clear();
        Tuple<String, Integer> t = new Tuple<>(s, n);
        Stitch.check(t._1().equals(s) && t._2() == n, "Tuple");
        String[] a = {s, "x"};
        Stitch.check(ArrayUtils.toUnmodifiableList(a).size() == 2 && ArrayUtils.toUnmodifiableCompositeList(a, a).size() == 4, "ArrayUtils sizes");
        try {
            DumperOptions.FlowStyle f = EnumUtils.findEnumInsensitiveCase(DumperOptions.FlowStyle.class, s);
            Stitch.check(f.name().equalsIgnoreCase(s), "findEnumInsensitiveCase matches ignoring case");
        } catch (IllegalArgumentException e) { /* documented: no such constant */ }
        new EnumUtils();
        PlatformFeatureDetector pf = new PlatformFeatureDetector();
        pf.isRunningOnAndroid(); pf.isIntrospectionAvailable();
        Character ch = style.getChar();
        if (style != DumperOptions.ScalarStyle.JSON_SCALAR_STYLE) Stitch.check(DumperOptions.ScalarStyle.createStyle(ch) == style, "createStyle(getChar()) round trips");
        lb.getString(); DumperOptions.LineBreak.getPlatformLineBreak();
        int cp = s.isEmpty() ? 'a' : s.codePointAt(0);
        Constant.ALPHA.has(cp); Constant.LINEBR.hasNo(cp); Constant.NULL_BL_T_LINEBR.has(cp, "#"); Constant.URI_CHARS.hasNo(cp, ":");
        ScalarAnalysis sa = new ScalarAnalysis(s, s.isEmpty(), s.contains("\n"), true, true, true, true);
        sa.getScalar(); sa.isEmpty(); sa.isMultiline(); sa.isAllowFlowPlain(); sa.isAllowBlockPlain(); sa.isAllowSingleQuoted(); sa.isAllowBlock();
        new UnTrustedTagInspector().isGlobalTagAllowed(new Tag("tag:yaml.org,2002:java.io.File"));
        new NumberAnchorGenerator(n).nextAnchor(new ScalarNode(Tag.STR, s, null, null, DumperOptions.ScalarStyle.PLAIN));
        new Tag(String.class).getValue();
        Resolver r = new Resolver();
        r.addImplicitResolver(new Tag("!x"), Pattern.compile("^x+$"), "x");
        r.addImplicitResolver(new Tag("!y"), Pattern.compile("^y+$"), "y", 8);
        r.resolve(NodeId.scalar, s, true); r.resolve(NodeId.sequence, null, true); r.resolve(NodeId.mapping, null, false);
    }

    @Endpoint
    public static void representer_settings(@Obj("Data") Object data, DumperOptions.ScalarStyle style, DumperOptions.FlowStyle flow) {
        Representer rep = new Representer(new DumperOptions());
        rep.setDefaultScalarStyle(style); rep.setDefaultFlowStyle(flow);
        Stitch.check(rep.getDefaultScalarStyle() == style && rep.getDefaultFlowStyle() == flow, "representer styles read back");
        rep.setPropertyUtils(new PropertyUtils());
        Stitch.check(rep.isExplicitPropertyUtils() && rep.getPropertyUtils() != null, "explicit PropertyUtils");
        try {
            Node n = rep.represent(data);
            Stitch.check(n != null, "represent non-null");
        } catch (YAMLException e) {
            throw Stubs.dumpBail(e);
        }
    }

    /** DumperOptions: every setter reads back. */
    @Endpoint
    public static void dumper_options(@Range(lo = 0, hi = 11) int indent, @Range(lo = 0, hi = 10) int indicator, int width, boolean b1, boolean b2,
                                      DumperOptions.FlowStyle flow, DumperOptions.ScalarStyle scalar, DumperOptions.LineBreak lb, DumperOptions.NonPrintableStyle nps,
                                      @Range(lo = 0, hi = 1024) int maxKey) {
        DumperOptions d = new DumperOptions();
        try {
            d.setIndent(indent);
            Stitch.check(indent >= Emitter.MIN_INDENT && indent <= Emitter.MAX_INDENT && d.getIndent() == indent, "setIndent accepts only the documented range");
        } catch (YAMLException e) {
            Stitch.check(indent < Emitter.MIN_INDENT || indent > Emitter.MAX_INDENT, "setIndent rejects only out-of-range values");
        }
        try { d.setIndicatorIndent(indicator); Stitch.check(d.getIndicatorIndent() == indicator, "indicator indent read back"); }
        catch (YAMLException e) { /* documented range check */ }
        d.setWidth(width); d.setSplitLines(b1); d.setPrettyFlow(b2); d.setIndentWithIndicator(b1); d.setCanonical(b2); d.setAllowUnicode(b1);
        d.setExplicitStart(b2); d.setExplicitEnd(b1); d.setDefaultFlowStyle(flow); d.setDefaultScalarStyle(scalar); d.setLineBreak(lb); d.setNonPrintableStyle(nps);
        try { d.setMaxSimpleKeyLength(maxKey); Stitch.check(d.getMaxSimpleKeyLength() == maxKey, "max simple key read back"); }
        catch (YAMLException e) { Stitch.check(maxKey > 1024, "setMaxSimpleKeyLength rejects only > 1024"); }
        Stitch.check(d.getWidth() == width && d.getSplitLines() == b1 && d.isPrettyFlow() == b2 && d.getIndentWithIndicator() == b1 && d.isCanonical() == b2
            && d.isAllowUnicode() == b1 && d.isExplicitStart() == b2 && d.isExplicitEnd() == b1 && d.getDefaultFlowStyle() == flow
            && d.getDefaultScalarStyle() == scalar && d.getLineBreak() == lb && d.getNonPrintableStyle() == nps, "DumperOptions read back");
    }
}
