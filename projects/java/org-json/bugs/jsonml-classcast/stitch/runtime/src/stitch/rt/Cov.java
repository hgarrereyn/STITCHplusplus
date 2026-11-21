package stitch.rt;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Coverage map and comparison hooks called by instrumented code (see Agent). Hooks return
 * immediately unless the engine traces the current program (cmplog) and a stub is running.
 */
public final class Cov {
    private Cov() {}

    public static final int MAP_SIZE = 1 << 18;
    public static final byte[] map = new byte[MAP_SIZE];
    static boolean cmplog;
    static boolean inStub;
    static int node;
    static final ConcurrentHashMap<Integer, int[]> SWITCHES = new ConcurrentHashMap<>();
    private static final Set<String> seen = new HashSet<>();
    private static final ByteArrayOutputStream cmps = new ByteArrayOutputStream();
    private static int ncmps;
    private static boolean busy;

    public static void hit(int id) {
        map[id]++;
    }

    static void beginExec() {
        seen.clear();
        cmps.reset();
        ncmps = 0;
    }

    static byte[] edgesBytes() {
        ByteBuffer b = ByteBuffer.allocate(4 + 5 * 4096).order(ByteOrder.LITTLE_ENDIAN);
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        int n = 0;
        for (int i = 0; i < MAP_SIZE; i++) {
            if (map[i] == 0) continue;
            if (b.remaining() < 5) {
                o.write(b.array(), 0, b.position());
                b.clear();
            }
            b.putInt(i).put(map[i]);
            map[i] = 0;
            n++;
        }
        o.write(b.array(), 0, b.position());
        byte[] body = o.toByteArray();
        ByteBuffer out = ByteBuffer.allocate(4 + body.length).order(ByteOrder.LITTLE_ENDIAN);
        return out.putInt(n).put(body).array();
    }

    static byte[] cmpsBytes() {
        byte[] body = cmps.toByteArray();
        ByteBuffer out = ByteBuffer.allocate(4 + body.length).order(ByteOrder.LITTLE_ENDIAN);
        return out.putInt(ncmps).put(body).array();
    }

    private static boolean on() {
        return cmplog && inStub && !busy;
    }

    private static void recInt(long a, long b) {
        String k = node + "|i|" + a + "|" + b;
        if (ncmps >= 16384 || !seen.add(k)) return;
        ByteBuffer r = ByteBuffer.allocate(72).order(ByteOrder.LITTLE_ENDIAN);
        r.putShort((short) node).put((byte) 0).put((byte) 8).put((byte) 8).put((byte) 8).put((byte) 0).put((byte) 0);
        r.putLong(8, a).putLong(40, b);
        cmps.write(r.array(), 0, 72);
        ncmps++;
    }

    private static void recBytes(byte[] a, byte[] b) {
        int la = Math.min(a.length, 32), lb = Math.min(b.length, 32);
        if (la == 0 || lb == 0) return;
        String k = node + "|b|" + new String(a, 0, la, StandardCharsets.ISO_8859_1) + "|" + new String(b, 0, lb, StandardCharsets.ISO_8859_1);
        if (ncmps >= 16384 || !seen.add(k)) return;
        byte[] r = new byte[72];
        r[0] = (byte) node;
        r[1] = (byte) (node >> 8);
        r[2] = 1;
        r[4] = (byte) la;
        r[5] = (byte) lb;
        System.arraycopy(a, 0, r, 8, la);
        System.arraycopy(b, 0, r, 40, lb);
        cmps.write(r, 0, 72);
        ncmps++;
    }

    private static void recStr(String a, String b) {
        if (a == null || b == null || a.equals(b)) return;
        busy = true;
        try {
            recBytes(a.getBytes(StandardCharsets.UTF_8), b.getBytes(StandardCharsets.UTF_8));
        } finally {
            busy = false;
        }
    }

    private static void recKey(Object k, Iterable<?> keys) {
        busy = true;
        try {
            int n = 0;
            for (Object x : keys) {
                if (x == null || x.getClass() != k.getClass() || x.equals(k)) continue;
                if (k instanceof String) recBytes(((String) k).getBytes(StandardCharsets.UTF_8), ((String) x).getBytes(StandardCharsets.UTF_8));
                else if (k instanceof Number) recInt(((Number) k).longValue(), ((Number) x).longValue());
                if (++n >= 64) break;
            }
        } catch (RuntimeException e) {
            // concurrent modification and the like: never disturb the program
        } finally {
            busy = false;
        }
    }

    // ------------------------------------------------------------ hooks (descriptors used by Agent)
    public static void icmp(int a, int b) {
        if (on() && a != b) recInt(a, b);
    }

    public static int lcmp(long a, long b) {
        if (on() && a != b) recInt(a, b);
        return Long.compare(a, b);
    }

    public static void sw(int v, int id) {
        if (!on()) return;
        int[] keys = SWITCHES.get(id);
        if (keys != null) for (int k : keys) if (k != v) recInt(v, k);
    }

    public static boolean strEquals(String a, Object b) {
        boolean r = a.equals(b);
        if (!r && on() && b instanceof String) recStr(a, (String) b);
        return r;
    }

    public static boolean strEqualsIgnoreCase(String a, String b) {
        boolean r = a.equalsIgnoreCase(b);
        if (!r && on()) recStr(a, b);
        return r;
    }

    public static boolean strStartsWith(String a, String b) {
        boolean r = a.startsWith(b);
        if (!r && on()) recStr(a, b);
        return r;
    }

    public static boolean strEndsWith(String a, String b) {
        boolean r = a.endsWith(b);
        if (!r && on()) recStr(a, b);
        return r;
    }

    public static boolean strContains(String a, CharSequence b) {
        boolean r = a.contains(b);
        if (!r && on() && b != null) recStr(a, b.toString());
        return r;
    }

    public static int strIndexOf(String a, String b) {
        int r = a.indexOf(b);
        if (r < 0 && on()) recStr(a, b);
        return r;
    }

    public static int strCompareTo(String a, String b) {
        int r = a.compareTo(b);
        if (r != 0 && on()) recStr(a, b);
        return r;
    }

    public static Object mapGet(Map<?, ?> m, Object k) {
        if (on() && k != null && m.size() <= 256 && !m.containsKey(k)) recKey(k, m.keySet());
        return m.get(k);
    }

    public static boolean mapContainsKey(Map<?, ?> m, Object k) {
        boolean r = m.containsKey(k);
        if (!r && on() && k != null && m.size() <= 256) recKey(k, m.keySet());
        return r;
    }

    public static boolean setContains(Set<?> s, Object k) {
        boolean r = s.contains(k);
        if (!r && on() && k != null && s.size() <= 256) recKey(k, s);
        return r;
    }
}
