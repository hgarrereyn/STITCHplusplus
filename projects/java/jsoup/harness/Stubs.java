import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.Charset;
import java.nio.file.Path;
import java.util.*;
import java.util.stream.Collectors;
import org.jsoup.Jsoup;
import org.jsoup.helper.DataUtil;
import org.jsoup.helper.W3CDom;
import org.jsoup.nodes.*;
import org.jsoup.parser.*;
import org.jsoup.safety.Cleaner;
import org.jsoup.safety.Safelist;
import org.jsoup.select.*;
import stitch.*;
import stitch.Range;

/**
 * jsoup: parse HTML/XML (string, reader, bytes with charset detection, streaming), select, traverse
 * and edit the DOM, sanitize untrusted HTML, CSS/XPath queries, entities, W3C DOM conversion.
 * Objects: Document, Element, Node (any node), Elements (a selection), Parser, Tag, Attributes,
 * Safelist, StreamParser.
 * Oracles: clean() output contains no script, event handler or javascript: URL, is idempotent and
 * passes isValid(); the three selector entry points (Element.select, Selector.select, Collector)
 * agree and every match satisfies is(); cssSelector() selects its element; clones serialize equal;
 * streaming parse completes to the same document as a normal parse; entity and CSS identifier
 * escaping round trip; splitText keeps the text; Attributes put/get agree.
 * jsoup reports invalid arguments with ValidationException (an IllegalArgumentException) and bad
 * queries with Selector.SelectorParseException (an IllegalStateException).
 */
public class Stubs {
    // ---------------------------------------------------------------- decoders
    @Decoder("Html")
    public static String html(Source s) {
        StringBuilder b = new StringBuilder();
        String[] tags = {"a", "p", "div", "img", "b", "table", "tr", "td", "script", "style", "svg", "math", "span", "ul", "li", "iframe", "select", "option", "form", "template", "noscript", "input", "textarea", "title", "pre", "frameset", "head", "body", "html", "custom-el", "x:y"};
        while (s.more()) {
            switch (s.choice(7)) {
                case 0: b.append('<').append(s.pick(tags)); while (s.coin(0.4)) b.append(' ').append(s.pick("href", "src", "class", "id", "onclick", "style", "srcdoc", "title", "name", "value", "action", "xmlns", "xmlns:x", "x:attr", "data-foo", "type", "checked")).append("=\"").append(s.coin(0.5) ? s.token() : s.str(8)).append('"'); b.append(s.coin(0.1) ? "/>" : ">"); break;
                case 1: b.append("</").append(s.pick(tags)).append('>'); break;
                case 2: b.append(s.token()); break;
                case 3: b.append(s.pick("&amp;", "&lt;", "&#106;", "&#x3c;", "&nbsp", "&", "&#0;", "&notit;", "&ampx", "&#x110000;", "&#128;")); break;
                case 4: b.append(s.pick("<!DOCTYPE html>", "<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "<!-- c -->", "<![CDATA[x]]>", "<?pi data?>", "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">")); break;
                default: b.append(s.str(10));
            }
        }
        return b.toString();
    }

    @Decoder("Css")
    public static String css(Source s) {
        StringBuilder b = new StringBuilder();
        do {
            b.append(s.coin(0.5) ? s.token() : s.pick("a", "p", "div", "td", "*", "[id]", ".x", "b", "span", "#a", "[class~=x]", "[href$=.pdf]", "[title*=a]", "ns|tag", "*|*", ":root", ":first-child", ":last-of-type", ":only-child", ":empty", ":is(a,b)", ":containsData(x)", ":matchText", ":nth-last-of-type(2)", ":gt(1)", "::textnode", ":comment"));
        } while (s.more());
        return b.toString();
    }

    // ---------------------------------------------------------------- helpers
    static Safelist safelist(int kind, boolean relative) {
        Safelist s;
        switch (kind) {
            case 0: s = Safelist.none(); break;
            case 1: s = Safelist.simpleText(); break;
            case 2: s = Safelist.basic(); break;
            case 3: s = Safelist.basicWithImages(); break;
            default: s = Safelist.relaxed();
        }
        return s.preserveRelativeLinks(relative);
    }

    /** a is b or an ancestor of b: inserting a under b would make a cycle. */
    static boolean ancestorOrSelf(Node a, Node b) {
        for (Node n = b; n != null; n = n.parentNode()) if (n == a) return true;
        return false;
    }

    static final String[] REGEXES = {"a", "^\\s*$", "[0-9]+", "(?i)foo", "\\w+\\s\\w+", ".", "x|y"};

    // ---------------------------------------------------------------- parsing
    @Endpoint
    public static Document parse(@Str(max = 4096, type = "Html") String html, @OneOf(strings = {"", "http://example.com/", "file:/x"}) String base) {
        return Jsoup.parse(html, base);
    }

    @Endpoint
    public static Document parseXml(@Str(max = 2048, type = "Html") String xml) {
        return Jsoup.parse(xml, "", Parser.xmlParser());
    }

    @Endpoint
    public static Document parseFragment(@Str(max = 2048, type = "Html") String html) {
        return Jsoup.parseBodyFragment(html);
    }

    @Endpoint
    public static Document parse_static(@Str(max = 2048, type = "Html") String html, @Range(lo = 0, hi = 4) int how) {
        switch (how) {
            case 0: return Jsoup.parse(html);
            case 1: return Jsoup.parse(html, Parser.htmlParser());
            case 2: return Parser.parse(html, "http://example.com/");
            case 3: return Parser.parseBodyFragment(html, "");
            default: return Jsoup.parseBodyFragment(html, "http://example.com/a/b");
        }
    }

