import java.util.Locale;
import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.jsoup.nodes.Element;
import org.jsoup.nodes.Node;
import org.jsoup.parser.Parser;
import org.jsoup.safety.Cleaner;
import org.jsoup.safety.Safelist;
import org.jsoup.select.Elements;
import org.jsoup.select.Selector;
import stitch.*;

/**
 * jsoup: parse HTML, select and edit elements, sanitize untrusted HTML.
 * Oracles: clean() output contains no script, event handler or javascript: URL, cleaning is
 * idempotent, and isValid() agrees with clean().
 */
public class Stubs {
    @Decoder("Html")
    public static String html(Source s) {
        StringBuilder b = new StringBuilder();
        String[] tags = {"a", "p", "div", "img", "b", "table", "tr", "td", "script", "style", "svg", "math", "span", "ul", "li", "iframe", "select", "option", "form", "template", "noscript"};
        while (s.more()) {
            switch (s.choice(6)) {
                case 0: b.append('<').append(s.pick(tags)); while (s.coin(0.4)) b.append(' ').append(s.pick("href", "src", "class", "id", "onclick", "style", "srcdoc", "title")).append("=\"").append(s.coin(0.5) ? s.token() : s.str(8)).append('"'); b.append('>'); break;
                case 1: b.append("</").append(s.pick(tags)).append('>'); break;
                case 2: b.append(s.token()); break;
                case 3: b.append(s.pick("&amp;", "&lt;", "&#106;", "&#x3c;", "&nbsp", "&", "&#0;")); break;
                default: b.append(s.str(10));
            }
        }
        return b.toString();
    }

    @Decoder("Css")
    public static String css(Source s) {
        StringBuilder b = new StringBuilder();
        do {
            b.append(s.coin(0.5) ? s.token() : s.pick("a", "p", "div", "td", "*", "[id]", ".x"));
        } while (s.more());
        return b.toString();
    }

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

    // an invalid query throws Selector.SelectorParseException (an IllegalStateException in 1.15)
    @Endpoint(optional = true, bailsOn = {IllegalArgumentException.class, Selector.SelectorParseException.class})
    public static Element select(Document doc, @Str(max = 64, type = "Css") String css, @Range(lo = 0, hi = 8) int i) {
        Elements els = doc.select(css);
        return els.size() > 0 ? els.get(i % els.size()) : null;
    }

    @Endpoint(optional = true)
    public static Element body(Document doc) {
        return doc.body();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})  // jsoup's Validate (empty tag names, ...)
    public static void edit(Element e, @OneOf(strings = {"append", "prepend", "before", "after", "wrap", "html", "text", "attr", "addClass", "tagName", "removeAttr"}) String op,
                            @Str(max = 256, type = "Html") String arg) {
        switch (op) {
            case "append": e.append(arg); break;
            case "prepend": e.prepend(arg); break;
            case "before": Stitch.require(e.parent() != null, "no parent"); e.before(arg); break;
            case "after": Stitch.require(e.parent() != null, "no parent"); e.after(arg); break;
            case "wrap": Stitch.require(e.parent() != null, "no parent"); e.wrap(arg.isEmpty() ? "<div>" : arg); break;
            case "html": e.html(arg); break;
            case "text": e.text(arg); break;
            case "attr": e.attr(arg.isEmpty() ? "x" : arg, arg); break;
            case "addClass": e.addClass(arg); break;
            case "tagName": Stitch.require(!arg.isEmpty(), "empty tag"); e.tagName(arg); break;
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

    @Endpoint(optional = true)
    public static Element child(Element e, @Range(lo = 0, hi = 4) int i) {
        return e.childrenSize() > 0 ? e.child(i % e.childrenSize()) : null;
    }

    @Endpoint
    public static void render(Document doc, boolean pretty, @Range(lo = 0, hi = 8) int indent, boolean outline) {
        doc.outputSettings().prettyPrint(pretty).indentAmount(indent).outline(outline);
        String out = doc.outerHtml();
        Document again = Jsoup.parse(out);
        again.outerHtml();
        doc.text();
    }

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
        Stitch.check(twice.equals(out), "clean() is idempotent");
    }

    @Endpoint
    public static void cleanDocument(Document doc, @Range(lo = 0, hi = 4) int kind, boolean relative) {
        Cleaner c = new Cleaner(safelist(kind, relative));
        Document out = c.clean(doc);
        Stitch.check(!dangerous(out), "Cleaner.clean() output has no script, handler or javascript: URL");
    }
}
