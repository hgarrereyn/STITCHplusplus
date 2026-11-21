import demo.kv.Store;
import stitch.*;

public class Stubs {
    // A typed data type: "Pairs" decodes to a list of {key, value}, drawing each with "Pair"'s decoder.
    @Decoder("Pair")
    public static String[] pair(Source s) {
        String k = s.coin(0.3) ? s.token() : s.str(8);
        return new String[] {k, s.coin() ? s.str(12) : String.format("%.2f", s.f64(0, 100))};
    }

    @Decoder("Pairs")
    public static java.util.List<String[]> pairs(Source s) {
        java.util.List<String[]> out = new java.util.ArrayList<>();
        while (s.more()) out.add(s.draw("Pair"));
        return out;
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void putAll(Store st, @Val(value = "Pairs", max = 256) java.util.List<String[]> kvs) {
        for (String[] kv : kvs) st.put(kv[0], kv[1]);
    }

    @Decoder("KvText")
    public static String kvText(Source s) {
        StringBuilder b = new StringBuilder();
        while (s.more()) b.append(s.coin(0.3) ? s.token() : s.str(8)).append('=').append(s.str(12)).append('\n');
        return b.toString();
    }

    @Endpoint
    public static Store newStore() {
        return new Store();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static Store load(@Str(max = 256, type = "KvText") String text) {
        return Store.load(text);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void put(Store st, @Str(max = 16) String key, @Str(max = 32) String value) {
        st.put(key, value);
    }

    @Endpoint
    public static void remove(Store st, @Str(max = 16) String key) {
        st.remove(key);
    }

    @Endpoint
    public static void roundtrip(Store st) {
        Store again = Store.load(st.dump());
        Stitch.check(again.size() == st.size(), "dump/load roundtrip");
    }
}
