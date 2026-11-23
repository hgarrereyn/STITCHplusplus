package stitch;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Decisions inside decoders and stubs, read from a fuzzer-owned choice buffer. Same encoding as
 * the C++ runtime: little-endian fixed-width integers, 8-byte ranges, 1-byte coin/more,
 * length-prefixed strings.
 */
public final class Source {
    private static final long[] INTERESTING = {0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 100, 127, 128, 255, 256, 512,
        1000, 1024, 4096, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffffL, 0x80000000L, 0xffffffffL};

    private byte[] buf;
    private int len;
    private int pos;
    private final int cap;
    private final boolean gen;
    private long rngState;
    private final List<byte[]> dict;
    private final List<Integer> spans;
    private boolean exhausted;
    // loop iterations for the engine: site, instance, start, end per more() iteration
    private List<Integer> items;
    private final java.util.Map<Long, int[]> open = new java.util.HashMap<>();  // (site, depth) -> {inst, start}
    private int nextInst;
    /** Plan mode: (op, value, isBytes) for each top-level draw. */
    public interface Log {
        void draw(String op, Object value, boolean isBytes);
    }
    private Log log;

    public Source(byte[] choices) {
        this(choices, choices.length, false, 0, null, null);
    }

    public Source(byte[] choices, int cap, boolean gen, long seed, List<byte[]> dict, List<Integer> spans) {
        this.buf = choices.clone();
        this.len = choices.length;
        this.cap = Math.max(cap, len);
        this.gen = gen;
        this.rngState = seed;
        this.dict = dict == null ? new ArrayList<>() : dict;
        this.spans = spans;
    }

    public void setLog(Log l) {
        this.log = l;
    }

    /** Record more() loop iterations into `items` (site, instance, start, end). */
    public void recordItems(List<Integer> items) {
        this.items = items;
    }

    // ------------------------------------------------------------------ decoders by name
    private static final java.util.Map<String, java.lang.reflect.Method> DECODERS = new java.util.concurrent.ConcurrentHashMap<>();
    private static volatile boolean scanned;

    /** Called by the runtime for each {@code @Decoder} method. */
    public static void registerDecoder(String type, java.lang.reflect.Method m) {
        DECODERS.put(type, m);
    }

    static java.lang.reflect.Method decoder(String type) {
        java.lang.reflect.Method m = DECODERS.get(type);
        if (m == null && !scanned) {
            scanned = true;  // standalone reproducers: find @Decoder methods in the harness classes
            String app = System.getProperty("stitch.app");
            if (app != null) {
                try {
                    for (String l : java.nio.file.Files.readAllLines(java.nio.file.Path.of(app, "harness-classes.txt"))) {
                        if (l.isBlank()) continue;
                        try {
                            for (java.lang.reflect.Method x : Class.forName(l.split("\t")[0]).getDeclaredMethods()) {
                                Decoder d = x.getAnnotation(Decoder.class);
                                if (d != null) {
                                    x.setAccessible(true);
                                    DECODERS.putIfAbsent(d.value(), x);
                                }
                            }
                        } catch (Throwable t) {
                            // not loadable here
                        }
                    }
                } catch (java.io.IOException e) {
                    // no harness list
                }
            }
            m = DECODERS.get(type);
        }
        if (m == null) throw new IllegalArgumentException("no @Decoder(\"" + type + "\")");
        return m;
    }

    /** The value of data type `type`'s decoder, drawn from this source: decoders build on each other. */
    @SuppressWarnings("unchecked")
    public <T> T draw(String type) {
        try {
            return (T) decoder(type).invoke(null, this);
        } catch (java.lang.reflect.InvocationTargetException e) {
            Throwable c = e.getCause();
            if (c instanceof RuntimeException) throw (RuntimeException) c;
            if (c instanceof Error) throw (Error) c;
            throw new RuntimeException(c);
        } catch (IllegalAccessException e) {
            throw new RuntimeException(e);
        }
    }

    // ------------------------------------------------------------------ low level
    private long rnd() {
        long z = (rngState += 0x9E3779B97F4A7C15L);
        z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
        z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
        return z ^ (z >>> 31);
    }

    private double unit() {
        return (rnd() >>> 11) * (1.0 / 9007199254740992.0);
    }

    private long mod(long a, long n) {
        return Long.remainderUnsigned(a, n);
    }

