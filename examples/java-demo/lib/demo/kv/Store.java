package demo.kv;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** A tiny key-value store with planted bugs (STITCH++ example). */
public class Store {
    private final Map<String, String> data = new HashMap<>();
    private final List<String> order = new ArrayList<>();

    public void put(String key, String value) {
        if (key.isEmpty() || key.contains("\n") || value.contains("\n")) throw new IllegalArgumentException("bad entry");
        if (!data.containsKey(key)) order.add(key);
        data.put(key, value);
    }

    public void remove(String key) {
        if (data.remove(key) != null && !key.startsWith("tmp")) order.remove(key); // BUG: tmp keys stay
    }

    public String dump() {
        StringBuilder b = new StringBuilder();
        for (String k : order) b.append(k).append('=').append(data.get(k).trim()).append('\n'); // NPE after a tmp removal
        return b.toString();
    }

    public int size() {
        return data.size();
    }

    public static Store load(String text) {
        Store s = new Store();
        for (String line : text.split("\n")) {
            if (line.isEmpty()) continue;
            int eq = line.indexOf('=');
            if (eq < 0) throw new IllegalArgumentException("no '='");
            String v = line.substring(eq + 1);
            if (v.startsWith("#!")) v = v.substring(2, v.indexOf(' ')); // BUG: "#!x" has no space
            s.put(line.substring(0, eq), v); // BUG: keys with '=' do not round-trip
        }
        return s;
    }
}