    /** Bytes with charset detection (BOM, meta charset, XML declaration). */
    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static Document parse_bytes(@Bytes(max = 4096, type = "Html") byte[] data,
                                       @OneOf(strings = {"", "UTF-8", "ISO-8859-1", "UTF-16", "UTF-16LE", "Shift_JIS", "nonsense"}) String charset,
                                       @Range(lo = 0, hi = 3) int how, boolean xml) throws IOException {
        String cs = charset.isEmpty() ? null : charset;
        Parser p = xml ? Parser.xmlParser() : Parser.htmlParser();
        switch (how) {
            case 0: return Jsoup.parse(new ByteArrayInputStream(data), cs, "http://example.com/", p);
            case 1: return DataUtil.load(new ByteArrayInputStream(data), cs, "");
            case 2: return DataUtil.load(new ByteArrayInputStream(data), cs, "", p);
            default: return Jsoup.parse(new ByteArrayInputStream(data), cs, "");
        }
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static Document parse_file(@FileParam(max = 4096, type = "Html") Path file, @OneOf(strings = {"", "UTF-8", "windows-1252"}) String charset, @Range(lo = 0, hi = 5) int how) throws IOException {
        String cs = charset.isEmpty() ? null : charset;
        switch (how) {
            case 0: return Jsoup.parse(file, cs);
            case 1: return Jsoup.parse(file);
            case 2: return Jsoup.parse(file.toFile(), cs, "http://example.com/");
            case 3: return Jsoup.parse(file.toFile());
            case 4: return DataUtil.load(file, cs, "", Parser.xmlParser());
            default: return Jsoup.parse(file, cs, "", Parser.htmlParser());
        }
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void read_buffer(@Bytes(max = 4096) byte[] data, @Range(lo = 0, hi = 5000) int max) throws IOException {
        java.nio.ByteBuffer b = DataUtil.readToByteBuffer(new ByteArrayInputStream(data), max);
        Stitch.check(max == 0 ? b.limit() == data.length : b.limit() == Math.min(max, data.length), "readToByteBuffer reads up to max bytes");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Parser parser(boolean xml, @Range(lo = 0, hi = 20) int trackErrors, boolean trackPosition,
                                @Range(lo = 0, hi = 3) int caseMode, @OneOf(ints = {512, 512, 2, 3, 10, 100, 1}) int maxDepth, boolean copy) {
        Parser p = xml ? Parser.xmlParser() : Parser.htmlParser();
        p.setTrackErrors(trackErrors).setTrackPosition(trackPosition).setMaxDepth(maxDepth);
        switch (caseMode) {
            case 1: p.settings(ParseSettings.preserveCase); break;
            case 2: p.settings(ParseSettings.htmlDefault); break;
            case 3: p.settings(new ParseSettings(true, false)); break;
            default: break;
        }
        if (copy) p = p.newInstance();
        Stitch.check(p.isTrackErrors() == (trackErrors > 0), "isTrackErrors");
        Stitch.check(p.isTrackPosition() == trackPosition, "isTrackPosition");
        Stitch.check(p.getMaxDepth() == maxDepth, "getMaxDepth");
        p.settings().preserveTagCase();
        p.settings().preserveAttributeCase();
        p.defaultNamespace();
        p.getTreeBuilder();
        return p;
    }

    @Endpoint
    public static Parser parser_clone(Parser p) {
        return p.clone();
    }

    @Endpoint
    public static void parser_settings(Parser p, @Str(max = 32) String name) {
        ParseSettings s = p.settings();
        String t = s.normalizeTag(name), a = s.normalizeAttribute(name);
        Stitch.check(s.normalizeTag(t).equals(t) && s.normalizeAttribute(a).equals(a), "normalizing is idempotent");
        if (s.preserveTagCase()) Stitch.check(t.equalsIgnoreCase(new ParseSettings(false, false).normalizeTag(name)), "preserveCase differs from the default only in case");
        Stitch.check(a.length() <= name.length(), "normalizeAttribute does not grow");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Document parse_with(Parser p, @Str(max = 4096, type = "Html") String html, boolean reader) {
        Document d = reader ? p.parseInput(new StringReader(html), "http://example.com/") : p.parseInput(html, "");
        ParseErrorList errs = p.getErrors();
        for (ParseError e : errs) {
            e.getErrorMessage();
            Stitch.check(e.getPosition() >= 0 && e.getPosition() <= html.length(), "parse error position within the input");
            e.getCursorPos();
        }
        d.parser();
        return d;
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void parse_fragment_with(Parser p, Element context, @Str(max = 2048, type = "Html") String html, @Range(lo = 0, hi = 3) int how) {
        List<Node> nodes;
        switch (how) {
            case 0: nodes = p.parseFragmentInput(html, context, ""); break;
            case 1: nodes = p.parseFragmentInput(new StringReader(html), context, "http://example.com/"); break;
            case 2: nodes = Parser.parseFragment(html, context, "", ParseErrorList.tracking(5)); break;
            default: nodes = Parser.parseFragment(html, context, "");
        }
        for (Node n : nodes) n.outerHtml();
    }

    @Endpoint
    public static void parse_xml_fragment(@Str(max = 2048, type = "Html") String xml) {
        for (Node n : Parser.parseXmlFragment(xml, "")) n.outerHtml();
        ParseErrorList.noTracking();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Attributes parse_attributes(Parser p, @Str(max = 512, type = "Html") String attrs) {
        Attributes a = p.parseAttributes(attrs);
        for (Attribute at : a) Stitch.check(a.hasKey(at.getKey()), "parsed attribute key is present");
        return a;
    }

    @Endpoint
    public static void unescape(@Str(max = 512, type = "Html") String s, boolean inAttribute, Parser p) {
        String a = Parser.unescapeEntities(s, inAttribute);
        String b = p.unescape(s, inAttribute);
        Entities.unescape(s);
        Stitch.check(a.length() <= s.length() + 8 * s.length(), "unescape length bound");
        b.length();
    }

    // ---------------------------------------------------------------- tags
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Tag tag(@OneOf(strings = {"p", "div", "custom", "x-el", "svg", "math", "img", "script", "textarea", "x:y", "TITLE"}) String name,
                          @OneOf(strings = {"http://www.w3.org/1999/xhtml", "http://www.w3.org/2000/svg", "http://www.w3.org/1998/Math/MathML", "urn:x"}) String ns,
                          @Range(lo = 0, hi = 3) int how) {
        switch (how) {
            case 0: return Tag.valueOf(name);
            case 1: return Tag.valueOf(name, ns, ParseSettings.preserveCase);
            case 2: return new Tag(name, ns);
            default: return new Tag(name);
        }
    }

    static final int[] TAG_OPTIONS = {Tag.Known, Tag.Void, Tag.Block, Tag.InlineContainer, Tag.SelfClose, Tag.SeenSelfClose, Tag.PreserveWhitespace, Tag.RcData, Tag.Data, Tag.FormSubmittable, Tag.TextBoundary, Tag.RcDataRecover};

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void tag_options(Tag t, @Range(lo = 0, hi = 11) int option, boolean set, @OneOf(strings = {"", "renamed", "svg"}) String rename) {
        int o = TAG_OPTIONS[option];
        if (set) { t.set(o); Stitch.check(t.is(o), "set option is set"); }
        else { t.clear(o); Stitch.check(!t.is(o), "cleared option is clear"); }
        if (!rename.isEmpty()) t.name(rename);
        if (rename.equals("svg")) t.namespace(Parser.NamespaceSvg);
    }

    @Endpoint
    public static void tag_inspect(Tag t) {
        Stitch.check(t.getName().equals(t.name()), "getName() == name()");
        t.prefix(); t.localName(); t.normalName(); t.namespace();
        t.isBlock(); t.isInline(); t.isEmpty(); t.isSelfClosing(); t.isKnownTag(); t.preserveWhitespace(); t.isFormSubmittable();
        Tag.isKnownTag(t.normalName());
        Stitch.check(Tag.valueOf(t.normalName()).normalName().equals(t.normalName()), "valueOf(normalName) keeps the name");
    }

    /** Custom tags change how the tokenizer treats their content (Data, RcData, Void ...). */
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void parser_tag(Parser p, Tag t, boolean fresh) {
        TagSet ts = fresh ? new TagSet(TagSet.Html()) : p.tagSet();
        ts.add(t);
        Stitch.check(ts.get(t.name(), t.namespace()) == t, "TagSet.get finds the added tag");
        ts.valueOf(t.name(), t.namespace());
        ts.valueOf(t.name(), t.namespace(), ParseSettings.htmlDefault);
        ts.onNewTag(tag -> tag.isKnownTag());
        if (fresh) p.tagSet(ts);
    }

    @Endpoint
    public static void tagset_empty(Parser p) {
        p.tagSet(new TagSet());
    }

    // ---------------------------------------------------------------- streaming
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static StreamParser stream_open(Parser p, @Str(max = 4096, type = "Html") String html, boolean reader) {
        StreamParser sp = new StreamParser(p.newInstance());   // a private copy: later edits of p do not reach it
        Stitch.set(sp, "html", html);
        Stitch.set(sp, "ref", p.newInstance());
        return reader ? sp.parse(new StringReader(html), "") : sp.parse(html, "");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class, IOException.class})
    public static void stream_step(StreamParser sp, @Range(lo = 0, hi = 6) int op, @Str(max = 32, type = "Css") String css) throws IOException {
        Stitch.set(sp, "modified", 1);
        switch (op) {
            case 0: { Iterator<Element> it = sp.iterator(); if (it.hasNext()) it.next().outerHtml(); break; }
            case 1: sp.selectFirst(css); break;
            case 2: sp.selectNext(css); break;
            case 3: sp.expectNext(css); break;
            case 4: sp.expectFirst(css); break;
            case 5: sp.selectFirst(QueryParser.parse(css)); sp.selectNext(Selector.evaluatorOf(css)); break;
            default: sp.stream().limit(3).forEach(Element::remove);
        }
        sp.document();
    }

    /** Oracle: an untouched stream, fully consumed, completes to the same document as parseInput. */
    @Endpoint(bailsOn = {IOException.class})
    public static Document stream_complete(@Take StreamParser sp, boolean consume) throws IOException {
        try {
            if (consume) sp.stream().forEach(Element::tagName);
            Document d = sp.complete();
            Parser refParser = Stitch.getObj(sp, "ref", Parser.class);
            if (Stitch.getLong(sp, "modified", 0) == 0 && refParser != null) {
                Document ref = refParser.parseInput(Stitch.getString(sp, "html", ""), "");
                Stitch.check(d.outerHtml().equals(ref.outerHtml()), "StreamParser.complete() equals a normal parse");
            }
            return d;
        } finally {
            sp.stop();
            sp.close();
        }
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void stream_fragment(Parser p, Element context, @Str(max = 2048, type = "Html") String html, boolean reader) throws IOException {
        try (StreamParser sp = new StreamParser(p)) {
            if (reader) sp.parseFragment(new StringReader(html), context, ""); else sp.parseFragment(html, context, "");
            for (Node n : sp.completeFragment()) n.nodeName();
        }
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void stream_file(@FileParam(max = 4096, type = "Html") Path file, @Str(max = 32, type = "Css") String css) throws IOException {
        try (StreamParser sp = DataUtil.streamParser(file, Charset.forName("UTF-8"), "", Parser.htmlParser())) {
            while (sp.selectNext(css) != null) { }
        } catch (Selector.SelectorParseException e) {
            throw Stitch.bail("bad query");
        }
    }

    @Drop
    public static void close_stream(StreamParser sp) { sp.close(); }

    // ---------------------------------------------------------------- selecting
    // an invalid query throws Selector.SelectorParseException (an IllegalStateException)
    @Endpoint(optional = true, bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static Element select(Document doc, @Str(max = 64, type = "Css") String css, @Range(lo = 0, hi = 8) int i) {
        Elements els = doc.select(css);
        return els.size() > 0 ? els.get(i % els.size()) : null;
    }

    @Endpoint(optional = true)
    public static Element body(Document doc) {
        return doc.body();
    }

    @Endpoint
    public static Element doc_as_element(Document doc) {
        return doc;
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static Elements select_all(Element root, @Str(max = 64, type = "Css") String css) {
        return root.select(css);
    }

    /** Oracle: Element.select, Selector.select and Collector agree; matches satisfy is(); selectFirst is the first. */
    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void select_agree(Element root, @Str(max = 64, type = "Css") String css) {
        Evaluator ev = QueryParser.parse(css);
        Elements a = root.select(css);
        Elements b = Selector.select(css, root);
        Elements c = Collector.collect(ev, root);
        Stitch.check(a.equals(b) && a.size() == c.size(), "select / Selector.select / Collector.collect agree");
        Stitch.check(Selector.select(ev, root).size() == a.size(), "Selector.select(Evaluator) agrees");
        Stitch.check(root.selectStream(css).count() == a.size() && Collector.stream(ev, root).count() == a.size() && Selector.selectStream(css, root).count() == a.size(), "streams agree");
        Element first = root.selectFirst(css);
        Stitch.check(a.isEmpty() ? first == null : first == a.get(0), "selectFirst is the first match");
        Stitch.check(Collector.findFirst(ev, root) == first && Selector.selectFirst(css, root) == first && root.selectFirst(ev) == first, "findFirst agrees");
        if (root.parent() == null) {
            for (Element e : a) Stitch.check(e.is(css) && e.is(ev), "each match is(query)");
        }
        root.select(ev);
        root.selectStream(ev).count();
        Selector.selectStream(ev, root).count();
        Selector.select(css, (Iterable<Element>) a);
        Selector.selectFirst(css, (Iterable<Element>) a);
        Collector.collectNodes(ev, root, Element.class);
        Collector.streamNodes(ev, root, Element.class).count();
        Collector.findFirstNode(ev, root, Element.class);
        for (Element e : a) { ev.asPredicate(root).test(e); ev.matches(root, e); }
        ev.toString();
        Selector.evaluatorOf(css);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void select_nodes(Element root, @Str(max = 64, type = "Css") String css, @Range(lo = 0, hi = 3) int kind) {
        Class<? extends Node> k = kind == 0 ? TextNode.class : kind == 1 ? Comment.class : kind == 2 ? DataNode.class : Node.class;
        Nodes<? extends Node> ns = root.selectNodes(css, k);
        for (Node n : ns) Stitch.check(k.isInstance(n), "selectNodes returns the requested type");
        Node f = root.selectFirstNode(css, k);
        Stitch.check(ns.isEmpty() ? f == null : f == ns.get(0), "selectFirstNode is the first");
        root.selectNodes(css);
        root.selectNodes(QueryParser.parse(css));
        root.selectNodes(QueryParser.parse(css), k);
        root.selectFirstNode(QueryParser.parse(css), k);
        if (f != null) root.expectFirstNode(css, k);
        ns.outerHtml();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void search(Element e, @Range(lo = 0, hi = 22) int op, @Str(max = 24) String arg, @Range(lo = -1, hi = 8) int idx, @Range(lo = 0, hi = 6) int rx) {
        String re = REGEXES[rx];
        Elements r;
        switch (op) {
            case 0: r = e.getElementsByTag(arg); break;
            case 1: { Element x = e.getElementById(arg); if (x != null) Stitch.check(x.id().equals(arg), "getElementById matches the id"); return; }
            case 2: r = e.getElementsByClass(arg); for (Element x : r) Stitch.check(x.hasClass(arg), "getElementsByClass result hasClass"); break;
            case 3: r = e.getElementsByAttribute(arg); break;
            case 4: r = e.getElementsByAttributeStarting(arg); break;
            case 5: r = e.getElementsByAttributeValue(arg, "x"); break;
            case 6: r = e.getElementsByAttributeValueNot("id", arg); break;
            case 7: r = e.getElementsByAttributeValueStarting("class", arg); break;
            case 8: r = e.getElementsByAttributeValueEnding("href", arg); break;
            case 9: r = e.getElementsByAttributeValueContaining("title", arg); break;
            case 10: r = e.getElementsByAttributeValueMatching(arg.isEmpty() ? "id" : arg, re); break;
            case 11: r = e.getElementsByAttributeValueMatching("class", java.util.regex.Pattern.compile(re)); break;
            case 12: r = e.getElementsByIndexLessThan(idx); break;
            case 13: r = e.getElementsByIndexGreaterThan(idx); break;
            case 14: r = e.getElementsByIndexEquals(idx); break;
            case 15: r = e.getElementsContainingText(arg); break;
            case 16: r = e.getElementsContainingOwnText(arg); break;
            case 17: r = e.getElementsMatchingText(re); break;
            case 18: r = e.getElementsMatchingText(java.util.regex.Pattern.compile(re)); break;
            case 19: r = e.getElementsMatchingOwnText(re); break;
            case 20: r = e.getElementsMatchingOwnText(java.util.regex.Pattern.compile(re)); break;
            case 21: { String q = arg.isEmpty() ? "*" : arg; Element c = e.closest(q); if (c != null && e.root() == e.ownerDocument()) Stitch.check(c.is(q), "closest(q) matches q"); e.closest(QueryParser.parse("div")); return; }
            default: r = e.getAllElements(); Stitch.check(r.size() >= 1 && r.get(0) == e, "getAllElements starts with the element"); break;
        }
        Stitch.check(r != null, "non-null");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void xpath(Element e, @OneOf(strings = {"//a", "//*", "//p/text()", "//div[@id]", "/html/body", "//comment()", "count(//a)", "//*[local-name()='svg']", "//@href", "..", "//x:y", "]["}) String xp) {
        e.selectXpath(xp);
        e.selectXpath(xp, TextNode.class);
        e.selectXpath(xp, Node.class);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void expect_first(Element e, @Str(max = 32, type = "Css") String css0, @Range(lo = 0, hi = 16) int pick) {
        Elements all = e.getAllElements();
        String css = pick < 8 ? css0 : all.get(pick % all.size()).tagName();   // often a tag that is there
        try {
            Element f = e.expectFirst(css);
            Stitch.check(f == e.selectFirst(css), "expectFirst == selectFirst");
        } catch (Selector.SelectorParseException x) {
            throw Stitch.bail("bad query");
        }
    }

    // ---------------------------------------------------------------- elements (a selection)
    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void elements_query(Elements els, @Range(lo = 0, hi = 24) int op, @Str(max = 32, type = "Css") String css, @Range(lo = 0, hi = 8) int i) {
        switch (op) {
            case 0: els.attr("id"); els.hasAttr("class"); els.eachAttr("href"); break;
            case 1: { List<String> t = els.eachText(); Stitch.check(t.size() <= els.size(), "eachText has at most one per element"); els.text(); els.hasText(); break; }
            case 2: els.html(); els.outerHtml(); els.val(); break;
            case 3: els.hasClass(css); els.is(css); break;
            case 4: els.select(css); els.selectFirst(css); break;
            case 5: { Elements n = els.not(css); Stitch.check(n.size() <= els.size(), "not() narrows"); break; }
            case 6: { Elements q = els.eq(i); Stitch.check(q.size() <= 1, "eq() at most one"); break; }
            case 7: els.next(); els.next(css); break;
            case 8: els.nextAll(); els.nextAll(css); break;
            case 9: els.prev(); els.prev(css); break;
            case 10: els.prevAll(); els.prevAll(css); break;
            case 11: els.parents(); els.first(); els.last(); break;
            case 12: els.forms(); els.comments(); els.textNodes(); els.dataNodes(); break;
            case 13: { int[] n = {0}; els.traverse((node, depth) -> n[0]++); els.filter(new NodeFilter() { public FilterResult head(Node node, int depth) { return depth > 2 ? FilterResult.SKIP_CHILDREN : FilterResult.CONTINUE; } }); break; }
            case 14: { Elements c = els.clone(); Stitch.check(c.size() == els.size(), "clone keeps size"); els.asList(); break; }
            case 15: if (!els.isEmpty()) els.expectFirst(css); break;
            case 16: { int n = els.size(); if (n > 0) { els.deselect(Math.floorMod(i, n)); Stitch.check(els.size() == n - 1, "deselect removes one"); } break; }
            case 17: { Elements c = new Elements(els); Elements d = new Elements(els.toArray(new Element[0])); Elements f = new Elements((Collection<Element>) els); new Elements(4); Stitch.check(c.size() == d.size() && d.size() == f.size(), "copies agree"); break; }
            case 18: { int n = els.size(); els.removeIf(e -> e.childNodeSize() == 0); els.deselectAll(); Stitch.check(els.isEmpty() && n >= 0, "deselectAll empties"); break; }
            case 19: { Elements c = els.clone(); c.retainAll(els); c.removeAll(new ArrayList<>(els)); Stitch.check(c.isEmpty(), "removeAll of all"); break; }
            case 20: els.replaceAll(e -> e); break;
            case 21: { int n = els.size(); if (n > 0) els.set(Math.floorMod(i, n), els.get(0)); break; }
            case 22: { Elements c = els.clone(); c.clear(); Nodes<Element> nn = new Nodes<>(els); new Nodes<Element>(); new Nodes<Element>(2); nn.first(); nn.last(); nn.asList(); nn.clone(); break; }
            case 23: { int[] n = {0}; NodeTraversor.traverse((node, depth) -> n[0]++, els); NodeTraversor.filter((node, depth) -> NodeFilter.FilterResult.CONTINUE, els); break; }
            default: for (Element e : els) e.cssSelector();
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void elements_edit(Elements els, @Range(lo = 0, hi = 18) int op, @Str(max = 128, type = "Html") String arg, @Range(lo = 0, hi = 8) int i) {
        for (Element e : els) Stitch.require(e.parent() != null || op < 5 || op == 10 || op == 11, "detached element");
        switch (op) {
            case 0: els.attr(arg.isEmpty() ? "x" : arg, arg); break;
            case 1: els.removeAttr(arg); break;
            case 2: els.addClass(arg); break;
            case 3: els.removeClass(arg); break;
            case 4: els.toggleClass(arg); break;
            case 5: els.val(arg); break;
            case 6: els.html(arg); break;
            case 7: els.prepend(arg); break;
            case 8: els.append(arg); break;
            case 9: els.before(arg); break;
            case 10: els.tagName(arg); break;
            case 11: els.empty(); break;
            case 12: els.after(arg); break;
            case 13: els.wrap(arg.isEmpty() ? "<div>" : arg); break;
            case 14: els.unwrap(); break;
            case 15: els.remove(); break;
            case 16: { int n = els.size(); if (n > 0) { els.remove(i % n); Stitch.check(els.size() == n - 1, "remove(i) removes one"); } break; }
            case 17: { TextNode t = new TextNode(arg); els.append(t); els.prepend(new Comment(arg)); break; }
            default: { Node t = new TextNode(arg); els.before(t); els.after(new TextNode("x")); }
        }
    }

    // ---------------------------------------------------------------- element inspection and edits
    @Endpoint
    public static void inspect(Element e) {
        e.tagName(); e.normalName(); e.nodeName(); e.id(); e.className(); e.classNames(); e.classList();
        e.text(); e.wholeText(); e.ownText(); e.wholeOwnText(); e.hasText(); e.data(); e.val(); e.nodeValue();
        e.html(); e.outerHtml(); e.html(new StringBuilder()); e.outerHtml(new StringBuilder()); e.toString();
        e.siblingElements(); e.nextElementSiblings(); e.previousElementSiblings();
        e.firstElementSibling(); e.lastElementSibling(); e.elementSiblingIndex();
        e.firstElementChild(); e.lastElementChild(); e.children(); e.textNodes(); e.dataNodes();
        e.dataset(); e.attributes(); e.isBlock(); e.tag(); e.parents(); e.root(); e.ownerDocument(); e.baseUri();
        e.stream().count(); e.iterator().hasNext(); e.forEach(Element::tagName); e.forEachNode(Node::nodeName);
        e.endSourceRange(); e.sourceRange(); e.nodeStream().count(); e.nodeStream(TextNode.class).count();
        e.childNodes(); e.childNodesCopy(); e.siblingNodes(); e.siblingIndex(); e.firstChild(); e.lastChild();
        e.nextSibling(); e.previousSibling(); e.firstSibling(); e.lastSibling(); e.nextElementSibling(); e.previousElementSibling();
        e.hasParent(); e.parentNode(); e.parentElement(); e.nameIs("div"); e.parentNameIs("body");
        e.parentElementIs("div", Parser.NamespaceHtml); e.elementIs("p", Parser.NamespaceHtml); e.attributesSize();
        e.attribute("id"); e.absUrl("href"); e.hasAttr("abs:href"); e.attr("abs:src");
        Stitch.check(e.childrenSize() == e.children().size(), "childrenSize() == children().size()");
        Stitch.check(e.childNodeSize() == e.childNodes().size(), "childNodeSize() == childNodes().size()");
        if (e.childrenSize() > 0) Stitch.check(e.child(0) == e.firstElementChild(), "child(0) == firstElementChild()");
        if (e.childNodeSize() > 0) Stitch.check(e.childNode(0) == e.firstChild(), "childNode(0) == firstChild()");
        int[] n = {0};
        e.traverse((node, depth) -> n[0]++);
        e.filter((node, depth) -> NodeFilter.FilterResult.CONTINUE);
        NodeTraversor.traverse((node, depth) -> { }, e);
        NodeTraversor.filter((node, depth) -> NodeFilter.FilterResult.CONTINUE, e);
        new NodeTraversor();
        NodeVisitor v = (node, depth) -> node.nodeName();
        v.traverse(e);
        NodeFilter f = (node, depth) -> NodeFilter.FilterResult.CONTINUE;
        f.traverse(e);
        Stitch.check(n[0] == e.nodeStream().count(), "traverse visits every node of nodeStream()");
    }

    /** Oracle: cssSelector() selects exactly its element in its document. */
    @Endpoint(bailsOn = {Selector.SelectorParseException.class})
    public static void css_selector(Element e) {
        Document d = e.ownerDocument();
        Stitch.require(d != null && e != d, "not in a document");
        String sel = e.cssSelector();
        Elements found = d.select(sel);
        Stitch.check(found.contains(e), "document.select(e.cssSelector()) finds e");
    }

    /** Oracle: a clone serializes like the original (compact output with default settings, so the
     *  owner document's settings and pretty-printing context do not matter). */
    @Endpoint
    public static void clone_eq(Element e, boolean shallow) {
        if (shallow) {
            Element c = e.shallowClone();
            Stitch.check(c.childNodeSize() == 0 && c.attributes().equals(e.attributes()), "shallowClone has the attributes and no children");
            return;
        }
        Element c = e.clone();   // (keeps a shell owner document, for the output settings)
        Document owner = e.ownerDocument();
        if (owner == null) {
            Stitch.check(c.outerHtml().equals(e.outerHtml()), "clone() serializes like the original");
            return;
        }
        Document.OutputSettings def = new Document.OutputSettings();
        Document.OutputSettings o = owner.outputSettings();
        Stitch.require(o.syntax() == def.syntax() && o.escapeMode() == def.escapeMode() && o.charset().equals(def.charset()), "non-default output settings");
        Stitch.require(e != owner, "a document");
        Document holder = new Document("");
        holder.outputSettings().prettyPrint(false);
        holder.appendChild(c);
        boolean pretty = o.prettyPrint();
        o.prettyPrint(false);
        String orig;
        try { orig = e.outerHtml(); } finally { o.prettyPrint(pretty); }
        Stitch.check(c.outerHtml().equals(orig), "clone() serializes like the original");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})  // jsoup's Validate (empty tag names, ...)
    public static void edit(Element e, @OneOf(strings = {"append", "prepend", "before", "after", "wrap", "html", "text", "attr", "addClass", "tagName", "removeAttr",
                                                         "removeClass", "toggleClass", "id", "val", "attrBool", "tagNameNs", "appendElement", "prependElement", "appendText", "prependText",
                                                         "empty", "clearAttributes", "classNames", "appendElementNs", "prependElementNs", "dataset", "abs"}) String op,
                            @Str(max = 256, type = "Html") String arg) {
        switch (op) {
            case "append": e.append(arg); break;
            case "prepend": e.prepend(arg); break;
            case "before": Stitch.require(e.parent() != null, "no parent"); e.before(arg); break;
            case "after": Stitch.require(e.parent() != null, "no parent"); e.after(arg); break;
            case "wrap": Stitch.require(e.parent() != null, "no parent"); e.wrap(arg.isEmpty() ? "<div>" : arg); break;
            case "html": e.html(arg); break;
            case "text": e.text(arg); if (!(e instanceof Document)) Stitch.check(e.childNodeSize() <= 1, "text() leaves at most one child"); break;
            case "attr": e.attr(arg.isEmpty() ? "x" : arg, arg); break;
            case "addClass": e.addClass(arg); break;
            case "tagName": Stitch.require(!arg.isEmpty(), "empty tag"); e.tagName(arg); break;
            case "removeClass": e.removeClass(arg); break;
            case "toggleClass": e.toggleClass(arg); break;
            case "id": e.id(arg); Stitch.check(e.id().equals(arg), "id(x).id() == x"); break;
            case "val": e.val(arg); break;
            case "attrBool": e.attr(arg.isEmpty() ? "checked" : arg, arg.length() % 2 == 0); break;
            case "tagNameNs": Stitch.require(!arg.isEmpty(), "empty tag"); e.tagName(arg, Parser.NamespaceSvg); break;
            case "appendElement": e.appendElement(arg); break;
            case "prependElement": e.prependElement(arg); break;
            case "appendElementNs": e.appendElement(arg, Parser.NamespaceMathml); break;
            case "prependElementNs": e.prependElement(arg, Parser.NamespaceSvg); break;
            case "appendText": e.appendText(arg); break;
            case "prependText": e.prependText(arg); break;
            case "empty": e.empty(); Stitch.check(e.childNodeSize() == 0, "empty() removes all children"); break;
            case "clearAttributes": e.clearAttributes(); Stitch.check(e.attributesSize() == 0, "clearAttributes"); break;
            case "classNames": e.classNames(new LinkedHashSet<>(Arrays.asList(arg.split(" ")))); break;
            case "dataset": e.dataset().put(arg.isEmpty() ? "k" : arg, arg); break;
            case "abs": e.setBaseUri(arg); e.absUrl("href"); break;
            default: e.removeAttr(arg);
        }
    }

    @Endpoint
    public static void structure(@Take Element e, @OneOf(strings = {"remove", "unwrap", "empty", "clone"}) String op) {
        Stitch.require(e.parent() != null || op.equals("empty") || op.equals("clone"), "detached");
        switch (op) {
            case "remove": e.remove(); break;
            case "unwrap": e.unwrap(); break;
            case "empty": e.empty(); break;
            default: e.clone().outerHtml();
        }
    }

    /** Moves one element relative to another. Inserting an element into itself or its own subtree is not allowed. */
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void move(Element target, Element other, @Range(lo = 0, hi = 9) int op, @Range(lo = -1, hi = 6) int index) {
        Stitch.require(!ancestorOrSelf(other, target), "would create a cycle");
        switch (op) {
            case 0: target.appendChild(other); Stitch.check(other.parent() == target, "appendChild sets the parent"); break;
            case 1: target.prependChild(other); Stitch.check(target.childNode(0) == other, "prependChild puts it first"); break;
            case 2: target.insertChildren(index, other); break;
            case 3: target.insertChildren(index, Collections.singletonList(other)); break;
            case 4: other.appendTo(target); break;
            case 5: target.appendChildren(Collections.singletonList(other)); break;
            case 6: target.prependChildren(Collections.singletonList(other)); break;
            case 7: Stitch.require(target.parent() != null, "no parent"); target.before(other); break;
            case 8: Stitch.require(target.parent() != null, "no parent"); target.after(other); break;
            default: Stitch.require(target.parent() != null, "no parent"); target.replaceWith(other);
        }
    }

    @Endpoint(optional = true)
    public static Element child(Element e, @Range(lo = 0, hi = 4) int i) {
        return e.childrenSize() > 0 ? e.child(i % e.childrenSize()) : null;
    }

    @Endpoint(optional = true)
    public static Element parent_of(Element e) {
        return e.parent();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Element new_element(@OneOf(strings = {"div", "p", "span", "svg", "table", "x-y", "form", "script", "a"}) String tag, @Range(lo = 0, hi = 4) int how, Document doc) {
        switch (how) {
            case 0: return new Element(tag);
            case 1: return new Element(tag, Parser.NamespaceSvg);
            case 2: return new Element(Tag.valueOf(tag), "http://example.com/");
            case 3: { Attributes a = new Attributes(); a.put("id", "n"); return new Element(Tag.valueOf(tag), "", a); }
            default: return doc.createElement(tag);
        }
    }

    @Endpoint
    public static void render(Document doc, boolean pretty, @Range(lo = 0, hi = 8) int indent, boolean outline,
                              Entities.EscapeMode mode, Document.OutputSettings.Syntax syntax, @OneOf(strings = {"UTF-8", "US-ASCII", "ISO-8859-1", "Shift_JIS"}) String charset,
                              @Range(lo = 0, hi = 40) int padding) {
        doc.outputSettings().prettyPrint(pretty).indentAmount(indent).outline(outline).syntax(syntax).escapeMode(mode).charset(charset).maxPaddingWidth(padding);  // syntax(xml) resets escapeMode: set it first
        Document.OutputSettings o = doc.outputSettings();
        Stitch.check(o.prettyPrint() == pretty && o.outline() == outline && o.indentAmount() == indent && o.escapeMode() == mode && o.syntax() == syntax && o.maxPaddingWidth() == padding, "output settings read back");
        o.charset();
        String out = doc.outerHtml();
        Document again = Jsoup.parse(out);
        again.outerHtml();
        doc.text();
        doc.outputSettings(o.clone());
        new Document.OutputSettings().charset(Charset.forName("UTF-8"));
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static void document_ops(Document doc, @Range(lo = 0, hi = 10) int op, @Str(max = 64) String arg) {
        switch (op) {
            case 0: doc.title(arg); doc.title(); break;
            case 1: doc.charset(Charset.forName(arg.length() % 2 == 0 ? "UTF-8" : "ISO-8859-1")); doc.charset(); break;
            case 2: doc.location(); doc.documentType(); doc.head(); doc.body(); doc.forms(); break;
            case 3: doc.quirksMode(Document.QuirksMode.values()[arg.length() % 3]); doc.quirksMode(); break;
            case 4: { Document c = doc.clone(); Stitch.check(c.outerHtml().equals(doc.outerHtml()), "Document.clone() serializes equal"); break; }
            case 5: doc.shallowClone(); break;
            case 6: doc.expectForm(arg.isEmpty() ? "form" : arg); break;
            case 7: doc.text(arg); break;
            case 8: Document.createShell(arg).outerHtml(); new Document(arg); new Document(Parser.NamespaceHtml, arg); break;
            case 9: doc.parser(Parser.xmlParser()); doc.parser(); break;
            default: doc.connection(); doc.nodeName();
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void forms(Document doc, Element extra) {
        for (FormElement f : doc.forms()) {
            f.elements();
            f.formData();
            if (extra.tag().isFormSubmittable()) f.addElement(extra);
            f.clone();
            try { f.submit(); } catch (IllegalArgumentException e) { /* needs an absolute action URL */ }
        }
        if (!doc.forms().isEmpty()) doc.forms().get(0).empty();
    }

    // ---------------------------------------------------------------- nodes
    @Endpoint(optional = true)
    public static Node child_node(Element e, @Range(lo = 0, hi = 8) int i) {
        return e.childNodeSize() > 0 ? e.childNode(i % e.childNodeSize()) : null;
    }

    @Endpoint
    public static Node element_as_node(Element e) {
        return e;
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Node new_node(@Range(lo = 0, hi = 9) int kind, @Str(max = 64, type = "Html") String text) {
        switch (kind) {
            case 0: return new TextNode(text);
            case 1: return TextNode.createFromEncoded(text);
            case 2: return new Comment(text);
            case 3: return new DataNode(text);
            case 4: return new CDataNode(text);
            case 5: return new DocumentType(text.isEmpty() ? "html" : text, "-//W3C//DTD HTML 4.01//EN", "");
            case 6: return new XmlDeclaration("xml", text.length() % 2 == 0);
            case 7: return new ProcessingInstruction("pi", text);
            case 8: return new XmlDeclaration(text.isEmpty() ? "x" : text, true);
            default: return new Element("p").text(text);
        }
    }

    @Endpoint
    public static void node_inspect(Node n) {
        n.nodeName(); n.normalName(); n.nodeValue(); n.attributes(); n.attributesSize(); n.baseUri(); n.childNodeSize();
        n.outerHtml(); n.toString(); n.sourceRange(); n.parent(); n.parentNode(); n.parentElement(); n.root(); n.ownerDocument();
        n.siblingNodes(); n.siblingIndex(); n.nextSibling(); n.previousSibling(); n.firstSibling(); n.lastSibling();
        n.nextElementSibling(); n.previousElementSibling(); n.firstChild(); n.lastChild(); n.childNodes(); n.childNodesCopy();
        n.hasParent(); n.nameIs("#text"); n.parentNameIs("p"); n.parentElementIs("p", Parser.NamespaceHtml); n.hasAttr("id"); n.attr("id"); n.absUrl("href");
        n.nodeStream().count(); n.nodeStream(Element.class).count();
        n.traverse((x, d) -> { }); n.forEachNode(Node::nodeName); n.filter((x, d) -> NodeFilter.FilterResult.CONTINUE);
        n.hasSameValue(n.clone());
        n.shallowClone();
        if (n.childNodeSize() > 0) n.childNode(0);
        if (n.hasParent()) Stitch.check(n.parentNode().childNode(n.siblingIndex()) == n, "parent.childNode(siblingIndex) is the node");
        if (n instanceof TextNode) {
            TextNode t = (TextNode) n;
            t.text(); t.getWholeText(); t.isBlank();
            if (n instanceof CDataNode) ((CDataNode) n).text();
        } else if (n instanceof Comment) {
            Comment c = (Comment) n;
            c.getData();
            if (c.isXmlDeclaration()) c.asXmlDeclaration();
        } else if (n instanceof DataNode) {
            ((DataNode) n).getWholeData();
        } else if (n instanceof DocumentType) {
            DocumentType d = (DocumentType) n;
            d.name(); d.publicId(); d.systemId();
        } else if (n instanceof XmlDeclaration) {
            XmlDeclaration x = (XmlDeclaration) n;
            x.name(); x.getWholeDeclaration();
        } else if (n instanceof ProcessingInstruction) {
            ProcessingInstruction p = (ProcessingInstruction) n;
            p.target(); p.data(); p.nodeValue(); p.attr("x"); p.attributesSize();
        }
        if (n instanceof LeafNode) {
            LeafNode l = (LeafNode) n;
            l.nodeValue(); l.attributes(); l.parent(); l.attr("#text"); l.hasAttr("x"); l.absUrl("x"); l.baseUri(); l.childNodeSize();
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void node_edit(Node n, @Range(lo = 0, hi = 14) int op, @Str(max = 64, type = "Html") String arg, @Range(lo = 0, hi = 64) int off) {
        switch (op) {
            case 0: Stitch.require(n.parent() != null, "no parent"); n.before(arg); break;
            case 1: Stitch.require(n.parent() != null, "no parent"); n.after(arg); break;
            case 2: Stitch.require(n.parent() != null, "no parent"); n.wrap(arg.isEmpty() ? "<span>" : arg); break;
            case 3: Stitch.require(n.parent() != null, "no parent"); n.unwrap(); break;
            case 4: n.attr(arg.isEmpty() ? "k" : arg, arg); n.removeAttr(arg); break;
            case 5: n.clearAttributes(); break;
            case 6: n.setBaseUri(arg); break;
            case 7: n.empty(); break;
            case 8: Stitch.require(n.parent() != null, "no parent"); n.replaceWith(new TextNode(arg)); break;
            case 9: Stitch.require(n.parent() != null, "no parent"); n.before(new Comment(arg)); n.after(new TextNode(arg)); break;
            case 10: {
                Stitch.require(n instanceof TextNode, "not text");
                TextNode t = (TextNode) n;
                String whole = t.getWholeText();
                Stitch.require(off < whole.length(), "offset past the end");
                TextNode tail = t.splitText(off);
                Stitch.check((t.getWholeText() + tail.getWholeText()).equals(whole), "splitText keeps the text");
                break;
            }
            case 11:
                if (n instanceof TextNode) ((TextNode) n).text(arg);
                else if (n instanceof Comment) ((Comment) n).setData(arg);
                else if (n instanceof DataNode) ((DataNode) n).setWholeData(arg);
                else if (n instanceof ProcessingInstruction) ((ProcessingInstruction) n).data(arg);
                break;
            case 12:
                if (n instanceof DocumentType) { ((DocumentType) n).setPubSysKey(arg.length() % 2 == 0 ? DocumentType.PUBLIC_KEY : DocumentType.SYSTEM_KEY); ((DocumentType) n).setInternalSubset(arg); }
                else if (n instanceof ProcessingInstruction) { ((ProcessingInstruction) n).attr("k", arg); ((ProcessingInstruction) n).clearAttributes(); }
                break;
            case 13: Stitch.require(n.parent() != null, "no parent"); n.remove(); Stitch.check(n.parent() == null, "remove() detaches"); break;
            default: {
                NodeIterator<Node> it = NodeIterator.from(n);
                int k = 0;
                while (it.hasNext() && k++ < 1000) {
                    Node x = it.next();
                    if (x != n && x.parent() != null && arg.length() % 3 == 0 && k == 2) it.remove();
                }
                it.restart(n);
                NodeIterator<TextNode> ti = new NodeIterator<>(n, TextNode.class);
                while (ti.hasNext()) ti.next().getWholeText();
            }
        }
    }

    /** Inserts a node into an element. */
    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void insert_node(Element parent, Node n, @Range(lo = 0, hi = 4) int op, @Range(lo = -1, hi = 6) int index) {
        Stitch.require(!ancestorOrSelf(n, parent), "would create a cycle");
        switch (op) {
            case 0: parent.appendChild(n); break;
            case 1: parent.prependChild(n); break;
            case 2: parent.insertChildren(index, n); break;
            case 3: Stitch.require(parent.parent() != null, "no parent"); ((Node) parent).before(n); break;
            default: Stitch.require(parent.parent() != null, "no parent"); ((Node) parent).after(n);
        }
    }

    // ---------------------------------------------------------------- source positions
    @Endpoint
    public static void ranges(Element e) {
        org.jsoup.nodes.Range r = e.sourceRange();
        org.jsoup.nodes.Range end = e.endSourceRange();
        for (org.jsoup.nodes.Range x : new org.jsoup.nodes.Range[] {r, end}) {
            x.start(); x.end(); x.startPos(); x.endPos(); x.isTracked(); x.isImplicit();
            for (org.jsoup.nodes.Range.Position p : new org.jsoup.nodes.Range.Position[] {x.start(), x.end()}) { p.pos(); p.lineNumber(); p.columnNumber(); p.isTracked(); }
            if (x.isTracked()) Stitch.check(x.startPos() <= x.endPos(), "a tracked range starts before it ends");
        }
        for (Attribute a : e.attributes()) {
            org.jsoup.nodes.Range.AttributeRange ar = e.attributes().sourceRange(a.getKey());
            ar.isTracked(); ar.nameRange(); ar.valueRange();
            a.sourceRange();
        }
    }

    // ---------------------------------------------------------------- attributes
    @Endpoint
    public static Attributes attributes_of(Element e) {
        return e.attributes();
    }

    @Endpoint
    public static Attributes new_attributes() {
        return new Attributes();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void attributes_op(Attributes a, @Range(lo = 0, hi = 15) int op, @Str(max = 24) String key, @Str(max = 24) String value) {
        Stitch.require(!key.startsWith("/"), "internal key");
        switch (op) {
            case 0: a.put(key, value); Stitch.check(value.equals(a.get(key)) && a.hasKey(key), "put(k, v) then get(k) == v"); break;
            case 1: a.add(key, value); break;
            case 2: a.put(key, value.length() % 2 == 0); break;
            case 3: a.put(new Attribute(key.isEmpty() ? "k" : key, value)); break;
            case 4: a.remove(key); Stitch.check(!a.hasKey(key), "remove(k) then !hasKey(k)"); break;
            case 5: a.removeIgnoreCase(key); break;
            case 6: a.get(key); a.getIgnoreCase(key); a.attribute(key); a.hasKeyIgnoreCase(key); a.hasDeclaredValueForKey(key); a.hasDeclaredValueForKeyIgnoreCase(key); break;
            case 7: a.userData(key, value); Stitch.check(value.equals(a.userData(key)), "userData read back"); break;
            case 8: { Attributes c = a.clone(); Stitch.check(c.equals(a) && c.size() == a.size(), "clone equals"); a.addAll(c); break; }
            case 9: { int n = a.size(); Stitch.check(a.asList().size() == n && a.isEmpty() == (n == 0), "size() == asList().size()"); a.html(); a.toString(); break; }
            case 10: a.dataset().put(key.isEmpty() ? "k" : key, value); a.dataset().keySet(); break;
            case 11: a.normalize(); break;
            case 12: { int n = a.size(); int d = a.deduplicate(value.isEmpty() ? ParseSettings.htmlDefault : ParseSettings.preserveCase); Stitch.check(a.size() == n - d, "deduplicate reports the removed count"); break; }
            case 13: for (Iterator<Attribute> it = a.iterator(); it.hasNext();) { Attribute x = it.next(); if (x.getKey().equals(key)) it.remove(); } break;
            case 14: a.sourceRange(key); break;
            default: a.hashCode();
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void attribute(@Str(max = 32) String key, @Str(max = 32) String value, Document.OutputSettings.Syntax syntax, Attributes parent) {
        Attribute a = new Attribute(key, value);
        a.getKey(); a.getValue(); a.hasDeclaredValue(); a.prefix(); a.localName(); a.namespace(); a.html(); a.sourceRange(); a.toString();
        Attribute c = a.clone();
        Stitch.check(c.equals(a), "clone equals");
        a.setValue(value + "x");
        a.setKey(key + "x");
        Attribute.getValidKey(key, syntax);
        Attribute.isBooleanAttribute(key);
        Attribute.createFromEncoded(key, value).getValue();
        new Attribute(key, value, parent);
    }

    // ---------------------------------------------------------------- entities, CSS identifiers, token queues
    static boolean hasSurrogate(String s) { return s.chars().anyMatch(c -> Character.isSurrogate((char) c)); }

    @Endpoint
    public static void entities(@Str(max = 64) String s, Entities.EscapeMode mode, @OneOf(strings = {"UTF-8", "US-ASCII", "ISO-8859-1"}) String charset, @OneOf(strings = {"amp", "nbsp", "notin", "NotEqualTilde", "x", "", "lt;", "AMP", "fjlig"}) String name) {
        Document.OutputSettings o = new Document.OutputSettings().escapeMode(mode).charset(charset);
        String e = Entities.escape(s, o);
        Entities.escape(s);
        // (character references to NUL, CR and C0/C1 controls are remapped by the HTML spec: excluded)
        boolean plain = s.chars().allMatch(c -> c == '\t' || c == '\n' || (c >= 0x20 && c < 0x7f) || c >= 0xa0) && !hasSurrogate(s);
        if (plain && mode != Entities.EscapeMode.xhtml) {
            Stitch.check(Entities.unescape(e).equals(s), "unescape(escape(s)) == s");
        }
        Entities.isNamedEntity(name); Entities.isBaseNamedEntity(name);
        String byName = Entities.getByName(name);
        int[] cp = new int[2];
        int n = Entities.codepointsForName(name, cp);
        if (Entities.isNamedEntity(name)) Stitch.check(n > 0 && byName.codePointAt(0) == cp[0], "getByName agrees with codepointsForName");
        Entities.findPrefix(s);
    }

    @Endpoint
    public static void css_identifier(@Str(max = 32) String s) {
        String e = Selector.escapeCssIdentifier(s);
        Stitch.check(e.equals(TokenQueue.escapeCssIdentifier(s)), "Selector and TokenQueue escape the same");
        if (!s.isEmpty() && s.indexOf('\0') < 0 && !hasSurrogate(s)) Stitch.check(Selector.unescapeCssIdentifier(e).equals(s), "unescape(escape(s)) == s");
        TokenQueue.unescape(s);
        try { Selector.unescapeCssIdentifier(s); } catch (IllegalArgumentException x) { /* not an identifier */ }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, IllegalStateException.class})
    public static void token_queue(@Str(max = 256, type = "Css") String s, @Max(64) Source src) {
        try (TokenQueue q = new TokenQueue(s)) {
            int steps = 0;
            while (!q.isEmpty() && src.more() && steps++ < 64) {
                int before = q.pos();
                switch (src.choice(22)) {
                    case 0: q.consume(); break;
                    case 1: q.advance(); break;
                    case 2: q.matches(src.pick("a", ":", "[", "(")); q.matches(':'); q.matchesAny(',', '>', '+', '~'); break;
                    case 3: q.matchChomp(src.pick(",", ">", "::")); break;
                    case 4: q.matchChomp('['); break;
                    case 5: q.matchesWhitespace(); q.matchesWord(); break;
                    case 6: if (q.matches("a")) q.consume("a"); break;
                    case 7: q.consumeTo(src.pick(")", "]", ",")); break;
                    case 8: q.consumeToAny(",", ">", " "); break;
                    case 9: q.chompBalanced('(', ')'); break;
                    case 10: q.chompBalancedRemainder('[', ']'); break;
                    case 11: q.consumeWhitespace(); break;
                    case 12: q.consumeElementSelector(); break;
                    case 13: q.consumeCssIdentifier(); break;
                    case 14: q.consumeCssValue(); break;
                    case 15: q.consumeCssValue(src.coin()); break;
                    case 16: q.toString(); break;
                    default: q.remainder();
                }
                Stitch.check(q.pos() >= before, "TokenQueue position never goes back");
            }
        }
    }

    @Endpoint
    public static void char_reader(@Str(max = 512, type = "Html") String s, @Max(64) Source src, boolean reader, @OneOf(ints = {0, 1, 16, 1024}) int bufSize) {
        CharacterReader r = !reader ? new CharacterReader(s) : bufSize == 0 ? new CharacterReader(new StringReader(s)) : new CharacterReader(new StringReader(s), bufSize);
        try {
            r.trackNewlines(src.coin());
            r.isTrackNewlines();
            int steps = 0;
            while (!r.isEmpty() && src.more() && steps++ < 64) {
                int before = r.pos();
                switch (src.choice(8)) {
                    case 0: r.consume(); break;
                    case 1: r.advance(); break;
                    case 2: r.consumeTo(src.pick('<', '&', '>')); break;
                    case 3: r.consumeTo(src.pick("-->", "]]>", "</")); break;
                    case 4: r.consumeToAny('<', '&', '\0'); break;
                    case 5: r.current(); break;
                    case 6: r.lineNumber(); r.columnNumber(); break;
                    default: r.toString();
                }
                Stitch.check(r.pos() >= before && r.pos() <= s.length(), "CharacterReader position moves forward within the input");
            }
        } finally {
            r.close();
        }
    }

    // ---------------------------------------------------------------- W3C DOM
    @Endpoint(bailsOn = {Selector.SelectorParseException.class})
    public static void w3c(Document doc, boolean nsAware, boolean html, Element context, @OneOf(strings = {"//a", "//*", "//p/text()", "//div[@id]", "/html/body", "//comment()"}) String xp) {
        W3CDom w = new W3CDom().namespaceAware(nsAware);
        Stitch.check(w.namespaceAware() == nsAware, "namespaceAware read back");
        org.w3c.dom.Document d = w.fromJsoup(doc);
        org.w3c.dom.NodeList nl = w.selectXpath(xp, d);
        w.sourceNodes(nl, Node.class);
        org.w3c.dom.Node ctx = w.contextNode(d);
        if (ctx != null) w.selectXpath(xp, ctx);
        W3CDom.convert(doc);
        if (context.ownerDocument() == doc) {
            org.w3c.dom.Document d2 = w.fromJsoup(context);
            w.selectXpath(xp, d2);
        }
        try {
            // into an empty W3C document (a document already holding an element cannot take a second root)
            org.w3c.dom.Document empty = javax.xml.parsers.DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument();
            w.convert(doc, empty);
            org.w3c.dom.Document empty2 = javax.xml.parsers.DocumentBuilderFactory.newInstance().newDocumentBuilder().newDocument();
            w.convert(context, empty2);
        } catch (javax.xml.parsers.ParserConfigurationException e) {
            throw Stitch.bail("no DOM builder");
        }
        try {
            w.asString(d);
            W3CDom.asString(d, html ? W3CDom.OutputHtml() : W3CDom.OutputXml());
        } catch (IllegalStateException e) {
            // documented: asString wraps the transformer's error, e.g. for characters XML 1.0 does not allow
            if (e.getCause() instanceof javax.xml.transform.TransformerException) throw Stitch.bail("not serializable as XML");
            throw e;
        }
    }

    // ---------------------------------------------------------------- sanitizing
    static boolean dangerous(Document out) {
        if (!out.select("script, iframe, object, embed, style, svg, math, base, form").isEmpty()) return true;
        for (Element e : out.getAllElements()) {
            for (org.jsoup.nodes.Attribute a : e.attributes()) {
                String k = a.getKey().toLowerCase(Locale.ROOT);
                if (k.startsWith("on") || k.equals("srcdoc") || k.equals("formaction")) return true;
                if (k.equals("href") || k.equals("src") || k.equals("cite") || k.equals("action")) {
                    String v = org.jsoup.parser.Parser.unescapeEntities(a.getValue(), true).replaceAll("[\\x00-\\x20]", "").toLowerCase(Locale.ROOT);
                    if (v.startsWith("javascript:") || v.startsWith("vbscript:") || v.startsWith("data:text/html")) return true;
                }
            }
        }
        return false;
    }

    @Endpoint
    public static void clean(@Str(max = 4096, type = "Html") String html, @Range(lo = 0, hi = 4) int kind, boolean relative,
                             @OneOf(strings = {"", "http://example.com/"}) String base) {
        Safelist sl = safelist(kind, relative);
        String out = base.isEmpty() ? Jsoup.clean(html, sl) : Jsoup.clean(html, base, sl);
        Document cleaned = Jsoup.parseBodyFragment(out, base);
        Stitch.check(!dangerous(cleaned), "clean() output has no script, handler or javascript: URL");
        String twice = base.isEmpty() ? Jsoup.clean(out, sl) : Jsoup.clean(out, base, sl);
        Stitch.check(twice.replaceAll("\\s+", "").equals(out.replaceAll("\\s+", "")), "clean() is idempotent (modulo pretty-print whitespace)");
        Jsoup.isValid(html, sl);
    }

    @Endpoint
    public static void cleanDocument(Document doc, @Range(lo = 0, hi = 4) int kind, boolean relative) {
        Cleaner c = new Cleaner(safelist(kind, relative));
        Document out = c.clean(doc);
        Stitch.check(!dangerous(out), "Cleaner.clean() output has no script, handler or javascript: URL");
        c.isValid(doc);
    }

    /** A custom safelist built only from harmless tags, attributes and protocols. */
    @Endpoint
    public static Safelist safelist_new(@Range(lo = 0, hi = 5) int kind) {
        return kind == 5 ? new Safelist() : new Safelist(safelist(kind, false));
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void safelist_edit(Safelist sl, @Range(lo = 0, hi = 9) int op,
                                     @OneOf(strings = {"a", "b", "p", "span", "img", "blockquote", ":all", "q"}) String tag,
                                     @OneOf(strings = {"href", "title", "class", "src", "cite", "rel", "lang"}) String attr,
                                     @OneOf(strings = {"http", "https", "mailto", "#", "ftp"}) String protocol,
                                     @OneOf(strings = {"nofollow", "x", ""}) String value, boolean relative) {
        switch (op) {
            case 0: sl.addTags(tag); Stitch.check(sl.isSafeTag(tag) || tag.equals(":all"), "addTags then isSafeTag"); break;
            case 1: sl.removeTags(tag); break;
            case 2: sl.addAttributes(tag, attr); break;
            case 3: sl.removeAttributes(tag, attr); break;
            case 4: sl.addEnforcedAttribute(tag, attr, value); sl.getEnforcedAttributes(tag); break;
            case 5: sl.removeEnforcedAttribute(tag, attr); break;
            case 6: sl.addProtocols(tag, attr, protocol); break;
            case 7: sl.removeProtocols(tag, attr, protocol); break;
            case 8: sl.preserveRelativeLinks(relative); Stitch.check(sl.preserveRelativeLinks() == relative, "preserveRelativeLinks read back"); break;
            default: { Element el = new Element(tag.equals(":all") ? "p" : tag); Attribute a = new Attribute(attr, protocol + "://x/"); el.attributes().put(a); sl.isSafeAttribute(el.tagName(), el, a); }
        }
    }

    @Endpoint
    public static void clean_custom(Safelist sl, @Str(max = 2048, type = "Html") String html, @OneOf(strings = {"", "http://example.com/"}) String base, boolean prettyPrint) {
        Document.OutputSettings o = new Document.OutputSettings().prettyPrint(prettyPrint);
        String out = Jsoup.clean(html, base, sl, o);
        // only tags and attributes: a custom safelist without protocols for an URL attribute allows any URL
        Stitch.check(Jsoup.parseBodyFragment(out, base).select("script, iframe, object, embed, style, svg, math, base, form, [^on]").isEmpty(), "clean() with a custom safelist has no script or event handler");
        Cleaner c = new Cleaner(sl);
        c.isValidBodyHtml(html);
        c.isValid(Jsoup.parse(html));
    }
}
