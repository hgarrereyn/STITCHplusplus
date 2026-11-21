import com.google.common.base.*;
import com.google.common.escape.Escaper;
import com.google.common.html.HtmlEscapers;
import com.google.common.net.UrlEscapers;
import com.google.common.xml.XmlEscapers;
import java.nio.charset.StandardCharsets;
import java.util.*;
import stitch.*;

/**
 * Guava 31: com.google.common.base, escapers. Oracles compare against the JDK or a direct
 * definition; IllegalArgumentException (documented argument checks) bails, any other exception is
 * a bug.
 */
public class BaseStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    @Decoder("Text")
    public static String text(Source s) {
        StringBuilder b = new StringBuilder();
        while (s.more()) {
            switch (s.choice(6)) {
                case 0: b.append(s.str(8)); break;
                case 1: b.append(s.pick("a", "B", " ", ",", "\t", "\n", "=", ";", "-", "_", "::")); break;
                case 2: b.append(s.pick("é", "中", "😀", "\ud83d", "\ude00", "\u0000", "​", " ")); break;
                case 3: b.append(s.pick("fooBar", "FOO_BAR", "foo-bar", "foo_bar", "FooBar", "x1y2", "URL", "a_b_c")); break;
                case 4: b.append(s.token()); break;
                default: b.append(s.pick("<", ">", "&", "\"", "'", "%", "+", "/", "?", "#"));
            }
        }
        return b.toString();
    }

    @Endpoint
    public static void strings_ops(@Str(max = 64, type = "Text") String a, @Str(max = 64, type = "Text") String b, @stitch.Range(lo = 0, hi = 20) int n) {
        String p = Strings.commonPrefix(a, b), q = Strings.commonSuffix(a, b);
        Stitch.check(a.startsWith(p) && b.startsWith(p), "commonPrefix is a prefix of both");
        Stitch.check(a.endsWith(q) && b.endsWith(q), "commonSuffix is a suffix of both");
        Stitch.check(p.isEmpty() || !Character.isHighSurrogate(p.charAt(p.length() - 1)) || p.length() == Math.min(a.length(), b.length()), "commonPrefix does not split a surrogate pair");
        Stitch.check(Strings.padStart(a, n, '*').length() == Math.max(n, a.length()), "padStart length");
        Stitch.check(Strings.padEnd(a, n, '*').endsWith(a.isEmpty() ? "" : a.substring(0, Math.min(a.length(), 0))) && Strings.padEnd(a, n, '*').startsWith(a), "padEnd keeps the string");
        Stitch.check(Strings.repeat(a, n % 5).length() == a.length() * (n % 5), "repeat length");
        Stitch.check(Strings.isNullOrEmpty(Strings.emptyToNull(a)) == a.isEmpty(), "emptyToNull");
        Strings.lenientFormat("%s and %s", a, b);
    }

    @Endpoint
    public static void ascii_ops(@Str(max = 64, type = "Text") String a, @stitch.Range(lo = 0, hi = 40) int n) {
        Stitch.check(Ascii.toLowerCase(a).length() == a.length(), "toLowerCase keeps length");
        Stitch.check(Ascii.equalsIgnoreCase(Ascii.toUpperCase(a), a), "equalsIgnoreCase(toUpperCase(a), a)");
        String t = Ascii.truncate(a, Math.max(n, 3), "...");
        Stitch.check(t.length() <= Math.max(n, 3), "truncate respects maxLength");
    }

    @Endpoint
    public static void case_format(@Str(max = 32, type = "Text") String a, @OneOf(ints = {0, 1, 2, 3, 4}) int from, @OneOf(ints = {0, 1, 2, 3, 4}) int to) {
        CaseFormat[] f = CaseFormat.values();
        String out = f[from].to(f[to], a);
        Converter<String, String> c = f[from].converterTo(f[to]);
        Stitch.check(out.equals(c.convert(a)), "to() and converterTo() agree");
        if (a.matches("[a-z]+(_[a-z]+)*") && from == CaseFormat.LOWER_UNDERSCORE.ordinal()) {
            Stitch.check(f[to].to(CaseFormat.LOWER_UNDERSCORE, out).equals(a), "LOWER_UNDERSCORE round trip");
        }
    }

    @Endpoint
    public static void char_matcher(@Str(max = 64, type = "Text") String a, @stitch.Range(lo = 0, hi = 9) int which) {
        CharMatcher[] ms = {CharMatcher.whitespace(), CharMatcher.breakingWhitespace(), CharMatcher.ascii(), CharMatcher.digit(), CharMatcher.javaLetterOrDigit(),
            CharMatcher.anyOf("ab,"), CharMatcher.inRange('a', 'f'), CharMatcher.javaIsoControl(), CharMatcher.invisible(), CharMatcher.singleWidth()};
        CharMatcher m = ms[which];
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length(), "countIn == retainFrom length");
        Stitch.check(m.matchesAllOf(k) && m.negate().matchesAllOf(r), "retained chars match, removed chars do not");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom == trimLeading + trimTrailing");
        m.collapseFrom(a, '-');
        m.trimAndCollapseFrom(a, ' ');
        m.replaceFrom(a, "<>");
        Stitch.check(m.indexIn(a) == (k.isEmpty() ? -1 : a.indexOf(k.charAt(0))) || m.indexIn(a) <= a.indexOf(k.isEmpty() ? '￿' : k.charAt(0)), "indexIn finds the first match");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed matcher agrees");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void splitter(@Str(max = 64, type = "Text") String a, @OneOf(strings = {",", " ", "::", "a", ""}) String sep, boolean trim, boolean omit, @stitch.Range(lo = 0, hi = 5) int limit) {
        Splitter s = sep.isEmpty() ? Splitter.fixedLength(3) : Splitter.on(sep);
        if (trim) s = s.trimResults();
        if (omit) s = s.omitEmptyStrings();
        if (limit > 0) s = s.limit(limit);
        List<String> parts = s.splitToList(a);
        if (!trim && !omit && limit == 0 && !sep.isEmpty()) {
            Stitch.check(Joiner.on(sep).join(parts).equals(a), "join(split(a)) == a");
            Stitch.check(parts.size() == a.split(java.util.regex.Pattern.quote(sep), -1).length, "split count matches String.split(-1)");
        }
        if (!sep.isEmpty()) Splitter.onPattern(java.util.regex.Pattern.quote(sep)).splitToList(a);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void map_splitter(@Str(max = 64, type = "Text") String a) {
        Map<String, String> m = Splitter.on(';').withKeyValueSeparator('=').split(a);
        String back = Joiner.on(';').withKeyValueSeparator('=').join(m);
        Stitch.check(Splitter.on(';').withKeyValueSeparator('=').split(back).equals(m), "map split(join(m)) == m");
    }

    @Endpoint
    public static void joiner(@Str(max = 16) String a, @Str(max = 16) String b, boolean skipNulls) {
        List<String> xs = Arrays.asList(a, null, b);
        String j = skipNulls ? Joiner.on("|").skipNulls().join(xs) : Joiner.on("|").useForNull("null").join(xs);
        Stitch.check(j.contains(a) && j.contains(b), "join contains its parts");
    }

    @Endpoint
    public static void utf8(@Str(max = 64, type = "Text") String a) {
        Stitch.check(Utf8.isWellFormed(a.getBytes(StandardCharsets.UTF_8)), "JDK UTF-8 output is well formed");
        boolean lone = false;
        for (int i = 0; i < a.length(); i++) {
            char c = a.charAt(i);
            if (Character.isHighSurrogate(c) && (i + 1 >= a.length() || !Character.isLowSurrogate(a.charAt(i + 1)))) lone = true;
            if (Character.isLowSurrogate(c) && (i == 0 || !Character.isHighSurrogate(a.charAt(i - 1)))) lone = true;
        }
        try {
            int n = Utf8.encodedLength(a);
            Stitch.check(!lone && n == a.getBytes(StandardCharsets.UTF_8).length, "encodedLength == UTF-8 byte count");
        } catch (IllegalArgumentException e) {
            Stitch.check(lone, "encodedLength rejects only unpaired surrogates");
        }
    }

    @Endpoint
    public static void utf8_bytes(@Bytes(max = 32) byte[] b) {
        boolean jdk;
        try {
            StandardCharsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(b));
            jdk = true;
        } catch (java.nio.charset.CharacterCodingException e) {
            jdk = false;
        }
        Stitch.check(Utf8.isWellFormed(b) == jdk, "Utf8.isWellFormed agrees with the JDK decoder");
    }

    @Endpoint
    public static void escapers(@Str(max = 64, type = "Text") String a) {
        String h = HtmlEscapers.htmlEscaper().escape(a);
        Stitch.check(!h.matches("(?s).*[<>\"'].*") && !h.matches("(?s).*&(?!amp;|lt;|gt;|quot;|#39;).*"), "HTML escaping leaves no markup characters");
        String x = XmlEscapers.xmlAttributeEscaper().escape(a);
        Stitch.check(!x.matches("(?s).*[<>\"'\\t\\n\\r].*"), "XML attribute escaping");
        XmlEscapers.xmlContentEscaper().escape(a);
        for (Escaper e : new Escaper[] {UrlEscapers.urlFormParameterEscaper(), UrlEscapers.urlPathSegmentEscaper(), UrlEscapers.urlFragmentEscaper()}) {
            String u;
            try {
                u = e.escape(a);
            } catch (IllegalArgumentException ex) {
                continue;  // (unpaired surrogates)
            }
            Stitch.check(u.chars().allMatch(c -> c < 128), "URL escapers give ASCII");
            if (e == UrlEscapers.urlFormParameterEscaper()) {
                try {
                    Stitch.check(java.net.URLDecoder.decode(u, "UTF-8").equals(a), "form-parameter escaping decodes back");
                } catch (java.io.UnsupportedEncodingException ignored) {
                }
            }
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void optional_converters(@Str(max = 16) String a) {
        com.google.common.base.Optional<String> o = com.google.common.base.Optional.fromNullable(a.isEmpty() ? null : a);
        Stitch.check(o.isPresent() == !a.isEmpty(), "fromNullable");
        Stitch.check(o.toJavaUtil().isPresent() == o.isPresent(), "toJavaUtil");
        Converter<String, Integer> c = com.google.common.primitives.Ints.stringConverter();
        try {
            Stitch.check(c.reverse().convert(c.convert(a)).equals(String.valueOf(Integer.decode(a))), "stringConverter round trip");
        } catch (NumberFormatException e) {
            Stitch.bail("not a number");
        }
        Enums.getIfPresent(CaseFormat.class, a);
        Stitch.check(com.google.common.base.Objects.equal(MoreObjects.firstNonNull(o.orNull(), "x"), a.isEmpty() ? "x" : a), "firstNonNull");
        MoreObjects.toStringHelper("T").add("a", a).omitNullValues().toString();
        Throwables.getStackTraceAsString(new RuntimeException(a));
        Stopwatch.createUnstarted().toString();
        Suppliers.memoize(() -> a).get();
    }
}
