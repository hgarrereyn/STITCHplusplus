package stitch.rt;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.io.Writer;
import java.lang.reflect.InvocationTargetException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import stitch.Bail;
import stitch.CheckFailed;
import stitch.Source;

/** Program execution: a port of languages/cpp/runtime/src/runtime.cpp. */
public final class Runtime {
    public static final Runtime RT = new Runtime();
    static final int ST_OK = 1, ST_BAIL = 2, ST_SKIP_MISSING = 3, ST_SKIP_INVALID = 4;
    static final int MAX_NODES = 256, STRINGS_SIZE = 65536;

    static final class Finding extends RuntimeException {
        Finding() {
            super(null, null, false, false);
        }
    }

    static final class Slot {
        Registry.ObjType type;
        Object value;
        boolean alive = true;
        int gen, node, out;
        Map<String, Object> attrs = new HashMap<>();
    }

    static final class Ref {
        final int slot, gen;

        Ref(int slot, int gen) {
            this.slot = slot;
            this.gen = gen;
        }
    }

    Registry reg = new Registry();
    boolean trace;
    Writer plan;
    boolean inExec;
    int curNode = -1;
    final List<Slot> pool = new ArrayList<>();
    final Map<Long, Integer> objOf = new HashMap<>();
    public final Map<String, Object> globals = new HashMap<>();
    final List<Object[]> pending = new ArrayList<>();
    List<Integer> curInSlots = new ArrayList<>();
    int curConsumed;
    int lastMiss;
    int genCounter = 1;
    final List<String> tmpFiles = new ArrayList<>();
    final boolean excBail = "bail".equals(System.getenv("STITCH_EXCEPTIONS"));
    int planSrcNext;
    final PrintStream err = System.err;
    // standalone use (no program running): attributes keyed by identity
    private final Map<Object, Map<String, Object>> looseAttrs = new IdentityHashMap<>();

    // results
    int nNodes, current = -1;
    long crashPc;
    int[] status = new int[0], nOut = new int[0], outMask = new int[0], reason = new int[0], miss = new int[0], consumed = new int[0];
    final ByteArrayOutputStream strings = new ByteArrayOutputStream();
    final Set<Integer> seen = new HashSet<>();

    static int fnv1a(byte[] b) {
        int h = 0x811c9dc5;
        for (byte c : b) h = (h ^ (c & 0xff)) * 16777619;
        return h == 0 ? 1 : h;
    }

    void resetResults(int n) {
        nNodes = n;
        current = -1;
        crashPc = 0;
        status = new int[n];
        nOut = new int[n];
        outMask = new int[n];
        reason = new int[n];
        miss = new int[n];
        consumed = new int[n];
        strings.reset();
        seen.clear();
    }

    int intern(String s) {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        int h = fnv1a(b);
        if (seen.contains(h)) return h;
        int l = Math.min(b.length, 512);
        if (strings.size() + 6 + l <= STRINGS_SIZE) {
            ByteBuffer hd = ByteBuffer.allocate(6).order(ByteOrder.LITTLE_ENDIAN).putInt(h).putShort((short) l);
            strings.write(hd.array(), 0, 6);
            strings.write(b, 0, l);
            seen.add(h);
        }
        return h;
    }

    byte[] resultsBytes() {
        byte[] str = strings.toByteArray(), edges = Cov.edgesBytes(), cmps = Cov.cmpsBytes();
        ByteBuffer b = ByteBuffer.allocate(16 + 16 * nNodes + 4 + str.length + edges.length + cmps.length).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(nNodes).putInt(current).putLong(crashPc);
        for (int i = 0; i < nNodes; i++) {
            b.put((byte) status[i]).put((byte) nOut[i]).putShort((short) outMask[i]).putInt(reason[i]).putInt(miss[i]).putInt(consumed[i]);
        }
        b.putInt(str.length).put(str).put(edges).put(cmps);
        return b.array();
    }

    // ------------------------------------------------------------ objects + attributes
    Slot live(int i) {
        return i >= 0 && i < pool.size() && pool.get(i).alive ? pool.get(i) : null;
    }

    int slotOf(Object o) {
        for (int i = pool.size() - 1; i >= 0; i--) if (pool.get(i).alive && pool.get(i).value == o) return i;
        return -1;
    }

