import java.math.BigDecimal;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.math.NumberUtils;
import org.apache.commons.lang3.text.StrBuilder;
import stitch.*;

/**
 * commons-lang3: StrBuilder (a mutable string builder, mirrored on java.lang.StringBuilder),
 * NumberUtils and StringUtils.
 */
@SuppressWarnings("deprecation")
public class Stubs {
    @Decoder("NumText")
    public static String num(Source s) {
        StringBuilder b = new StringBuilder();
        do {
            b.append(s.coin(0.5) ? s.token() : String.valueOf(s.pick("1", "2", "9", "0", "7f", "a", "3.5")));
        } while (s.more());
        return b.toString();
    }

    static StringBuilder model(StrBuilder b) {
        StringBuilder m = Stitch.getObj(b, "model", StringBuilder.class);
        Stitch.require(m != null, "no model");
        return m;
    }

    static void same(StrBuilder b, String what) {
        Stitch.check(b.toString().equals(model(b).toString()), "StrBuilder == model after " + what);
        Stitch.check(b.length() == model(b).length(), "length() after " + what);
    }

    @Endpoint
    public static StrBuilder newBuilder(@Str(max = 32) String init) {
        StrBuilder b = new StrBuilder(init);
        Stitch.set(b, "model", new StringBuilder(init));
        return b;
    }

    @Endpoint
    public static void append(StrBuilder b, @Str(max = 32) String s, @Range(lo = 0, hi = 5) int how, int n) {
        StringBuilder m = model(b);
        switch (how) {
            case 0: b.append(s); m.append(s); break;
            case 1: b.appendln(s); m.append(s).append(System.lineSeparator()); break;
            case 2: b.append(n); m.append(n); break;
            case 3: b.appendPadding(n % 50, '*'); for (int i = 0; i < n % 50; i++) m.append('*'); break;
            case 4: b.appendFixedWidthPadLeft(s, Math.abs(n % 20), '.'); {
                int w = Math.abs(n % 20);
                String t = s.length() >= w ? s.substring(s.length() - w) : ".".repeat(w - s.length()) + s;
                m.append(w == 0 ? "" : t);
            } break;
            default: b.appendSeparator(','); if (m.length() > 0) m.append(','); break;
        }
        same(b, "append#" + how);
    }

    @Endpoint(bailsOn = {IndexOutOfBoundsException.class})
    public static void insert(StrBuilder b, @Range(lo = -2, hi = 64) int at, @Str(max = 16) String s) {
        StringBuilder m = model(b);
        Stitch.require(at >= 0 && at <= m.length(), "index out of range (documented)");
        b.insert(at, s);
        m.insert(at, s);
        same(b, "insert");
    }

    @Endpoint(bailsOn = {IndexOutOfBoundsException.class})
    public static void delete(StrBuilder b, @Range(lo = 0, hi = 64) int start, @Range(lo = 0, hi = 64) int end) {
        StringBuilder m = model(b);
        Stitch.require(start <= m.length() && start <= end, "range out of bounds (documented)");
        b.delete(start, end);
        m.delete(start, Math.min(end, m.length()));
        same(b, "delete");
    }

    @Endpoint
    public static void replaceAll(StrBuilder b, @Str(max = 8) String from, @Str(max = 8) String to) {
        StringBuilder m = model(b);
        b.replaceAll(from, to);
        String r = from.isEmpty() ? m.toString() : m.toString().replace(from, to);
        m.setLength(0);
        m.append(r);
        same(b, "replaceAll");
    }

    @Endpoint
    public static void deleteAll(StrBuilder b, @Str(max = 8) String s) {
        StringBuilder m = model(b);
        b.deleteAll(s);
        String r = s.isEmpty() ? m.toString() : m.toString().replace(s, "");
        m.setLength(0);
        m.append(r);
        same(b, "deleteAll");
    }

