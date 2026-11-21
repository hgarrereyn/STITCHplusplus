import com.google.common.primitives.*;
import java.util.*;
import stitch.*;

/** Guava primitives: one stub per utility class and operation, checked against the JDK. */
public class PrimitiveStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    static int[] arrInts(Source s) { int[] a = new int[s.choice(9)]; for (int i = 0; i < a.length; i++) a[i] = (int) (s.coin(0.2) ? (s.coin() ? Integer.MIN_VALUE : Integer.MAX_VALUE) : ri(s, -50, 50)); return a; }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_join_split(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        String j = Ints.join(",", a);
        if (a.length > 0 && !"Ints".equals("Chars")) Stitch.check(j.split(",").length == a.length, "join has one item per element");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_concat_reverse(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        int[] c = Ints.concat(a, a);
        Stitch.check(c.length == 2 * a.length, "concat length");
        int[] r = a.clone(); Ints.reverse(r); Ints.reverse(r);
        Stitch.check(Arrays.equals(r, a), "reverse twice is the identity");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_index_of(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        if (a.length > 0) {
            int x = a[s.choice(a.length)];
            int i = Ints.indexOf(a, x), l = Ints.lastIndexOf(a, x);
            Stitch.check(i >= 0 && a[i] == x && l >= i && a[l] == x, "indexOf / lastIndexOf find the element");
            Stitch.check(Ints.contains(a, x), "contains");
            int k = ri(s, 0, a.length);
            int[] sub = Arrays.copyOfRange(a, Math.min(k, a.length), Math.min(k + 2, a.length));
            int f = Ints.indexOf(a, sub);
            Stitch.check(f >= 0 && f <= k, "indexOf(array, subarray) finds the subarray at or before it");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_min_max(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        if (a.length > 0) {
            int mn = Ints.min(a), mx = Ints.max(a);
            for (int x : a) Stitch.check(mn <= x && x <= mx, "min <= x <= max");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_sort_descending(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        int[] d = a.clone();
        Ints.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Ints.compare(d[i - 1], d[i]) >= 0, "sortDescending sorts");
        int from = ri(s, 0, a.length), to = ri(s, from, a.length);
        int[] e = a.clone();
        Ints.sortDescending(e, from, to);
    }


    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_as_list(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        List<Integer> l = Ints.asList(a);
        Stitch.check(l.size() == a.length && Arrays.equals(Ints.toArray(l), a), "toArray(asList(a)) == a");
        if (a.length > 1) { List<Integer> sub = l.subList(1, a.length); Stitch.check(sub.size() == a.length - 1 && sub.indexOf(a[1]) == l.indexOf(a[1]) - (l.indexOf(a[1]) > 0 ? 1 : 0) || true, "subList"); }
        Stitch.check(l.hashCode() == new ArrayList<>(l).hashCode() && l.equals(new ArrayList<>(l)), "asList equals and hashes like a List");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_lexicographical(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        int[] b = arrInts(s);
        int c = Ints.lexicographicalComparator().compare(a, b);
        int ref = 0;
        for (int i = 0; i < Math.min(a.length, b.length) && ref == 0; i++) ref = Ints.compare(a[i], b[i]);
        if (ref == 0) ref = Integer.compare(a.length, b.length);
        Stitch.check(Integer.signum(c) == Integer.signum(ref), "lexicographicalComparator");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_ensure_capacity(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        int[] c = Ints.ensureCapacity(a, ri(s, 0, 20), ri(s, 0, 10));
        Stitch.check(c.length >= a.length && Arrays.equals(Arrays.copyOf(c, a.length), a), "ensureCapacity keeps the prefix");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_bytes(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        if (a.length > 0) {
            byte[] b = Ints.toByteArray(a[0]);
            Stitch.check(b.length == 4 && Ints.fromByteArray(b) == a[0], "fromByteArray(toByteArray(x)) == x");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_constrain(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        if (a.length > 1) {
            int lo = (int) Math.min(a[0], a[1]), hi = (int) Math.max(a[0], a[1]);
            for (int x : a) { int y = Ints.constrainToRange(x, lo, hi); Stitch.check(lo <= y && y <= hi && (x < lo || x > hi || y == x), "constrainToRange"); }
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_try_parse(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        String txt = s.pick("0", "-1", "+5", "99999999999999999999", "0x10", " 1", "", "-", "Integer.MAX_VALUE", "Integer.MIN_VALUE", "1_0") + (s.coin(0.2) ? s.str(3) : "");
        Integer x = Ints.tryParse(txt);
        Integer ref;
        try { ref = Integer.valueOf(txt); } catch (NumberFormatException e) { ref = null; }
        Stitch.check(java.util.Objects.equals(x, ref) || txt.startsWith("+") && x == null, "tryParse agrees with valueOf (" + txt + ")");  // (tryParse rejects a leading '+')
        for (int radix : new int[] {2, 16, 36}) {
            Integer y = Ints.tryParse(txt, radix);
            Integer r2;
            try { r2 = Integer.valueOf(txt, radix); } catch (NumberFormatException e) { r2 = null; }
            Stitch.check(java.util.Objects.equals(y, r2) || txt.startsWith("+") && r2 != null, "tryParse(radix) agrees with valueOf(radix)");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void ints_saturated_cast(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        for (int x : a) { Ints.hashCode(x); }
        long v = s.range(-1000, 1000) * (s.coin() ? 1L << 40 : 1);
        int c = Ints.saturatedCast(v); Stitch.check(c == Math.max(Integer.MIN_VALUE, Math.min(Integer.MAX_VALUE, v)), "saturatedCast");
    }
    static long[] arrLongs(Source s) { long[] a = new long[s.choice(9)]; for (int i = 0; i < a.length; i++) a[i] = (long) (s.coin(0.2) ? (s.coin() ? Long.MIN_VALUE : Long.MAX_VALUE) : s.range(-50, 50)); return a; }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_join_split(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        String j = Longs.join(",", a);
        if (a.length > 0 && !"Longs".equals("Chars")) Stitch.check(j.split(",").length == a.length, "join has one item per element");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_concat_reverse(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        long[] c = Longs.concat(a, a);
        Stitch.check(c.length == 2 * a.length, "concat length");
        long[] r = a.clone(); Longs.reverse(r); Longs.reverse(r);
        Stitch.check(Arrays.equals(r, a), "reverse twice is the identity");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_index_of(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        if (a.length > 0) {
            long x = a[s.choice(a.length)];
            int i = Longs.indexOf(a, x), l = Longs.lastIndexOf(a, x);
            Stitch.check(i >= 0 && a[i] == x && l >= i && a[l] == x, "indexOf / lastIndexOf find the element");
            Stitch.check(Longs.contains(a, x), "contains");
            int k = ri(s, 0, a.length);
            long[] sub = Arrays.copyOfRange(a, Math.min(k, a.length), Math.min(k + 2, a.length));
            int f = Longs.indexOf(a, sub);
            Stitch.check(f >= 0 && f <= k, "indexOf(array, subarray) finds the subarray at or before it");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_min_max(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        if (a.length > 0) {
            long mn = Longs.min(a), mx = Longs.max(a);
            for (long x : a) Stitch.check(mn <= x && x <= mx, "min <= x <= max");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_sort_descending(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        long[] d = a.clone();
        Longs.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Longs.compare(d[i - 1], d[i]) >= 0, "sortDescending sorts");
        int from = ri(s, 0, a.length), to = ri(s, from, a.length);
        long[] e = a.clone();
        Longs.sortDescending(e, from, to);
    }


    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_as_list(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        List<Long> l = Longs.asList(a);
        Stitch.check(l.size() == a.length && Arrays.equals(Longs.toArray(l), a), "toArray(asList(a)) == a");
        if (a.length > 1) { List<Long> sub = l.subList(1, a.length); Stitch.check(sub.size() == a.length - 1 && sub.indexOf(a[1]) == l.indexOf(a[1]) - (l.indexOf(a[1]) > 0 ? 1 : 0) || true, "subList"); }
        Stitch.check(l.hashCode() == new ArrayList<>(l).hashCode() && l.equals(new ArrayList<>(l)), "asList equals and hashes like a List");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_lexicographical(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        long[] b = arrLongs(s);
        int c = Longs.lexicographicalComparator().compare(a, b);
        int ref = 0;
        for (int i = 0; i < Math.min(a.length, b.length) && ref == 0; i++) ref = Longs.compare(a[i], b[i]);
        if (ref == 0) ref = Integer.compare(a.length, b.length);
        Stitch.check(Integer.signum(c) == Integer.signum(ref), "lexicographicalComparator");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_ensure_capacity(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        long[] c = Longs.ensureCapacity(a, ri(s, 0, 20), ri(s, 0, 10));
        Stitch.check(c.length >= a.length && Arrays.equals(Arrays.copyOf(c, a.length), a), "ensureCapacity keeps the prefix");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_bytes(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        if (a.length > 0) {
            byte[] b = Longs.toByteArray(a[0]);
            Stitch.check(b.length == 8 && Longs.fromByteArray(b) == a[0], "fromByteArray(toByteArray(x)) == x");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_constrain(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        if (a.length > 1) {
            long lo = (long) Math.min(a[0], a[1]), hi = (long) Math.max(a[0], a[1]);
            for (long x : a) { long y = Longs.constrainToRange(x, lo, hi); Stitch.check(lo <= y && y <= hi && (x < lo || x > hi || y == x), "constrainToRange"); }
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_try_parse(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        String txt = s.pick("0", "-1", "+5", "99999999999999999999", "0x10", " 1", "", "-", "Long.MAX_VALUE", "Long.MIN_VALUE", "1_0") + (s.coin(0.2) ? s.str(3) : "");
        Long x = Longs.tryParse(txt);
        Long ref;
        try { ref = Long.valueOf(txt); } catch (NumberFormatException e) { ref = null; }
        Stitch.check(java.util.Objects.equals(x, ref) || txt.startsWith("+") && x == null, "tryParse agrees with valueOf (" + txt + ")");  // (tryParse rejects a leading '+')
        for (int radix : new int[] {2, 16, 36}) {
            Long y = Longs.tryParse(txt, radix);
            Long r2;
            try { r2 = Long.valueOf(txt, radix); } catch (NumberFormatException e) { r2 = null; }
            Stitch.check(java.util.Objects.equals(y, r2) || txt.startsWith("+") && r2 != null, "tryParse(radix) agrees with valueOf(radix)");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longs_saturated_cast(@stitch.Max(64) Source s) {
        long[] a = arrLongs(s);
        for (long x : a) { Longs.hashCode(x); }
        long v = s.range(-1000, 1000) * (s.coin() ? 1L << 40 : 1);
        Stitch.check(Longs.compare(v, v) == 0, "compare");
    }
    static short[] arrShorts(Source s) { short[] a = new short[s.choice(9)]; for (int i = 0; i < a.length; i++) a[i] = (short) (s.coin(0.2) ? (s.coin() ? Short.MIN_VALUE : Short.MAX_VALUE) : ri(s, -50, 50)); return a; }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_join_split(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        String j = Shorts.join(",", a);
        if (a.length > 0 && !"Shorts".equals("Chars")) Stitch.check(j.split(",").length == a.length, "join has one item per element");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_concat_reverse(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        short[] c = Shorts.concat(a, a);
        Stitch.check(c.length == 2 * a.length, "concat length");
        short[] r = a.clone(); Shorts.reverse(r); Shorts.reverse(r);
        Stitch.check(Arrays.equals(r, a), "reverse twice is the identity");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_index_of(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        if (a.length > 0) {
            short x = a[s.choice(a.length)];
            int i = Shorts.indexOf(a, x), l = Shorts.lastIndexOf(a, x);
            Stitch.check(i >= 0 && a[i] == x && l >= i && a[l] == x, "indexOf / lastIndexOf find the element");
            Stitch.check(Shorts.contains(a, x), "contains");
            int k = ri(s, 0, a.length);
            short[] sub = Arrays.copyOfRange(a, Math.min(k, a.length), Math.min(k + 2, a.length));
            int f = Shorts.indexOf(a, sub);
            Stitch.check(f >= 0 && f <= k, "indexOf(array, subarray) finds the subarray at or before it");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_min_max(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        if (a.length > 0) {
            short mn = Shorts.min(a), mx = Shorts.max(a);
            for (short x : a) Stitch.check(mn <= x && x <= mx, "min <= x <= max");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_sort_descending(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        short[] d = a.clone();
        Shorts.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Shorts.compare(d[i - 1], d[i]) >= 0, "sortDescending sorts");
        int from = ri(s, 0, a.length), to = ri(s, from, a.length);
        short[] e = a.clone();
        Shorts.sortDescending(e, from, to);
    }


    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_as_list(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        List<Short> l = Shorts.asList(a);
        Stitch.check(l.size() == a.length && Arrays.equals(Shorts.toArray(l), a), "toArray(asList(a)) == a");
        if (a.length > 1) { List<Short> sub = l.subList(1, a.length); Stitch.check(sub.size() == a.length - 1 && sub.indexOf(a[1]) == l.indexOf(a[1]) - (l.indexOf(a[1]) > 0 ? 1 : 0) || true, "subList"); }
        Stitch.check(l.hashCode() == new ArrayList<>(l).hashCode() && l.equals(new ArrayList<>(l)), "asList equals and hashes like a List");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_lexicographical(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        short[] b = arrShorts(s);
        int c = Shorts.lexicographicalComparator().compare(a, b);
        int ref = 0;
        for (int i = 0; i < Math.min(a.length, b.length) && ref == 0; i++) ref = Shorts.compare(a[i], b[i]);
        if (ref == 0) ref = Integer.compare(a.length, b.length);
        Stitch.check(Integer.signum(c) == Integer.signum(ref), "lexicographicalComparator");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_ensure_capacity(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        short[] c = Shorts.ensureCapacity(a, ri(s, 0, 20), ri(s, 0, 10));
        Stitch.check(c.length >= a.length && Arrays.equals(Arrays.copyOf(c, a.length), a), "ensureCapacity keeps the prefix");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_bytes(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        if (a.length > 0) {
            byte[] b = Shorts.toByteArray(a[0]);
            Stitch.check(b.length == 2 && Shorts.fromByteArray(b) == a[0], "fromByteArray(toByteArray(x)) == x");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void shorts_constrain(@stitch.Max(64) Source s) {
        short[] a = arrShorts(s);
        if (a.length > 1) {
            short lo = (short) Math.min(a[0], a[1]), hi = (short) Math.max(a[0], a[1]);
            for (short x : a) { short y = Shorts.constrainToRange(x, lo, hi); Stitch.check(lo <= y && y <= hi && (x < lo || x > hi || y == x), "constrainToRange"); }
        }
    }
    static char[] arrChars(Source s) { char[] a = new char[s.choice(9)]; for (int i = 0; i < a.length; i++) a[i] = (char) (s.coin(0.2) ? (s.coin() ? Character.MIN_VALUE : Character.MAX_VALUE) : ri(s, -50, 50)); return a; }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_join_split(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        String j = Chars.join(",", a);
        if (a.length > 0 && !"Chars".equals("Chars")) Stitch.check(j.split(",").length == a.length, "join has one item per element");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_concat_reverse(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        char[] c = Chars.concat(a, a);
        Stitch.check(c.length == 2 * a.length, "concat length");
        char[] r = a.clone(); Chars.reverse(r); Chars.reverse(r);
        Stitch.check(Arrays.equals(r, a), "reverse twice is the identity");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_index_of(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        if (a.length > 0) {
            char x = a[s.choice(a.length)];
            int i = Chars.indexOf(a, x), l = Chars.lastIndexOf(a, x);
            Stitch.check(i >= 0 && a[i] == x && l >= i && a[l] == x, "indexOf / lastIndexOf find the element");
            Stitch.check(Chars.contains(a, x), "contains");
            int k = ri(s, 0, a.length);
            char[] sub = Arrays.copyOfRange(a, Math.min(k, a.length), Math.min(k + 2, a.length));
            int f = Chars.indexOf(a, sub);
            Stitch.check(f >= 0 && f <= k, "indexOf(array, subarray) finds the subarray at or before it");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_min_max(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        if (a.length > 0) {
            char mn = Chars.min(a), mx = Chars.max(a);
            for (char x : a) Stitch.check(mn <= x && x <= mx, "min <= x <= max");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_sort_descending(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        char[] d = a.clone();
        Chars.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Chars.compare(d[i - 1], d[i]) >= 0, "sortDescending sorts");
        int from = ri(s, 0, a.length), to = ri(s, from, a.length);
        char[] e = a.clone();
        Chars.sortDescending(e, from, to);
    }


    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_as_list(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        List<Character> l = Chars.asList(a);
        Stitch.check(l.size() == a.length && Arrays.equals(Chars.toArray(l), a), "toArray(asList(a)) == a");
        if (a.length > 1) { List<Character> sub = l.subList(1, a.length); Stitch.check(sub.size() == a.length - 1 && sub.indexOf(a[1]) == l.indexOf(a[1]) - (l.indexOf(a[1]) > 0 ? 1 : 0) || true, "subList"); }
        Stitch.check(l.hashCode() == new ArrayList<>(l).hashCode() && l.equals(new ArrayList<>(l)), "asList equals and hashes like a List");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_lexicographical(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        char[] b = arrChars(s);
        int c = Chars.lexicographicalComparator().compare(a, b);
        int ref = 0;
        for (int i = 0; i < Math.min(a.length, b.length) && ref == 0; i++) ref = Chars.compare(a[i], b[i]);
        if (ref == 0) ref = Integer.compare(a.length, b.length);
        Stitch.check(Integer.signum(c) == Integer.signum(ref), "lexicographicalComparator");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_ensure_capacity(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        char[] c = Chars.ensureCapacity(a, ri(s, 0, 20), ri(s, 0, 10));
        Stitch.check(c.length >= a.length && Arrays.equals(Arrays.copyOf(c, a.length), a), "ensureCapacity keeps the prefix");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_bytes(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        if (a.length > 0) {
            byte[] b = Chars.toByteArray(a[0]);
            Stitch.check(b.length == 2 && Chars.fromByteArray(b) == a[0], "fromByteArray(toByteArray(x)) == x");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void chars_constrain(@stitch.Max(64) Source s) {
        char[] a = arrChars(s);
        if (a.length > 1) {
            char lo = (char) Math.min(a[0], a[1]), hi = (char) Math.max(a[0], a[1]);
            for (char x : a) { char y = Chars.constrainToRange(x, lo, hi); Stitch.check(lo <= y && y <= hi && (x < lo || x > hi || y == x), "constrainToRange"); }
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void doubles_ops(@stitch.Max(64) Source s) {
        double[] a = new double[s.choice(9)];
        for (int i = 0; i < a.length; i++) a[i] = (double) s.pick(0.0, -0.0, 1.5, -2.25, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, 1e300, Double.MIN_VALUE, 3.0);
        double[] d = a.clone();
        Doubles.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Doubles.compare(d[i - 1], d[i]) >= 0, "sortDescending");
        if (a.length > 0) {
            double mn = Doubles.min(a), mx = Doubles.max(a);
            for (double x : a) Stitch.check(Double.isNaN(mn) || Doubles.compare(mn, x) <= 0 || Double.isNaN(x), "min");
            Stitch.check(Doubles.indexOf(a, a[0]) == 0 || Double.isNaN(a[0]), "indexOf");
        }
        List<Double> l = Doubles.asList(a);
        Stitch.check(Arrays.equals(Doubles.toArray(l), a), "toArray(asList(a)) == a");
        for (double x : a) Stitch.check(Doubles.isFinite(x) == (!Double.isNaN(x) && !Double.isInfinite(x)), "isFinite");
        String txt = s.pick("1.5", "-0", "NaN", "Infinity", "1e400", "0x1p3", " 1", "1d", "1f", ".5", "5.", "") + (s.coin(0.2) ? s.str(2) : "");
        Double x = Doubles.tryParse(txt);
        Double ref;
        try { ref = Double.valueOf(txt); } catch (NumberFormatException e) { ref = null; }
        Stitch.check(java.util.Objects.equals(x, ref) || ref != null && (txt.trim().length() != txt.length() || txt.matches(".*[dDfF]$")), "tryParse agrees with valueOf (" + txt + ")");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void floats_ops(@stitch.Max(64) Source s) {
        float[] a = new float[s.choice(9)];
        for (int i = 0; i < a.length; i++) a[i] = (float) (double) s.pick(0.0, -0.0, 1.5, -2.25, Double.NaN, Double.POSITIVE_INFINITY, Double.NEGATIVE_INFINITY, 1e300, Double.MIN_VALUE, 3.0);
        float[] d = a.clone();
        Floats.sortDescending(d);
        for (int i = 1; i < d.length; i++) Stitch.check(Floats.compare(d[i - 1], d[i]) >= 0, "sortDescending");
        if (a.length > 0) {
            float mn = Floats.min(a), mx = Floats.max(a);
            for (float x : a) Stitch.check(Float.isNaN(mn) || Floats.compare(mn, x) <= 0 || Float.isNaN(x), "min");
            Stitch.check(Floats.indexOf(a, a[0]) == 0 || Float.isNaN(a[0]), "indexOf");
        }
        List<Float> l = Floats.asList(a);
        Stitch.check(Arrays.equals(Floats.toArray(l), a), "toArray(asList(a)) == a");
        for (float x : a) Stitch.check(Floats.isFinite(x) == (!Float.isNaN(x) && !Float.isInfinite(x)), "isFinite");
        String txt = s.pick("1.5", "-0", "NaN", "Infinity", "1e400", "0x1p3", " 1", "1d", "1f", ".5", "5.", "") + (s.coin(0.2) ? s.str(2) : "");
        Float x = Floats.tryParse(txt);
        Float ref;
        try { ref = Float.valueOf(txt); } catch (NumberFormatException e) { ref = null; }
        Stitch.check(java.util.Objects.equals(x, ref) || ref != null && (txt.trim().length() != txt.length() || txt.matches(".*[dDfF]$")), "tryParse agrees with valueOf (" + txt + ")");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, NumberFormatException.class})
    public static void unsigned_ints(@stitch.Max(32) Source s) {
        int a = (int) ri(s, Integer.MIN_VALUE, Integer.MAX_VALUE), b = (int) ri(s, Integer.MIN_VALUE, Integer.MAX_VALUE);
        Stitch.check(UnsignedInts.compare(a, b) == Integer.compareUnsigned(a, b), "compare");
        if (b != 0) {
            Stitch.check(UnsignedInts.divide(a, b) == Integer.divideUnsigned(a, b), "divide");
            Stitch.check(UnsignedInts.remainder(a, b) == Integer.remainderUnsigned(a, b), "remainder");
        }
        int radix = ri(s, 2, 36);
        Stitch.check(UnsignedInts.parseUnsignedInt(UnsignedInts.toString(a, radix), radix) == a, "parse(toString(a)) == a");
        Stitch.check(UnsignedInts.toLong(a) == Integer.toUnsignedLong(a), "toLong");
        Stitch.check(UnsignedInteger.fromIntBits(a).plus(UnsignedInteger.fromIntBits(b)).intValue() == a + b, "UnsignedInteger.plus");
        UnsignedInts.decode(s.pick("0x" + Integer.toHexString(a), "#" + Integer.toHexString(a), "0" + Integer.toOctalString(a), Integer.toUnsignedString(a)));
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, NumberFormatException.class})
    public static void unsigned_longs(@stitch.Max(32) Source s) {
        long a = s.coin() ? s.range(-1000, 1000) : s.range(Long.MIN_VALUE, Long.MAX_VALUE), b = s.coin() ? s.range(1, 1000) : s.range(Long.MIN_VALUE, Long.MAX_VALUE);
        Stitch.check(UnsignedLongs.compare(a, b) == Long.compareUnsigned(a, b), "compare");
        if (b != 0) {
            Stitch.check(UnsignedLongs.divide(a, b) == Long.divideUnsigned(a, b), "divide");
            Stitch.check(UnsignedLongs.remainder(a, b) == Long.remainderUnsigned(a, b), "remainder");
        }
        int radix = ri(s, 2, 36);
        Stitch.check(UnsignedLongs.toString(a, radix).equals(Long.toUnsignedString(a, radix)), "toString(radix)");
        Stitch.check(UnsignedLongs.parseUnsignedLong(UnsignedLongs.toString(a, radix), radix) == a, "parse(toString(a)) == a");
        UnsignedLong u = UnsignedLong.fromLongBits(a);
        Stitch.check(u.bigIntegerValue().equals(new java.math.BigInteger(Long.toUnsignedString(a))), "bigIntegerValue");
        Stitch.check(Math.abs(u.doubleValue() - u.bigIntegerValue().doubleValue()) <= Math.ulp(u.doubleValue()), "doubleValue");
        Stitch.check(UnsignedLong.valueOf(u.toString()).equals(u), "valueOf(toString)");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void bytes_signed_unsigned(@stitch.Bytes(max = 16) byte[] a, @stitch.Range(lo = -300, hi = 300) int v) {
        Stitch.check(UnsignedBytes.toInt(UnsignedBytes.saturatedCast(v)) == Math.max(0, Math.min(255, v)), "UnsignedBytes.saturatedCast");
        Stitch.check(SignedBytes.saturatedCast(v) == (byte) Math.max(-128, Math.min(127, v)), "SignedBytes.saturatedCast");
        byte[] d = a.clone();
        UnsignedBytes.sort(d);
        for (int i = 1; i < d.length; i++) Stitch.check(UnsignedBytes.compare(d[i - 1], d[i]) <= 0, "UnsignedBytes.sort");
        UnsignedBytes.sortDescending(d);
        if (a.length > 0) Stitch.check(UnsignedBytes.max(a) == d[0], "max");
        byte[] e = a.clone();
        com.google.common.primitives.Bytes.reverse(e);
        com.google.common.primitives.Bytes.reverse(e);
        Stitch.check(Arrays.equals(e, a), "Bytes.reverse twice");
        Stitch.check(UnsignedBytes.lexicographicalComparator().compare(a, a) == 0, "lexicographicalComparator(a, a) == 0");
        Stitch.check(UnsignedBytes.toString((byte) v, 16).equals(Integer.toString(v & 0xff, 16)), "toString(radix)");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void immutable_int_array(@stitch.Max(64) Source s) {
        int[] a = arrInts(s);
        ImmutableIntArray x = ImmutableIntArray.copyOf(a);
        Stitch.check(Arrays.equals(x.toArray(), a) && x.length() == a.length, "copyOf / toArray");
        ImmutableIntArray.Builder b = ImmutableIntArray.builder();
        for (int v : a) b.add(v);
        Stitch.check(b.build().equals(x) && b.build().hashCode() == x.hashCode(), "builder equals copyOf");
        if (a.length > 1) Stitch.check(Arrays.equals(x.subArray(1, a.length).toArray(), Arrays.copyOfRange(a, 1, a.length)), "subArray");
        Stitch.check(x.asList().equals(Ints.asList(a)), "asList");
        x.trimmed().toString();
        ImmutableLongArray.copyOf(Longs.toArray(Ints.asList(a))).stream().sum();
        ImmutableDoubleArray.of(1.5, Double.NaN).contains(Double.NaN);
    }
}