    private void append(byte[] b, int n) {
        if (len + n > buf.length) {
            byte[] nb = new byte[Math.max(len + n, buf.length * 2 + 16)];
            System.arraycopy(buf, 0, nb, 0, len);
            buf = nb;
        }
        System.arraycopy(b, 0, buf, len, n);
        len += n;
    }

    private interface Synth {
        byte[] make();
    }

    private byte[] draw(int n, Synth synth) {
        byte[] out = new byte[n];
        if (pos + n <= len) {
            System.arraycopy(buf, pos, out, 0, n);
            pos += n;
            return out;
        }
        if (gen && len + n <= cap) {
            len = pos;
            byte[] f = synth.make();
            System.arraycopy(f, 0, out, 0, Math.min(n, f.length));
            append(out, n);
            pos += n;
            return out;
        }
        if (pos < len) System.arraycopy(buf, pos, out, 0, len - pos);
        pos = len;
        exhausted = true;
        return out;
    }

    private static long le(byte[] b) {
        long v = 0;
        for (int i = b.length - 1; i >= 0; i--) v = (v << 8) | (b[i] & 0xff);
        return v;
    }

    private static byte[] toLe(long v, int n) {
        byte[] b = new byte[n];
        for (int i = 0; i < n; i++) {
            b[i] = (byte) v;
            v >>>= 8;
        }
        return b;
    }

    private long interesting(int width) {
        long mask = width >= 8 ? -1L : (1L << (8 * width)) - 1;
        long r = mod(rnd(), 4);
        if (r == 0) return mask;
        if (r == 1) return mask >>> 1;
        return INTERESTING[(int) mod(rnd(), INTERESTING.length)] & mask;
    }

    private long uint(int width) {
        return le(draw(width, () -> toLe(unit() < 0.3 ? interesting(width) : rnd(), width)));
    }

    private static int lenPrefix(int max) {
        return max < 256 ? 1 : max < 65536 ? 2 : 4;
    }

    private int synthLen(int max) {
        double u = unit();
        long l = u < 0.7 ? mod(rnd(), 17) : u < 0.95 ? mod(rnd(), 65) : mod(rnd(), max + 1L);
        return (int) Math.min(l, max);
    }

    private byte[] blob(int max, boolean useDict) {
        int w = lenPrefix(max);
        int p0 = pos;
        byte[] out = blobInner(max, useDict, w);
        if (spans != null && p0 + w <= len) {
            spans.add(p0);
            spans.add(w);
            spans.add(out.length);
        }
        return out;
    }

    private byte[] blobInner(int max, boolean useDict, int w) {
        byte[][] content = {null};
        long raw = le(draw(w, () -> {
            byte[] c;
            if (useDict && !dict.isEmpty() && unit() < 0.5) {
                byte[] t = dict.get((int) mod(rnd(), dict.size()));
                c = java.util.Arrays.copyOf(t, Math.min(t.length, max));
            } else {
                int l = synthLen(max);
                boolean printable = unit() < 0.7;
                c = new byte[l];
                for (int i = 0; i < l; i++) c[i] = (byte) (printable ? 32 + mod(rnd(), 95) : rnd());
            }
            content[0] = c;
            return toLe(c.length, w);
        }));
        int ln = (int) (Long.compareUnsigned(raw, max) <= 0 ? raw : mod(raw, max + 1L));
        if (content[0] != null) {
            if (len + ln <= cap) {
                append(content[0], ln);
                pos += ln;
                return java.util.Arrays.copyOf(content[0], ln);
            }
            exhausted = true;
            return new byte[0];
        }
        int n = Math.min(ln, Math.max(0, len - pos));
        byte[] out = java.util.Arrays.copyOfRange(buf, pos, pos + n);
        pos += n;
        if (n < ln) exhausted = true;
        return out;
    }

    private long rangeImpl(long lo, long hi) {
        if (hi < lo) {
            long t = lo;
            lo = hi;
            hi = t;
        }
        final long flo = lo, fhi = hi;
        long span = hi - lo + 1;
        long raw = le(draw(8, () -> {
            double u = unit();
            long v = u < 0.05 ? flo : u < 0.1 ? fhi : flo + (span != 0 ? mod(rnd(), span) : rnd());
            return toLe(v, 8);
        }));
        if (raw >= lo && raw <= hi) return raw;
        long off = raw - lo;
        return lo + (span != 0 ? mod(off, span) : off);
    }

