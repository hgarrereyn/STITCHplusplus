import com.google.common.base.*;
import com.google.common.collect.*;
import com.google.common.escape.Escaper;
import java.util.*;
import stitch.Endpoint;
import stitch.Stitch;
import stitch.Str;

/** More Guava APIs: one CharMatcher per stub, path simplification, equivalences, predicates, converters, preconditions,
 * escaper builders, FluentIterable, immutable multisets / multimaps / tables, enum collections, futures, reflection. */
public class MoreStubs {

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_whitespace(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.whitespace();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_breaking_whitespace(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.breakingWhitespace();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_ascii(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.ascii();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_digit(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.digit();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_letter_or_digit(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.javaLetterOrDigit();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_any_of(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.anyOf("ab,\ud83d");
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_in_range(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.inRange('a', 'f').or(CharMatcher.is('_'));
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_iso_control(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.javaIsoControl();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_invisible(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.invisible();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_single_width(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.singleWidth();
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_none_of(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.noneOf("aeiou").and(CharMatcher.javaLowerCase());
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void char_matcher_for_predicate(@Str(max = 64, type = "Text") String a) {
        CharMatcher m = CharMatcher.forPredicate(Character::isUpperCase);
        String r = m.removeFrom(a), k = m.retainFrom(a);
        Stitch.check(r.length() + k.length() == a.length(), "removeFrom + retainFrom partition the string");
        Stitch.check(m.countIn(a) == k.length() && m.matchesAllOf(k) && m.negate().matchesAllOf(r), "count / matches");
        Stitch.check(m.trimFrom(a).equals(m.trimTrailingFrom(m.trimLeadingFrom(a))), "trimFrom");
        Stitch.check(m.precomputed().removeFrom(a).equals(r), "precomputed agrees");
        Stitch.check(m.lastIndexIn(a) == -1 || m.matches(a.charAt(m.lastIndexIn(a))), "lastIndexIn");
        m.collapseFrom(a, '_'); m.trimAndCollapseFrom(a, ' '); m.replaceFrom(a, '.'); m.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void strings_simplify_path(@Str(max = 64, type = "Text") String a) {
        String p = com.google.common.io.Files.simplifyPath(a);
        Stitch.check(!p.isEmpty(), "simplifyPath never returns an empty string");
        Stitch.check(com.google.common.io.Files.simplifyPath(p).equals(p), "simplifyPath is idempotent (" + p + ")");
        Stitch.check(!p.contains("//") && !(p.length() > 1 && p.endsWith("/")), "no empty segments or trailing slash");
        Stitch.check(p.startsWith("/") == a.startsWith("/") || p.equals("."), "absolute stays absolute");
        com.google.common.io.Files.getFileExtension(a); com.google.common.io.Files.getNameWithoutExtension(a);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void splitter_char_matcher(@Str(max = 64, type = "Text") String a) {
        List<String> parts = Splitter.on(CharMatcher.anyOf(",;")).trimResults(CharMatcher.whitespace()).splitToList(a);
        Stitch.check(parts.size() == CharMatcher.anyOf(",;").countIn(a) + 1, "one more part than separators");
        Splitter.on(java.util.regex.Pattern.compile("\\s+")).omitEmptyStrings().splitToStream(a).count();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void splitter_fixed_length(@Str(max = 64, type = "Text") String a) {
        int n = 1 + Math.abs(a.hashCode() % 5);
        List<String> parts = Splitter.fixedLength(n).splitToList(a);
        Stitch.check(String.join("", parts).equals(a), "fixed-length parts concatenate back");
        for (int i = 0; i + 1 < parts.size(); i++) Stitch.check(parts.get(i).length() == n, "every part but the last has the length");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void equivalence_ops(@Str(max = 64, type = "Text") String a) {
        Equivalence<Object> eq = Equivalence.equals();
        Stitch.check(eq.equivalent(a, new String(a)) && eq.hash(a) == a.hashCode(), "equals equivalence");
        Stitch.check(!Equivalence.identity().equivalent(a, new String(a)) || a.isEmpty(), "identity equivalence");
        Stitch.check(Equivalence.equals().<String>pairwise().equivalent(Arrays.asList(a, a), Arrays.asList(a, a)), "pairwise");
        Stitch.check(eq.wrap(a).equals(eq.wrap(new String(a))), "wrapper equality");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void predicates_functions(@Str(max = 64, type = "Text") String a) {
        Predicate<CharSequence> p = Predicates.containsPattern("[a-c]+");
        Stitch.check(p.apply(a) == java.util.regex.Pattern.compile("[a-c]+").matcher(a).find(), "containsPattern");
        Stitch.check(Predicates.and(Predicates.notNull(), Predicates.equalTo(a)).apply(a), "and / equalTo");
        Stitch.check(Predicates.in(Arrays.asList(a)).apply(a) && !Predicates.not(Predicates.alwaysTrue()).apply(a), "in / not");
        Stitch.check(Functions.forMap(java.util.Collections.singletonMap(a, 1), 0).apply(a) == 1, "forMap");
        Stitch.check(Functions.compose(String::length, Functions.<String>identity()).apply(a) == a.length(), "compose");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void converter_compose(@Str(max = 64, type = "Text") String a) {
        Converter<String, String> c = CaseFormat.LOWER_CAMEL.converterTo(CaseFormat.UPPER_UNDERSCORE).andThen(CaseFormat.UPPER_UNDERSCORE.converterTo(CaseFormat.LOWER_CAMEL));
        if (a.matches("[a-z]+([A-Z][a-z]+)*")) Stitch.check(c.convert(a).equals(a), "LOWER_CAMEL -> UPPER_UNDERSCORE -> LOWER_CAMEL");
        Stitch.check(c.reverse().reverse() == c || c.reverse().reverse().equals(c), "reverse twice");
        Iterables.size(c.convertAll(Arrays.asList(a, a)));
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void enums_string_converter(@Str(max = 64, type = "Text") String a) {
        Converter<String, CaseFormat> c = Enums.stringConverter(CaseFormat.class);
        try { CaseFormat f = c.convert(a); Stitch.check(c.reverse().convert(f).equals(a), "stringConverter round trip"); } catch (IllegalArgumentException e) { Stitch.check(!Enums.getIfPresent(CaseFormat.class, a).isPresent(), "converter and getIfPresent agree"); }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void preconditions_indexes(@Str(max = 64, type = "Text") String a) {
        int n = a.length(), i = a.hashCode() % 12, j = (a.hashCode() >> 8) % 12;
        try { Preconditions.checkElementIndex(i, n); Stitch.check(i >= 0 && i < n, "checkElementIndex accepts [0, size)"); } catch (IndexOutOfBoundsException e) { Stitch.check(i < 0 || i >= n, "checkElementIndex rejects out of range"); }
        try { Preconditions.checkPositionIndex(i, n); Stitch.check(i >= 0 && i <= n, "checkPositionIndex accepts [0, size]"); } catch (IndexOutOfBoundsException e) { Stitch.check(i < 0 || i > n, "checkPositionIndex rejects out of range"); }
        try { Preconditions.checkPositionIndexes(Math.min(i, j), Math.max(i, j), n); Stitch.check(Math.min(i, j) >= 0 && Math.max(i, j) <= n, "checkPositionIndexes"); } catch (IndexOutOfBoundsException e) { Stitch.check(Math.min(i, j) < 0 || Math.max(i, j) > n, "checkPositionIndexes rejects"); }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void escapers_builder(@Str(max = 64, type = "Text") String a) {
        Escaper e = com.google.common.escape.Escapers.builder().addEscape('a', "<A>").addEscape('\u00e9', "e'").setSafeRange('b', 'z').setUnsafeReplacement("?").build();
        String out = e.escape(a);
        Stitch.check(!out.contains("a"), "escaped chars are replaced");
        Stitch.check(e.escape(out.replace("<A>", "")).length() >= 0, "escape again");

    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void fluent_iterable(@Str(max = 64, type = "Text") String a) {
        List<Integer> l = new ArrayList<>(); for (char c : a.toCharArray()) l.add((int) c % 10);
        FluentIterable<Integer> f = FluentIterable.from(l);
        Stitch.check(f.size() == l.size() && f.toList().equals(l), "from / toList");
        Stitch.check(f.filter(x -> x > 4).size() == l.stream().filter(x -> x > 4).count(), "filter");
        Stitch.check(f.transform(x -> x * 2).toList().size() == l.size(), "transform");
        Stitch.check(f.skip(2).limit(3).size() == Math.max(0, Math.min(3, l.size() - 2)), "skip / limit");
        Stitch.check(f.toSortedSet(Comparator.naturalOrder()).equals(new TreeSet<>(l)), "toSortedSet");
        f.index(x -> x % 3).size(); f.uniqueIndex(x -> x.toString() + System.identityHashCode(x)); f.cycle().limit(5).toList(); f.append(1, 2).first(); f.join(Joiner.on(","));
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void immutable_multisets(@Str(max = 64, type = "Text") String a) {
        List<String> l = new ArrayList<>(); for (char c : a.toCharArray()) l.add(String.valueOf(c));
        ImmutableMultiset<String> m = ImmutableMultiset.copyOf(l);
        Stitch.check(m.size() == l.size() && m.count("a") == Collections.frequency(l, "a"), "ImmutableMultiset");
        ImmutableSortedMultiset<String> sm = ImmutableSortedMultiset.copyOf(l);
        Stitch.check(sm.size() == l.size() && Iterables.elementsEqual(sm, l.stream().sorted().collect(java.util.stream.Collectors.toList())), "ImmutableSortedMultiset is sorted");
        if (!sm.isEmpty()) Stitch.check(sm.headMultiset(sm.lastEntry().getElement(), BoundType.CLOSED).size() == sm.size(), "headMultiset(last, CLOSED) is everything");
        Stitch.check(sm.descendingMultiset().descendingMultiset().equals(sm), "descending twice");
        ImmutableMultiset.<String>builder().addCopies("x", 3).setCount("x", 1).build();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void immutable_multimaps(@Str(max = 64, type = "Text") String a) {
        ImmutableListMultimap.Builder<Character, Integer> b = ImmutableListMultimap.builder();
        for (int i = 0; i < a.length(); i++) b.put(a.charAt(i), i);
        ImmutableListMultimap<Character, Integer> m = b.build();
        Stitch.check(m.size() == a.length(), "one entry per character");
        Stitch.check(m.inverse().inverse().equals(m), "inverse twice");
        ImmutableSetMultimap<Character, Integer> sm = ImmutableSetMultimap.copyOf(m);
        Stitch.check(sm.size() == m.size(), "no duplicate pairs");
        ImmutableListMultimap.<Character, Integer>builder().orderKeysBy(Comparator.naturalOrder()).orderValuesBy(Comparator.reverseOrder()).putAll(m).build().keySet();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void immutable_table_builder(@Str(max = 64, type = "Text") String a) {
        ImmutableTable.Builder<Character, Integer, String> b = ImmutableTable.builder();
        Set<List<Object>> seen = new HashSet<>();
        for (int i = 0; i < a.length(); i++) if (seen.add(Arrays.asList(a.charAt(i), i % 3))) b.put(a.charAt(i), i % 3, a);
        ImmutableTable<Character, Integer, String> t = b.build();
        Stitch.check(t.size() == seen.size(), "table size");
        Stitch.check(ImmutableTable.copyOf(t).equals(t) && t.rowMap().size() == t.rowKeySet().size(), "copyOf / rowMap");
        TreeBasedTable<Character, Integer, String> tt = TreeBasedTable.create(); tt.putAll(t);
        Stitch.check(tt.equals(t), "TreeBasedTable.putAll");
        if (!tt.isEmpty()) tt.rowMap().headMap(tt.rowKeySet().last()).size();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void enum_collections(@Str(max = 64, type = "Text") String a) {
        EnumMultiset<CaseFormat> m = EnumMultiset.create(CaseFormat.class);
        EnumBiMap<CaseFormat, BoundType> bm = EnumBiMap.create(CaseFormat.class, BoundType.class);
        for (char c : a.toCharArray()) { CaseFormat f = CaseFormat.values()[c % 5]; m.add(f); bm.forcePut(f, BoundType.values()[c % 2]); }
        Stitch.check(m.size() == a.length(), "EnumMultiset size");
        Stitch.check(bm.inverse().size() == bm.size() && bm.size() <= 2, "EnumBiMap is a bijection");
        Sets.immutableEnumSet(m.elementSet()); Maps.immutableEnumMap(bm);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void comparison_chain(@Str(max = 64, type = "Text") String a) {
        String b = new StringBuilder(a).reverse().toString();
        int c = ComparisonChain.start().compare(a.length(), b.length()).compare(a, b, Ordering.natural().nullsFirst()).result();
        int ref = a.length() != b.length() ? Integer.compare(a.length(), b.length()) : a.compareTo(b);
        Stitch.check(Integer.signum(c) == Integer.signum(ref), "ComparisonChain");
        Stitch.check(Comparators.emptiesFirst(Comparator.<String>naturalOrder()).compare(java.util.Optional.empty(), java.util.Optional.of(a)) < 0, "emptiesFirst");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void collections2(@Str(max = 64, type = "Text") String a) {
        List<Integer> l = new ArrayList<>(); for (char c : a.toCharArray()) l.add((int) c % 7);
        Collection<Integer> f = Collections2.filter(l, x -> x > 2);
        Stitch.check(f.size() == l.stream().filter(x -> x > 2).count(), "filter view size");
        Collection<String> t = Collections2.transform(l, String::valueOf);
        Stitch.check(t.size() == l.size(), "transform view");
        Stitch.check(Iterators.size(Iterators.partition(l.iterator(), 3)) == (l.size() + 2) / 3, "Iterators.partition");
        Stitch.check(com.google.common.collect.Streams.concat(l.stream(), l.stream()).count() == 2L * l.size(), "Streams.concat");
        Stitch.check(ImmutableList.copyOf(Iterators.limit(Iterators.cycle(l), 5)).size() == (l.isEmpty() ? 0 : 5), "Iterators.cycle");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void futures_direct(@Str(max = 64, type = "Text") String a) {
        com.google.common.util.concurrent.SettableFuture<String> f = com.google.common.util.concurrent.SettableFuture.create();
        com.google.common.util.concurrent.ListenableFuture<Integer> t = com.google.common.util.concurrent.Futures.transform(f, String::length, com.google.common.util.concurrent.MoreExecutors.directExecutor());
        f.set(a);
        Stitch.check(com.google.common.util.concurrent.Futures.getUnchecked(t) == a.length(), "transform of a set future");
        com.google.common.util.concurrent.ListenableFuture<List<Integer>> all = com.google.common.util.concurrent.Futures.allAsList(t, com.google.common.util.concurrent.Futures.immediateFuture(1));
        Stitch.check(com.google.common.util.concurrent.Futures.getUnchecked(all).size() == 2, "allAsList");
        com.google.common.util.concurrent.ListenableFuture<String> failed = com.google.common.util.concurrent.Futures.immediateFailedFuture(new IllegalStateException(a));
        com.google.common.util.concurrent.ListenableFuture<String> caught = com.google.common.util.concurrent.Futures.catching(failed, IllegalStateException.class, ex -> ex.getMessage(), com.google.common.util.concurrent.MoreExecutors.directExecutor());
        Stitch.check(a.equals(com.google.common.util.concurrent.Futures.getUnchecked(caught)), "catching");
        com.google.common.util.concurrent.FluentFuture.from(t).transform(x -> x + 1, com.google.common.util.concurrent.MoreExecutors.directExecutor());
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void atomic_double_striped(@Str(max = 64, type = "Text") String a) {
        com.google.common.util.concurrent.AtomicDouble d = new com.google.common.util.concurrent.AtomicDouble();
        double sum = 0; for (char c : a.toCharArray()) { d.addAndGet(c / 4.0); sum += c / 4.0; }
        Stitch.check(d.get() == sum, "AtomicDouble.addAndGet");
        com.google.common.util.concurrent.Striped<java.util.concurrent.locks.Lock> s = com.google.common.util.concurrent.Striped.lock(4);
        Stitch.check(s.get(a) == s.get(new String(a)), "equal keys share a stripe");
        Stitch.check(s.size() == 4, "stripe count");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void reflection_utils(@Str(max = 64, type = "Text") String a) {
        Stitch.check(com.google.common.reflect.Reflection.getPackageName("a.b.C").equals("a.b"), "getPackageName");
        com.google.common.reflect.TypeToken<List<String>> t = new com.google.common.reflect.TypeToken<List<String>>() {};
        Stitch.check(t.getRawType() == List.class, "raw type");
        Stitch.check(t.getSupertype(Iterable.class).toString().contains("String"), "getSupertype keeps arguments");
        com.google.common.reflect.Invokable<?, ?> inv = com.google.common.reflect.Invokable.from(Object.class.getConstructors()[0]);
        inv.getParameters(); inv.isPublic();
        new com.google.common.reflect.TypeResolver().resolveType(t.getType());
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ascii_case_ops(@Str(max = 64, type = "Text") String a) {
        Stitch.check(Ascii.toUpperCase(Ascii.toLowerCase(a)).equals(Ascii.toUpperCase(a)), "toUpperCase(toLowerCase(a))");
        for (int i = 0; i < a.length(); i++) Stitch.check(Ascii.isUpperCase(Ascii.toUpperCase(a.charAt(i))) || !Ascii.isLowerCase(a.charAt(i)), "toUpperCase of a lowercase ASCII letter");
        Stitch.check(Ascii.toLowerCase(a).equals(a.chars().map(c -> c >= 'A' && c <= 'Z' ? c + 32 : c).collect(StringBuilder::new, StringBuilder::appendCodePoint, StringBuilder::append).toString()), "toLowerCase changes only ASCII letters");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void strings_lenient_format(@Str(max = 64, type = "Text") String a) {
        String f = Strings.lenientFormat(a, 1, null, "x");
        Stitch.check(f.contains("x") || a.contains("%s"), "extra arguments are appended");
        Stitch.check(!f.isEmpty() || a.isEmpty(), "lenientFormat");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void verify_and_throwables(@Str(max = 64, type = "Text") String a) {
        try { Verify.verify(a.length() < 10, "too long: %s", a); } catch (VerifyException e) { Stitch.check(a.length() >= 10, "verify throws only when false"); }
        Throwable t = new RuntimeException(a, new IllegalStateException(a));
        Stitch.check(Throwables.getRootCause(t) instanceof IllegalStateException, "getRootCause");
        Stitch.check(Throwables.getCausalChain(t).size() == 2, "getCausalChain");
        Throwables.lazyStackTraceIsLazy();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void stopwatch_ticker(@Str(max = 64, type = "Text") String a) {
        long[] now = {0};
        Ticker tk = new Ticker() { @Override public long read() { return now[0]; } };
        Stopwatch w = Stopwatch.createStarted(tk);
        now[0] += a.length() * 1000L;
        Stitch.check(w.elapsed(java.util.concurrent.TimeUnit.MICROSECONDS) == a.length(), "elapsed with a fake ticker");
        w.stop(); now[0] += 5000;
        Stitch.check(w.elapsed().toNanos() == a.length() * 1000L, "stopped watch does not advance");
        w.toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void suppliers_memoize(@Str(max = 64, type = "Text") String a) {
        int[] calls = {0};
        Supplier<String> s = Suppliers.memoize(() -> { calls[0]++; return a; });
        s.get(); s.get();
        Stitch.check(calls[0] == 1, "memoize calls the delegate once");
        Stitch.check(Suppliers.compose(String::length, s).get() == a.length(), "compose");
        Suppliers.ofInstance(a).get(); Suppliers.synchronizedSupplier(s).get();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void maps_from_properties_and_enum(@Str(max = 64, type = "Text") String a) {
        Map<String, Integer> m = new HashMap<>(); for (char c : a.toCharArray()) m.merge(String.valueOf(c), 1, Integer::sum);
        SortedMap<String, Integer> sm = Maps.newTreeMap(); sm.putAll(m);
        Stitch.check(Maps.filterEntries(sm, e -> e.getValue() > 1).size() == m.values().stream().filter(v -> v > 1).count(), "filterEntries");
        java.util.Properties p = new java.util.Properties(); p.putAll(Maps.transformValues(m, String::valueOf));
        Stitch.check(Maps.fromProperties(p).size() == m.size(), "fromProperties");
        Stitch.check(Maps.toMap(m.keySet(), m::get).equals(m), "toMap");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void hash_bimap_iteration(@Str(max = 64, type = "Text") String a) {
        HashBiMap<Character, Integer> b = HashBiMap.create();
        for (int i = 0; i < a.length(); i++) b.forcePut(a.charAt(i), i % 4);
        Map<Character, Integer> copy = new HashMap<>(b);
        b.replaceAll((k, v) -> v + 1);
        for (Map.Entry<Character, Integer> e : copy.entrySet()) Stitch.check(b.get(e.getKey()) == e.getValue() + 1, "replaceAll");
        Stitch.check(b.inverse().size() == b.size(), "inverse still a bijection");
        for (Map.Entry<Character, Integer> e : b.entrySet()) Stitch.check(e.getKey().equals(b.inverse().get(e.getValue())), "inverse after replaceAll");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void lists_ops_more(@Str(max = 64, type = "Text") String a) {
        List<Character> l = Lists.charactersOf(a);
        Stitch.check(l.size() == a.length(), "charactersOf");
        List<Character> r = Lists.reverse(new ArrayList<>(l));
        for (int i = 0; i < l.size(); i++) Stitch.check(r.get(i).equals(l.get(l.size() - 1 - i)), "reverse view");
        ListIterator<Character> it = r.listIterator(r.size());
        int n = 0; while (it.hasPrevious()) { it.previous(); n++; }
        Stitch.check(n == l.size(), "reverse view list iterator");
        Lists.newArrayListWithExpectedSize(a.length()).addAll(l);
        Stitch.check(Lists.newLinkedList(l).equals(l) && Lists.newCopyOnWriteArrayList(l).equals(l), "copies equal");
    }
}
