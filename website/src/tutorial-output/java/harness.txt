import demo.kv.Store;
import stitch.*;

public class Stubs {
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