    private <T> T logged(java.util.function.Supplier<String> op, T v, boolean isBytes) {
        if (log != null) log.draw(op.get(), v, isBytes);
        return v;
    }

    // ------------------------------------------------------------------ public draws
    public int u8() { return logged(() -> "u8()", (int) uint(1), false); }
    public int u16() { return logged(() -> "u16()", (int) uint(2), false); }
    public long u32() { return logged(() -> "u32()", uint(4), false); }
    public long u64() { return logged(() -> "u64()", uint(8), false); }
    public byte i8() { return logged(() -> "i8()", (byte) uint(1), false); }
    public short i16() { return logged(() -> "i16()", (short) uint(2), false); }
    public int i32() { return logged(() -> "i32()", (int) uint(4), false); }
    public long i64() { return logged(() -> "i64()", uint(8), false); }
    public float f32() { return logged(() -> "f32()", Float.intBitsToFloat((int) uint(4)), false); }
    public double f64() { return logged(() -> "f64()", Double.longBitsToDouble(uint(8)), false); }
    public long range(long lo, long hi) { return logged(() -> "range(" + lo + ", " + hi + ")", rangeImpl(lo, hi), false); }
    public int choice(int n) { return logged(() -> "choice(" + n + ")", n <= 0 ? 0 : (int) rangeImpl(0, n - 1), false); }

    @SafeVarargs
    public final <T> T pick(T... items) {
        if (items.length == 0) throw new IllegalArgumentException("pick() from an empty list");
        int i = (int) rangeImpl(0, items.length - 1);
        return logged(() -> "pick(" + items.length + " items) -> #" + i, items[i], false);
    }

    public <T> T pick(List<T> items) {
        if (items.isEmpty()) throw new IllegalArgumentException("pick() from an empty list");
        int i = (int) rangeImpl(0, items.size() - 1);
        return logged(() -> "pick(" + items.size() + " items) -> #" + i, items.get(i), false);
    }

    public boolean coin(double p) {
        double thr = p * 256.0;
        int b = draw(1, () -> {
            boolean yes = unit() < p;
            long r = rnd();
            if (yes) return new byte[] {(byte) (thr >= 1 ? mod(r, (long) Math.min(256.0, thr)) : 0)};
            return new byte[] {(byte) Math.min(255.0, thr + mod(r, (long) Math.max(1.0, 256.0 - thr)))};
        })[0] & 0xff;
        // a full or exhausted buffer ends coin-driven loops, like more()
        return logged(() -> "coin(" + p + ")", b < thr && !exhausted, false);
    }

    public boolean coin() {
        return coin(0.5);
    }

    public boolean more() {
        int at = Math.min(pos, len);
        boolean v = false;
        if (!(exhausted && !gen)) {
            int b = draw(1, () -> {
                double fill = cap > 0 ? (double) len / cap : 1.0;
                double p = fill > 0.5 ? 0.15 : 0.65;
                return new byte[] {(byte) ((rnd() & 0xfe) | (unit() < p ? 1 : 0))};
            })[0] & 0xff;
            v = (b & 1) == 1 && !exhausted;
        }
        if (items != null) {
            // the loop: the caller's source line, and the stack depth (recursion level)
            StackWalker.StackFrame f = StackWalker.getInstance().walk(s -> s.skip(1).findFirst()).orElse(null);
            int site = f == null ? 0 : (f.getClassName() + "." + f.getMethodName() + ":" + f.getLineNumber()).hashCode();
            long depth = StackWalker.getInstance().walk(s -> s.count());
            loopItem(site, depth, at, v);
        }
        final boolean fv = v;
        return logged(() -> "more()", fv, false);
    }

    private void loopItem(int site, long depth, int p, boolean v) {
        long key = ((long) site << 32) ^ depth;
        int[] st = open.get(key);
        if (st == null) {
            st = new int[] {nextInst++, -1, site};
            open.put(key, st);
        }
        if (st[1] >= 0) java.util.Collections.addAll(items, site, st[0], st[1], p);
        if (v) st[1] = p;
        else {
            java.util.Collections.addAll(items, site, st[0], p, p);
            open.remove(key);
        }
    }

    /** End of decoding: close the loops still open. */
    public void finishItems() {
        for (int[] st : open.values()) {
            if (st[1] >= 0) java.util.Collections.addAll(items, st[2], st[0], st[1], len);
            java.util.Collections.addAll(items, st[2], st[0], len, len);
        }
        open.clear();
    }