    public void noteMiss(String key) {
        if (inExec) lastMiss = intern(key);
    }

    private Object wrap(Object v) {
        if (v == null || v instanceof Number || v instanceof String || v instanceof Boolean) return v;
        int i = slotOf(v);
        return i >= 0 ? new Ref(i, pool.get(i).gen) : v;
    }

    private Object unwrap(Object v) {
        if (v instanceof Ref) {
            Slot s = live(((Ref) v).slot);
            return s != null && s.gen == ((Ref) v).gen ? s.value : null;
        }
        return v;
    }

    public void attrSet(Object obj, String key, Object value) {
        int i = slotOf(obj);
        if (i >= 0) pool.get(i).attrs.put(key, wrap(value));
        else if (inExec) pending.add(new Object[] {obj, key, wrap(value)});
        else looseAttrs.computeIfAbsent(obj, k -> new HashMap<>()).put(key, value);
    }

    public Object attrGet(Object obj, String key, boolean noteMissing) {
        int i = slotOf(obj);
        if (i >= 0 && pool.get(i).attrs.containsKey(key)) return unwrap(pool.get(i).attrs.get(key));
        if (i < 0) {
            for (int k = pending.size() - 1; k >= 0; k--) {
                Object[] p = pending.get(k);
                if (p[0] == obj && p[1].equals(key)) return unwrap(p[2]);
            }
            Map<String, Object> m = looseAttrs.get(obj);
            if (m != null && m.containsKey(key)) return m.get(key);
        }
        if (noteMissing) noteMiss(key);
        return null;
    }

    public boolean attrHas(Object obj, String key) {
        int i = slotOf(obj);
        if (i >= 0) return pool.get(i).attrs.containsKey(key);
        Map<String, Object> m = looseAttrs.get(obj);
        return m != null && m.containsKey(key);
    }

    public void attrDel(Object obj, String key) {
        int i = slotOf(obj);
        if (i >= 0) pool.get(i).attrs.remove(key);
        else if (looseAttrs.containsKey(obj)) looseAttrs.get(obj).remove(key);
    }

    public void consume(Object obj) {
        int i = slotOf(obj);
        for (int k = 0; k < curInSlots.size(); k++) if (curInSlots.get(k) == i) curConsumed |= 1 << k;
    }

    public void log(String msg) {
        if (trace) err.println("[stitch]     log: " + msg);
    }