    @Endpoint
    public static void trimReverse(StrBuilder b, @OneOf(strings = {"trim", "reverse", "toUpper", "clear"}) String op) {
        StringBuilder m = model(b);
        String cur = m.toString();
        switch (op) {
            case "trim": b.trim(); cur = StringUtils.strip(cur, null).isEmpty() ? "" : trim(cur); break;
            case "reverse": { // StrBuilder reverses chars (StringBuilder keeps surrogate pairs together)
                b.reverse();
                char[] c = cur.toCharArray();
                for (int i = 0, j = c.length - 1; i < j; i++, j--) { char t = c[i]; c[i] = c[j]; c[j] = t; }
                cur = new String(c);
                break;
            }
            case "toUpper": b.replaceAll("a", "A"); cur = cur.replace("a", "A"); break;
            default: b.clear(); cur = "";
        }
        m.setLength(0);
        m.append(cur);
        same(b, op);
    }

    static String trim(String s) {
        int st = 0, en = s.length();
        while (st < en && s.charAt(st) <= ' ') st++;
        while (en > st && s.charAt(en - 1) <= ' ') en--;
        return s.substring(st, en);
    }

    @Endpoint
    public static void search(StrBuilder b, @Str(max = 8) String s, @Range(lo = -1, hi = 40) int from) {
        String m = model(b).toString();
        Stitch.check(b.indexOf(s, from) == (from < 0 ? m.indexOf(s) : m.indexOf(s, from)) || s.isEmpty(), "indexOf");
        Stitch.check(b.lastIndexOf(s) == m.lastIndexOf(s) || s.isEmpty(), "lastIndexOf");
        Stitch.check(b.contains(s) == m.contains(s), "contains");
    }

    @Endpoint(bailsOn = {IndexOutOfBoundsException.class})
    public static void setLength(StrBuilder b, @Range(lo = -1, hi = 64) int n) {
        Stitch.require(n >= 0, "negative (documented)");
        StringBuilder m = model(b);
        b.setLength(n);
        m.setLength(n);
        same(b, "setLength");
    }

    @Endpoint(bailsOn = {NumberFormatException.class})
    public static void createNumber(@Str(max = 48, type = "NumText") String text) {
        Number n = NumberUtils.createNumber(text);
        Stitch.require(n != null, "null for null input");
        Stitch.check(NumberUtils.isCreatable(text), "isCreatable() true for what createNumber() parses");
    }

    @Endpoint
    public static void isCreatable(@Str(max = 48, type = "NumText") String text) {
        if (NumberUtils.isCreatable(text)) {
            try {
                NumberUtils.createNumber(text);
            } catch (NumberFormatException e) {
                Stitch.check(false, "createNumber() parses what isCreatable() accepts");
            }
        }
        NumberUtils.isParsable(text);
        NumberUtils.toDouble(text, 0.0);
        NumberUtils.toInt(text, 0);
    }

    @Endpoint
    public static void abbreviate(@Str(max = 64) String s, @Range(lo = 0, hi = 40) int offset, @Range(lo = -1, hi = 40) int width,
                                  @OneOf(strings = {"...", "", "…", "<>"}) String marker) {
        String r;
        try {
            r = StringUtils.abbreviate(s, marker, offset, width);
        } catch (IllegalArgumentException e) {
            Stitch.bail("width too small (documented)");
            return;
        }
        Stitch.check(r.length() <= Math.max(width, s.length()) || r.equals(s), "abbreviate() result is at most maxWidth");
    }

    @Endpoint
    public static void wrapping(@Str(max = 64) String s, @Range(lo = -1, hi = 30) int width, boolean longWords) {
        String r = org.apache.commons.lang3.text.WordUtils.wrap(s, width, "\n", longWords);
        Stitch.check(r.replaceAll("\\s", "").equals(s.replaceAll("\\s", "")), "wrap() keeps every non-space character");
    }

    @Endpoint
    public static void splitJoin(@Str(max = 48) String s, @OneOf(strings = {",", " ", "ab", ""}) String sep) {
        String[] parts = StringUtils.splitByWholeSeparatorPreserveAllTokens(s, sep.isEmpty() ? null : sep);
        if (!sep.isEmpty() && parts != null) Stitch.check(StringUtils.join(parts, sep).equals(s), "split then join gives the input");
        StringUtils.difference(s, sep);
        StringUtils.getLevenshteinDistance(s, sep);
        StringUtils.capitalize(s);
        StringUtils.swapCase(s);
    }
}
