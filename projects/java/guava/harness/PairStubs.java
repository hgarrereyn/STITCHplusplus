import com.google.common.base.*;
import com.google.common.collect.*;
import java.util.*;
import stitch.Endpoint;
import stitch.Stitch;
import stitch.Str;

/** Guava string utilities on pairs of strings. */
public class PairStubs {

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_common_prefix(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        String p = Strings.commonPrefix(a, b); Stitch.check(a.startsWith(p) && b.startsWith(p), "prefix of both"); Stitch.check(p.length() == Math.min(a.length(), b.length()) || p.length() >= 0 && (p.length() >= a.length() || p.length() >= b.length() || a.charAt(p.length()) != b.charAt(p.length()) || Character.isLowSurrogate(a.charAt(p.length()))), "longest common prefix");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_common_suffix(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        String q = Strings.commonSuffix(a, b); Stitch.check(a.endsWith(q) && b.endsWith(q), "suffix of both"); Stitch.check(q.isEmpty() || !Character.isLowSurrogate(q.charAt(0)) || q.length() == Math.min(a.length(), b.length()), "does not split a surrogate pair");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_splitter_limit(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        int n = 1 + (b.length() % 4); List<String> parts = Splitter.on(",").limit(n).splitToList(a); Stitch.check(parts.size() <= n, "limit"); Stitch.check(String.join(",", parts).equals(a), "limited parts join back");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_splitter_trim_omit(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        List<String> parts = Splitter.on(b.isEmpty() ? "," : b.substring(0, 1)).trimResults().omitEmptyStrings().splitToList(a); for (String p : parts) Stitch.check(!p.isEmpty() && p.equals(CharMatcher.whitespace().trimFrom(p)), "trimmed, non-empty parts");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_joiner_map_joiner(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        Map<String, String> m = new LinkedHashMap<>(); m.put(a, b); m.put(b, a); String j = Joiner.on("&").withKeyValueSeparator("=").join(m); Stitch.check(j.contains(a) && j.contains(b), "map joiner");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_ascii_truncate(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        int n = 3 + b.length() % 20; String t = Ascii.truncate(a, n, "..."); Stitch.check(t.length() <= n && (a.length() <= n ? t.equals(a) : t.endsWith("...")), "truncate");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_ascii_equals_ignore_case(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        Stitch.check(Ascii.equalsIgnoreCase(a, b) == Ascii.toLowerCase(a).equals(Ascii.toLowerCase(b)), "equalsIgnoreCase");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_strings_pad(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        int n = b.length() * 2; String s = Strings.padStart(a, n, '0'); Stitch.check(s.endsWith(a) && s.length() == Math.max(n, a.length()), "padStart"); String e = Strings.padEnd(a, n, '.'); Stitch.check(e.startsWith(a) && e.length() == Math.max(n, a.length()), "padEnd");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_strings_repeat(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        int n = b.length() % 6; String r = Strings.repeat(a, n); Stitch.check(r.length() == a.length() * n && (n == 0 || r.startsWith(a)), "repeat");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_case_format_all(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        for (CaseFormat f : CaseFormat.values()) for (CaseFormat t : CaseFormat.values()) { String x = f.to(t, a); Stitch.check(x != null, "to"); Stitch.check(f.converterTo(t).convert(a).equals(x), "converterTo agrees"); }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_char_matcher_anyof_pair(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        CharMatcher m = CharMatcher.anyOf(b); for (int i = 0; i < a.length(); i++) Stitch.check(m.matches(a.charAt(i)) == (b.indexOf(a.charAt(i)) >= 0), "anyOf matches exactly its chars"); Stitch.check(m.removeFrom(a).length() + m.countIn(a) == a.length(), "removeFrom / countIn");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_char_matcher_replace(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        CharMatcher m = CharMatcher.anyOf(b); String r = m.replaceFrom(a, "[]"); Stitch.check(r.length() == a.length() + m.countIn(a), "replaceFrom with a 2-char string adds one char per match");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_utf8_encoded_length_pair(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        String s = a + b; try { Stitch.check(Utf8.encodedLength(s) == Utf8.encodedLength(a) + Utf8.encodedLength(b) || Character.isHighSurrogate(a.isEmpty() ? 'x' : a.charAt(a.length() - 1)), "encodedLength is additive"); } catch (IllegalArgumentException e) { Stitch.bail("unpaired surrogate"); }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_hashing_strings_agree(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        com.google.common.hash.HashFunction f = com.google.common.hash.Hashing.murmur3_128(); Stitch.check(f.hashString(a + b, java.nio.charset.StandardCharsets.UTF_8).equals(f.newHasher().putString(a, java.nio.charset.StandardCharsets.UTF_8).putString(b, java.nio.charset.StandardCharsets.UTF_8).hash()) || a.isEmpty() || b.isEmpty() || Character.isHighSurrogate(a.charAt(a.length() - 1)), "putString twice == hashString of the concatenation");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_ordering_explicit(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        List<Character> order = new ArrayList<>(new LinkedHashSet<>(Lists.charactersOf(b))); Stitch.require(!order.isEmpty(), "empty order"); Ordering<Character> o = Ordering.explicit(order); List<Character> xs = new ArrayList<>(); for (char c : a.toCharArray()) if (order.contains(c)) xs.add(c); List<Character> sorted = o.sortedCopy(xs); for (int i = 1; i < sorted.size(); i++) Stitch.check(order.indexOf(sorted.get(i - 1)) <= order.indexOf(sorted.get(i)), "explicit ordering");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void pair_internet_domain_valid(@Str(max = 48, type = "Text") String a, @Str(max = 16, type = "Text") String b) {
        Stitch.check(com.google.common.net.InternetDomainName.isValid(a) == com.google.common.net.InternetDomainName.isValid(a.toLowerCase(java.util.Locale.ROOT)) || !a.chars().allMatch(c -> c < 128), "validity does not depend on ASCII case");
    }
}