    /** A finite double in [lo, hi]: the 8 bytes are the value itself when it is in range. */
    public double f64(double lo, double hi) {
        if (!(lo <= hi)) {
            double t = lo;
            lo = hi;
            hi = t;
        }
        final double flo = lo, fhi = hi;
        long bits = le(draw(8, () -> {
            double u = unit();
            double x = u < 0.08 ? flo : u < 0.16 ? fhi : u < 0.22 && flo <= 0 && 0 <= fhi ? 0 : flo + (fhi - flo) * unit();
            if (!(x >= flo && x <= fhi)) x = flo;
            return toLe(Double.doubleToRawLongBits(x), 8);
        }));
        double v = Double.longBitsToDouble(bits);
        if (!(Double.isFinite(v) && v >= lo && v <= hi)) {
            double p = (bits >>> 11) * (1.0 / 9007199254740992.0);
            double w = hi - lo;
            v = Double.isFinite(w) ? lo + w * p : p < 0.5 ? lo + (hi / 2 - lo / 2) * (2 * p) : lo / 2 + hi / 2 + (hi / 2 - lo / 2) * (2 * p - 1);
            v = Math.min(hi, Math.max(lo, v));
        }
        final double fv = v;
        return logged(() -> "f64(" + flo + ", " + fhi + ")", fv, false);
    }

    /** A finite float in [lo, hi]. */
    public float f32(float lo, float hi) {
        if (!(lo <= hi)) {
            float t = lo;
            lo = hi;
            hi = t;
        }
        final float flo = lo, fhi = hi;
        int bits = (int) le(draw(4, () -> {
            double u = unit();
            float x = u < 0.08 ? flo : u < 0.16 ? fhi : (float) (flo + ((double) fhi - flo) * unit());
            if (!(x >= flo && x <= fhi)) x = flo;
            return toLe(Float.floatToRawIntBits(x), 4);
        }));
        float v = Float.intBitsToFloat(bits);
        if (!(Float.isFinite(v) && v >= lo && v <= hi)) {
            double p = (bits >>> 8) * (1.0 / 16777216.0);
            v = (float) Math.min(hi, Math.max(lo, lo + ((double) hi - lo) * p));
        }
        final float fv = v;
        return logged(() -> "f32(" + flo + ", " + fhi + ")", fv, false);
    }

    /** Exactly n bytes (zeros past the end of the input). */
    public byte[] fixed(int n) {
        if (n <= 0) return logged(() -> "fixed(" + n + ")", new byte[0], true);
        byte[] v = draw(n, () -> {
            boolean pr = unit() < 0.5;
            byte[] b = new byte[n];
            for (int i = 0; i < n; i++) b[i] = (byte) (pr ? 32 + mod(rnd(), 95) : rnd());
            return b;
        });
        return logged(() -> "fixed(" + n + ")", v, true);
    }

    /** Every byte that is left. */
    public byte[] rest() {
        int p0 = pos;
        if (gen && pos >= len && len < cap) {
            int room = cap - len;
            int l = Math.min(synthLen(room), room);
            boolean pr = unit() < 0.7;
            byte[] b = new byte[l];
            for (int i = 0; i < l; i++) b[i] = (byte) (pr ? 32 + mod(rnd(), 95) : rnd());
            len = pos;
            append(b, l);
        }
        byte[] v = java.util.Arrays.copyOfRange(buf, Math.min(pos, len), len);
        pos = len;
        if (spans != null) {
            spans.add(p0);
            spans.add(0);  // no length prefix
            spans.add(v.length);
        }
        return logged(() -> "rest()", v, true);
    }

    public byte[] bytes(int max) {
        return logged(() -> "bytes(" + max + ")", blob(max, false), true);
    }

    public String str(int max) {
        byte[] v = blob(max, false);
        logged(() -> "str(" + max + ")", v, true);
        return new String(v, StandardCharsets.UTF_8);
    }

    public String token() {
        byte[] v = blob(255, true);
        logged(() -> "token()", v, true);
        return new String(v, StandardCharsets.UTF_8);
    }

    public int position() { return pos; }
    public int remaining() { return Math.max(0, len - pos); }
    public boolean exhausted() { return exhausted; }

    /** The choice buffer (after generation). */
    public byte[] buffer() {
        return java.util.Arrays.copyOf(buf, len);
    }
}