    // ------------------------------------------------------------ params
    String tempFile(byte[] data) {
        try {
            String dir = System.getenv("STITCH_TMPDIR");
            Path p = dir == null || dir.isEmpty() ? Files.createTempFile("stitch-", "") : Files.createTempFile(Path.of(dir), "stitch-", "");
            Files.write(p, data);
            tmpFiles.add(p.toString());
            return p.toString();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    static final class UncheckedIOException extends RuntimeException {
        UncheckedIOException(IOException e) {
            super(e);
        }
    }

    Source newSource(byte[] buf, int cap) {
        Source s = new Source(buf, cap, false, 0, null, null);
        if (plan != null && inExec && curNode >= 0) {
            int sid = planSrcNext++, node = curNode;
            s.setLog((op, v, isBytes) -> planDraw(node, sid, op, v, isBytes));
        }
        return s;
    }

    byte[] decodeParam(String dt, byte[] raw, int form, int max) {
        Registry.Datatype d = dt == null ? null : reg.datatypes.get(dt);
        if (form == 1 && d != null && d.decoder != null) {
            return runDecoder(d, new Source(raw, Math.max(raw.length, max), false, 0, null, null), max);
        }
        return java.util.Arrays.copyOf(raw, Math.min(raw.length, max));
    }

    byte[] runDecoder(Registry.Datatype d, Source s, int max) {
        Object v;
        try {
            v = d.decoder.invoke(null, s);
        } catch (Throwable t) {
            v = new byte[0];
        }
        byte[] b = v instanceof byte[] ? (byte[]) v : String.valueOf(v).getBytes(StandardCharsets.UTF_8);
        return max >= 0 && b.length > max ? java.util.Arrays.copyOf(b, max) : b;
    }

    String describe(Param p, byte[] raw, int form) {
        switch (p.kind) {
            case "str": case "bytes": case "file": {
                byte[] v = p.rawValue(raw, form, this);
                return p.ctype + "[" + v.length + (form == 1 ? ",decoded" : "") + "]" + preview(v);
            }
            case "source":
                return "Src[" + raw.length + "]" + Json.hex(java.util.Arrays.copyOf(raw, Math.min(raw.length, 48))) + (raw.length > 48 ? "..." : "");
            default: {
                Object v = p.decode(raw, form, this);
                String lit = Json.javaLiteral(v);
                return lit != null ? lit : String.valueOf(v);
            }
        }
    }

    static String preview(byte[] b) {
        StringBuilder s = new StringBuilder("\"");
        for (int i = 0; i < b.length && i < 160; i++) {
            int c = b[i] & 0xff;
            if (c == '"' || c == '\\') s.append('\\').append((char) c);
            else if (c >= 32 && c < 127) s.append((char) c);
            else if (c == '\n') s.append("\\n");
            else s.append(String.format("\\x%02x", c));
        }
        return s.append('"').append(b.length > 160 ? "..." : "").toString();
    }

    // ------------------------------------------------------------ plan mode
    void planLine(String json) {
        if (plan == null) return;
        try {
            plan.write(json);
            plan.write('\n');
            plan.flush();
        } catch (IOException e) {
            plan = null;
        }
    }

    void planDraw(int node, int src, String op, Object v, boolean isBytes) {
        String val = isBytes ? ",\"hex\":\"" + Json.hex((byte[]) v) + "\"" : ",\"v\":" + Json.str(String.valueOf(v))
            + (Json.javaLiteral(v) != null ? ",\"java\":" + Json.str(Json.javaLiteral(v)) : "");
        planLine("{\"t\":\"draw\",\"node\":" + node + ",\"src\":" + src + ",\"op\":" + Json.str(op) + val + "}");
    }

    String planParam(Registry.Arg a, byte[] raw, int form) {
        Param p = a.param;
        StringBuilder b = new StringBuilder("{\"kind\":\"" + p.kind + "\",\"size\":" + p.size + ",\"name\":" + Json.str(a.name)
            + ",\"jtype\":" + Json.str(p.type.getCanonicalName()));
        switch (p.kind) {
            case "str": case "bytes": case "file":
                b.append(",\"decoded\":").append(form == 1).append(",\"hex\":\"").append(Json.hex(p.rawValue(raw, form, this))).append('"');
                break;
            case "source":
                b.append(",\"hex\":\"").append(Json.hex(java.util.Arrays.copyOf(raw, Math.min(raw.length, p.size)))).append('"');
                break;
            default: {
                Object v = p.decode(raw, form, this);
                b.append(",\"v\":").append(Json.str(String.valueOf(v)));
                String lit = Json.javaLiteral(v);
                if (lit != null) b.append(",\"java\":").append(Json.str(lit));
            }
        }
        if (p.datatype != null) b.append(",\"datatype\":").append(Json.str(p.datatype));
        return b.append('}').toString();
    }

    // ------------------------------------------------------------ program
    static final class Node {
        String name;
        int[][] ins;
        int[] forms;
        byte[][] params;
    }

    static List<Node> parse(byte[] data) {
        try {
            ByteBuffer b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
            if (data.length < 4 || data[0] != 'S' || data[1] != 'T' || data[2] != 'C' || data[3] != 'H') return null;
            b.position(4);
            int nn = b.getInt();
            if (nn < 0 || nn > 100000) return null;
            String[] names = new String[nn];
            for (int i = 0; i < nn; i++) {
                int l = b.getShort() & 0xffff;
                byte[] s = new byte[l];
                b.get(s);
                names[i] = new String(s, StandardCharsets.UTF_8);
            }
            int n = b.getInt();
            if (n < 0 || n > MAX_NODES) return null;
            List<Node> nodes = new ArrayList<>();
            for (int i = 0; i < n; i++) {
                Node nd = new Node();
                int ni = b.getInt();
                nd.name = ni >= 0 && ni < nn ? names[ni] : "?";
                int nin = b.get() & 0xff;
                nd.ins = new int[nin][2];
                for (int k = 0; k < nin; k++) {
                    nd.ins[k][0] = b.getInt();
                    nd.ins[k][1] = b.get() & 0xff;
                }
                int np = b.get() & 0xff;
                nd.forms = new int[np];
                nd.params = new byte[np][];
                for (int k = 0; k < np; k++) {
                    nd.forms[k] = b.get() & 0xff;
                    int l = b.getInt();
                    if (l < 0 || l > (16 << 20) || l > b.remaining()) return null;
                    nd.params[k] = new byte[l];
                    b.get(nd.params[k]);
                }
                nodes.add(nd);
            }
            return nodes;
        } catch (RuntimeException e) {
            return null;
        }
    }

    static String excName(Throwable t) {
        String n = t.getClass().getName();
        return n.startsWith("java.lang.") && n.indexOf('.', 10) < 0 ? n.substring(10) : n;
    }

    /** Innermost-first frames: (function, location), without runtime and reflection frames. */
    List<String[]> frames(Throwable t) {
        List<String[]> out = new ArrayList<>();
        for (StackTraceElement e : t.getStackTrace()) {
            String c = e.getClassName();
            if (c.startsWith("stitch.") || c.startsWith("jdk.internal.reflect") || c.startsWith("java.lang.reflect")
                || c.startsWith("sun.reflect") || c.startsWith("jdk.internal.")) continue;
            String simple = c.substring(c.lastIndexOf('.') + 1);
            String file = e.getFileName() == null ? simple + ".java" : e.getFileName();
            String h = reg.harnessFiles.get(c.contains("$") ? c.substring(0, c.indexOf('$')) : c);
            String loc = h != null ? "/project/" + h : "/" + (c.contains(".") ? c.substring(0, c.lastIndexOf('.')).replace('.', '/') + "/" : "") + file;
            out.add(new String[] {simple + "." + e.getMethodName(), loc + ":" + Math.max(0, e.getLineNumber())});
        }
        return out;
    }

    void report(String ep, Throwable t, String what) {
        List<String[]> fr = frames(t);
        if (what != null) {
            crashPc = (1L << 63) | (fnv1a(what.getBytes(StandardCharsets.UTF_8)) & 0xffffffffL);
            err.println("\n[stitch] CHECK FAILED: " + what + " (node " + curNode + ")");
        } else {
            String kind = excName(t);
            String msg = String.valueOf(t.getMessage()).replace('\n', ' ');
            if (msg.length() > 500) msg = msg.substring(0, 500);
            String top = "";
            for (String[] f : fr) {
                if (!f[1].startsWith("/project/harness")) {
                    top = f[1];
                    break;
                }
            }
            int idx = Math.max(0, reg.stubs.indexOf(reg.byName.get(ep)));
            crashPc = (1L << 63) | ((long) idx << 32) | (fnv1a((kind + "|" + top).getBytes(StandardCharsets.UTF_8)) & 0xffffffffL);
            err.println("\n[stitch] EXCEPTION in " + ep + ": " + kind + ": " + msg);
        }
        for (int k = 0; k < fr.size() && k < 24; k++) err.println("    #" + k + " 0x0 in " + fr.get(k)[0] + " " + fr.get(k)[1]);
        err.println();
        err.flush();
    }

    /** Returns "ok", "finding" or "invalid". */
    String run(byte[] data, boolean trace) {
        List<Node> nodes = parse(data);
        if (nodes == null) {
            resetResults(0);
            if (trace) err.println("[stitch] invalid program");
            return "invalid";
        }
        resetResults(nodes.size());
        pool.clear();
        objOf.clear();
        globals.clear();
        this.trace = trace;
        inExec = true;
        Cov.beginExec();
        try {
            return runNodes(nodes);
        } catch (Finding f) {
            return "finding";
        } finally {
            inExec = false;
            curNode = -1;
            Cov.cmplog = false;
            Cov.inStub = false;
            for (String f : tmpFiles) {
                try {
                    Files.deleteIfExists(Path.of(f));
                } catch (IOException e) {
                    // gone
                }
            }
            tmpFiles.clear();
        }
    }

    private String runNodes(List<Node> nodes) {
        for (int i = 0; i < nodes.size(); i++) {
            Node nd = nodes.get(i);
            current = i;
            curNode = i;
            Cov.node = i;
            Registry.Stub e = reg.byName.get(nd.name);
            if (e == null) {
                status[i] = ST_SKIP_INVALID;
                planLine("{\"t\":\"skip\",\"node\":" + i + ",\"why\":\"unknown endpoint\"}");
                if (trace) err.println("[stitch] #" + i + " " + nd.name + ": unknown endpoint, skipped");
                continue;
            }
            List<Registry.Arg> inputs = e.inputs(), params = e.params();
            nOut[i] = e.output == null ? 0 : 1;
            if (nd.ins.length != inputs.size() || nd.params.length != params.size()) {
                status[i] = ST_SKIP_INVALID;
                planLine("{\"t\":\"skip\",\"node\":" + i + ",\"why\":\"signature mismatch\"}");
                if (trace) err.println("[stitch] #" + i + " " + nd.name + ": signature mismatch, skipped");
                continue;
            }
            List<Integer> inSlot = new ArrayList<>();
            boolean missing = false;
            for (int k = 0; k < nd.ins.length; k++) {
                Integer si = objOf.get(((long) nd.ins[k][0] << 8) | nd.ins[k][1]);
                Slot s = si == null ? null : live(si);
                if (s == null || s.type != inputs.get(k).obj || inSlot.contains(si)) {
                    missing = true;
                    break;
                }
                inSlot.add(si);
            }
            if (missing) {
                status[i] = ST_SKIP_MISSING;
                planLine("{\"t\":\"skip\",\"node\":" + i + ",\"why\":\"input not available\"}");
                if (trace) err.println("[stitch] #" + i + " " + nd.name + ": input not available, skipped");
                continue;
            }
            if (trace) {
                StringBuilder line = new StringBuilder("[stitch] #" + i + " " + nd.name + "(");
                int oi = 0, pi = 0;
                for (int a = 0; a < e.args.size(); a++) {
                    Registry.Arg arg = e.args.get(a);
                    if (a > 0) line.append(", ");
                    if (arg.obj != null) {
                        int[] src = nd.ins[oi++];
                        line.append(arg.name).append('=').append(arg.take ? "take " : "").append('o').append(src[0]).append('.').append(src[1]);
                    } else {
                        line.append(arg.name).append('=').append(describe(arg.param, nd.params[pi], nd.forms[pi]));
                        pi++;
                    }
                }
                err.println(line.append(')'));
            }
            if (plan != null) {
                planCall(i, e, nd);
                planSrcNext = 0;
            }
            curInSlots = inSlot;
            curConsumed = 0;
            lastMiss = 0;
            pending.clear();
            String bailReason = null;
            Object result = null;
            try {
                Object[] vals = new Object[e.args.size()];
                int oi = 0, pi = 0;
                for (int a = 0; a < vals.length; a++) {
                    Registry.Arg arg = e.args.get(a);
                    if (arg.obj != null) vals[a] = pool.get(inSlot.get(oi++)).value;
                    else {
                        vals[a] = arg.param.decode(nd.params[pi], nd.forms[pi], this);
                        pi++;
                    }
                }
                Cov.inStub = true;
                try {
                    result = e.method.invoke(null, vals);
                } finally {
                    Cov.inStub = false;
                }
                status[i] = ST_OK;
            } catch (InvocationTargetException ite) {
                Throwable t = ite.getCause();
                if (t instanceof Bail) {
                    status[i] = ST_BAIL;
                    bailReason = ((Bail) t).reason;
                } else if (t instanceof CheckFailed) {
                    report(nd.name, t, ((CheckFailed) t).what);
                    throw new Finding();
                } else if (expected(e, t)) {
                    status[i] = ST_BAIL;
                    bailReason = "threw " + excName(t);
                } else if (excBail && t instanceof Exception) {
                    status[i] = ST_BAIL;
                    bailReason = "exception";
                } else {
                    report(nd.name, t, null);
                    throw new Finding();
                }
            } catch (IllegalAccessException | IllegalArgumentException x) {
                status[i] = ST_BAIL;
                bailReason = "cannot call stub: " + x;
            } catch (Bail b) {
                status[i] = ST_BAIL;
                bailReason = b.reason;
            }
            for (int k = 0; k < inSlot.size(); k++) {
                boolean take = inputs.get(k).take;
                if (take || (curConsumed & (1 << k)) != 0) {
                    Slot s = pool.get(inSlot.get(k));
                    s.alive = false;
                    s.value = null;
                    if (!take) consumed[i] |= 1 << k;
                }
            }
            if (status[i] == ST_OK && e.output != null) {
                if (result != null) {
                    Slot s = new Slot();
                    s.type = e.output;
                    s.value = result;
                    s.gen = genCounter++;
                    s.node = i;
                    for (Object[] p : pending) if (p[0] == result) s.attrs.put((String) p[1], p[2]);
                    pool.add(s);
                    objOf.put(((long) i << 8), pool.size() - 1);
                    outMask[i] = 1;
                } else if (!e.optional) {
                    status[i] = ST_BAIL;
                    bailReason = "returned null";
                }
            }
            if (status[i] == ST_BAIL) {
                reason[i] = intern(bailReason == null ? "bail" : bailReason);
                miss[i] = lastMiss;
            }
            if (plan != null) {
                StringBuilder d = new StringBuilder("{\"t\":\"done\",\"node\":" + i + ",\"status\":\"" + (status[i] == ST_OK ? "ok" : "bail") + "\"");
                if (status[i] == ST_BAIL) d.append(",\"reason\":").append(Json.str(bailReason == null ? "bail" : bailReason));
                d.append(",\"outs\":[").append(outMask[i] != 0 ? "0" : "").append("],\"consumed\":[");
                boolean first = true;
                for (int k = 0; k < inSlot.size(); k++) {
                    if ((consumed[i] & (1 << k)) != 0) {
                        d.append(first ? "" : ",").append(k);
                        first = false;
                    }
                }
                planLine(d.append("]}").toString());
            }
            if (trace) {
                if (status[i] == ST_OK) err.println("[stitch]     -> ok" + (outMask[i] != 0 ? " produced o" + i + ".0" : ""));
                else err.println("[stitch]     -> bail \"" + (bailReason == null ? "bail" : bailReason) + "\"");
            }
        }
        current = nodes.size();
        curNode = nodes.size();
        Cov.node = nodes.size();
        List<Slot> liveDrops = new ArrayList<>();
        for (int k = pool.size() - 1; k >= 0; k--) {
            Slot s = pool.get(k);
            if (s.alive && s.type.drop != null) liveDrops.add(s);
        }
        if (plan != null) {
            StringBuilder d = new StringBuilder("{\"t\":\"drops\",\"objs\":[");
            for (int k = 0; k < liveDrops.size(); k++) d.append(k > 0 ? "," : "").append('[').append(liveDrops.get(k).node).append(",0]");
            planLine(d.append("]}").toString());
        }
        for (Slot s : liveDrops) {
            try {
                s.type.drop.invoke(null, s.value);
            } catch (Throwable t) {
                // ignore
            }
        }
        pool.clear();
        current = -1;
        planLine("{\"t\":\"end\"}");
        return "ok";
    }

    private static boolean expected(Registry.Stub e, Throwable t) {
        for (Class<? extends Throwable> c : e.bailsOn) if (c.isInstance(t)) return true;
        return false;
    }

    private void planCall(int i, Registry.Stub e, Node nd) {
        StringBuilder b = new StringBuilder("{\"t\":\"call\",\"node\":" + i + ",\"ep\":" + Json.str(e.name) + ",\"args\":[");
        int oi = 0, pi = 0;
        for (int a = 0; a < e.args.size(); a++) {
            Registry.Arg arg = e.args.get(a);
            if (a > 0) b.append(',');
            if (arg.obj != null) {
                int[] src = nd.ins[oi++];
                b.append("{\"obj\":[").append(src[0]).append(',').append(src[1]).append("],\"type\":").append(Json.str(arg.obj.name))
                    .append(",\"take\":").append(arg.take).append(",\"name\":").append(Json.str(arg.name)).append('}');
            } else {
                b.append(planParam(arg, nd.params[pi], nd.forms[pi]));
                pi++;
            }
        }
        b.append("],\"outs\":[").append(e.output != null ? Json.str(e.output.name) : "").append("],\"multi\":false");
        b.append(",\"class\":").append(Json.str(e.method.getDeclaringClass().getName())).append(",\"method\":").append(Json.str(e.method.getName()));
        b.append(",\"bailsOn\":[");
        for (int k = 0; k < e.bailsOn.length; k++) b.append(k > 0 ? "," : "").append(Json.str(e.bailsOn[k].getName()));
        b.append(']');
        planLine(b.append('}').toString());
    }
}
