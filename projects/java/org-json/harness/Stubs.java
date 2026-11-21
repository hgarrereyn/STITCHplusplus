import org.json.CDL;
import org.json.Cookie;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONML;
import org.json.JSONObject;
import org.json.JSONPointer;
import org.json.JSONPointerException;
import org.json.XML;
import stitch.*;

/**
 * org.json (JSON-java): JSON objects and arrays, XML/JSONML/Cookie/CDL converters, JSON Pointer.
 * Oracle: toString() and toString(indent) parse back to a similar() value.
 */
public class Stubs {
    @Decoder("JsonText")
    public static String json(Source s) {
        return value(s, 0);
    }

    static String value(Source s, int d) {
        switch (d > 4 ? s.choice(4) : s.choice(7)) {
            case 0: return String.valueOf(s.i64());
            case 1: return "\"" + s.str(10).replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
            case 2: return s.pick("true", "false", "null", "1.5e3", "-0", "0.1", "1e400");
            case 3: return s.token();
            case 4: {
                StringBuilder b = new StringBuilder("[");
                while (s.more()) b.append(value(s, d + 1)).append(s.coin(0.9) ? "," : "");
                return b.append("]").toString();
            }
            case 5: {
                StringBuilder b = new StringBuilder("{");
                while (s.more()) b.append(s.coin(0.8) ? "\"" + s.str(6) + "\"" : s.str(4)).append(s.pick(":", "=", "=>")).append(value(s, d + 1)).append(s.pick(",", ";", ""));
                return b.append("}").toString();
            }
            default: return s.str(8);
        }
    }

    @Decoder("XmlText")
    public static String xml(Source s) {
        StringBuilder b = new StringBuilder();
        while (s.more()) {
            switch (s.choice(5)) {
                case 0: b.append('<').append(s.pick("a", "b", "x:y", "c")).append(s.coin(0.3) ? " k=\"" + s.str(4) + "\"" : "").append('>'); break;
                case 1: b.append("</").append(s.pick("a", "b", "x:y", "c")).append('>'); break;
                case 2: b.append(s.token()); break;
                default: b.append(s.str(6));
            }
        }
        return b.toString();
    }

    static void roundtrip(JSONObject o) {
        for (int indent : new int[] {0, 2}) {
            String text = indent == 0 ? o.toString() : o.toString(indent);
            JSONObject again;
            try {
                again = new JSONObject(text);
            } catch (JSONException e) {
                Stitch.check(false, "toString(" + indent + ") output parses");
                return;
            }
            Stitch.check(again.similar(o) || hasNonFinite(o), "toString(" + indent + ") parses to a similar object");
        }
    }

    static boolean hasNonFinite(Object v) {
        if (v instanceof Double) return ((Double) v).isNaN() || ((Double) v).isInfinite();
        if (v instanceof Float) return ((Float) v).isNaN() || ((Float) v).isInfinite();
        if (v instanceof JSONObject) for (String k : ((JSONObject) v).keySet()) if (hasNonFinite(((JSONObject) v).opt(k))) return true;
        if (v instanceof JSONArray) for (Object x : (JSONArray) v) if (hasNonFinite(x)) return true;
        return false;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static JSONObject parseObject(@Str(max = 4096, type = "JsonText") String text) {
        JSONObject o = new JSONObject(text);
        roundtrip(o);
        return o;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static JSONArray parseArray(@Str(max = 4096, type = "JsonText") String text) {
        JSONArray a = new JSONArray(text);
        new JSONArray(a.toString());
        return a;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static JSONObject fromXml(@Str(max = 4096, type = "XmlText") String text, boolean keepStrings) {
        JSONObject o = XML.toJSONObject(text, keepStrings);
        XML.toString(o);
        return o;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static JSONArray fromJsonML(@Str(max = 4096, type = "XmlText") String text) {
        JSONArray a = JSONML.toJSONArray(text);
        JSONML.toString(a);
        return a;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static JSONObject fromCookie(@Str(max = 256) String text) {
        JSONObject o = Cookie.toJSONObject(text);
        Cookie.toString(o);
        return o;
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static void cdl(@Str(max = 512) String text) {
        JSONArray a = CDL.toJSONArray(text);
        if (a != null) CDL.toString(a);
    }

    @Endpoint
    public static JSONObject newObject() {
        return new JSONObject();
    }

    @Endpoint(bailsOn = {JSONException.class})
    public static void put(JSONObject o, @Str(max = 16) String key, @Range(lo = 0, hi = 8) int kind, long n, double x, @Str(max = 16) String s) {
        switch (kind) {
            case 0: o.put(key, n); break;
            case 1: o.put(key, x); break;
            case 2: o.put(key, s); break;
            case 3: o.put(key, n % 2 == 0); break;
            case 4: o.put(key, new JSONArray().put(n).put(s)); break;
            case 5: o.put(key, new JSONObject().put("k", x)); break;
            case 6: o.accumulate(key, n); break;
            case 7: o.append(key, s); break;
            default: o.increment(key);
        }
        roundtrip(o);
    }

    @Endpoint(optional = true)
    public static JSONObject child(JSONObject o, Source s) {
        Object[] keys = o.keySet().toArray();
        Stitch.require(keys.length > 0, "empty");
        Object v = o.opt((String) s.pick(keys));
        return v instanceof JSONObject ? (JSONObject) v : null;
    }

    @Endpoint
    public static void remove(JSONObject o, Source s) {
        Object[] keys = o.keySet().toArray();
        Stitch.require(keys.length > 0, "empty");
        o.remove((String) s.pick(keys));
        roundtrip(o);
    }

    @Endpoint(bailsOn = {JSONPointerException.class, IllegalArgumentException.class})
    public static void pointer(JSONObject o, @Str(max = 48) String ptr) {
        new JSONPointer(ptr).queryFrom(o);
        o.optQuery(ptr);
    }

    @Endpoint
    public static void toXml(JSONObject o) {
        String xml = XML.toString(o, "root");
        try {
            XML.toJSONObject(xml);
        } catch (JSONException e) {
            Stitch.check(false, "XML.toString() output parses as XML");
        }
    }
}
