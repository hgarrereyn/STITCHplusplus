package stitch;

import stitch.rt.Runtime;

/**
 * STITCH harness API for Java.
 *
 * <pre>
 * public class Stubs {
 *     &#64;Endpoint(bailsOn = {IllegalArgumentException.class})
 *     public static Document parse(&#64;Str(max = 4096, type = "Html") String html) {
 *         return Jsoup.parse(html);
 *     }
 *
 *     &#64;Endpoint
 *     public static void select(Document doc, &#64;Str(max = 32) String css) {
 *         doc.select(css);
 *     }
 * }
 * </pre>
 *
 * Parameters of non-data types are objects (borrowed; &#64;Take consumes them); a non-void
 * return value is a new object. Any other Throwable is a bug. Reference: `stitch guide java`.
 */
public final class Stitch {
    private Stitch() {}

    /** Stop this call; not a bug. The program continues. */
    public static Bail bail(String reason) {
        throw new Bail(reason);
    }

    public static void require(boolean cond, String reason) {
        if (!cond) throw new Bail(reason);
    }

    /** An oracle: a false condition is reported as a bug ("check failed: what"). */
    public static void check(boolean cond, String what) {
        if (!cond) throw new CheckFailed(what);
    }

    /** Printed by `stitch exec --trace`. */
    public static void log(String fmt, Object... args) {
        Runtime.RT.log(args.length == 0 ? fmt : String.format(fmt, args));
    }

    /** The library took ownership of a borrowed input in this call. */
    public static void consume(Object obj) {
        Runtime.RT.consume(obj);
    }

    /** Attach state to an object: numbers, strings, other objects (links die with them), anything. */
    public static void set(Object obj, String key, Object value) {
        Runtime.RT.attrSet(obj, key, value);
    }

    /** An attribute, or null. A missing key is reported by `stitch probe`. */
    public static Object get(Object obj, String key) {
        return Runtime.RT.attrGet(obj, key, true);
    }

    public static long getLong(Object obj, String key, long def) {
        Object v = Runtime.RT.attrGet(obj, key, false);
        return v instanceof Number ? ((Number) v).longValue() : def;
    }

    public static String getString(Object obj, String key, String def) {
        Object v = Runtime.RT.attrGet(obj, key, false);
        return v instanceof String ? (String) v : def;
    }

    /** A linked object, or null if it is missing or was consumed. */
    public static <T> T getObj(Object obj, String key, Class<T> type) {
        Object v = Runtime.RT.attrGet(obj, key, false);
        return type.isInstance(v) ? type.cast(v) : null;
    }

    public static boolean has(Object obj, String key) {
        return Runtime.RT.attrHas(obj, key);
    }

    public static void del(Object obj, String key) {
        Runtime.RT.attrDel(obj, key);
    }

    public static void globalSet(String key, Object value) {
        Runtime.RT.globals.put(key, value);
    }

    public static Object globalGet(String key) {
        if (!Runtime.RT.globals.containsKey(key)) Runtime.RT.noteMiss(key);
        return Runtime.RT.globals.get(key);
    }
}
