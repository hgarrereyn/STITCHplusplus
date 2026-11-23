package stitch.rt;

import java.io.BufferedWriter;
import java.io.DataInputStream;
import java.io.EOFException;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import stitch.Source;

/**
 * Entry points: the engine's worker, and the reproducer CLI.
 *
 * <pre>
 *   java stitch.rt.Main --app DIR [--trace] [--plan FILE] [--manifest] FILE.stp...   run programs
 *   java stitch.rt.Main --app DIR --each [--timeout S] [--files-from LIST] FILE...   run all (stitch cov)
 *   java stitch.rt.Main --app DIR worker                                             engine worker (fds 3, 4)
 * </pre>
 *
 * DIR holds the compiled harness (classes/) and harness-classes.txt (class TAB source path).
 */
public final class Main {
    private Main() {}

    static Runtime RT = Runtime.RT;

    static void loadHarness(Path app) throws Exception {
        List<String> lines = Files.readAllLines(app.resolve("harness-classes.txt"));
        List<Class<?>> classes = new ArrayList<>();
        for (String l : lines) {
            if (l.isBlank()) continue;
            String[] p = l.split("\t");
            RT.reg.harnessFiles.put(p[0], p.length > 1 ? p[1] : "");
        }
        ClassLoader cl = Main.class.getClassLoader();
        for (String l : lines) {
            if (l.isBlank()) continue;
            classes.add(Class.forName(l.split("\t")[0], true, cl));
        }
        for (Class<?> c : classes) RT.reg.scan(c);
        if (RT.reg.stubs.isEmpty()) throw new IllegalStateException("the harness defines no stubs (@Endpoint)");
        for (Method m : RT.reg.inits) m.invoke(null);
    }

    static String runProgram(byte[] data, boolean trace, boolean cmplog) {
        Cov.cmplog = cmplog;
        return RT.run(data, trace);
    }

    // ------------------------------------------------------------ worker
    private static byte[] readExact(DataInputStream in, int n) throws IOException {
        byte[] b = new byte[n];
        in.readFully(b);
        return b;
    }

    private static void send(OutputStream out, char tag, byte[] payload) throws IOException {
        ByteBuffer h = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN);
        h.put((byte) tag).putInt(payload.length);
        out.write(h.array());
        out.write(payload);
        out.flush();
    }

    static void worker(Path app) throws Exception {
        DataInputStream in = new DataInputStream(new java.io.BufferedInputStream(new FileInputStream("/proc/self/fd/3")));
        OutputStream out = new java.io.BufferedOutputStream(new FileOutputStream("/proc/self/fd/4"), 1 << 16);
        loadHarness(app);
        byte[] man = RT.reg.manifest().getBytes(StandardCharsets.UTF_8);
        send(out, 'H', ByteBuffer.allocate(4 + man.length).order(ByteOrder.LITTLE_ENDIAN).putInt(Cov.MAP_SIZE).put(man).array());
        List<Registry.Datatype> dts = RT.reg.dtList();
        while (true) {
            byte[] h;
            try {
                h = readExact(in, 5);
            } catch (EOFException e) {
                return;
            }
            int len = ByteBuffer.wrap(h, 1, 4).order(ByteOrder.LITTLE_ENDIAN).getInt();
            ByteBuffer p = ByteBuffer.wrap(readExact(in, len)).order(ByteOrder.LITTLE_ENDIAN);
            switch ((char) h[0]) {
                case 'X': {
                    int flags = p.getInt();
                    byte[] prog = new byte[len - 4];
                    p.get(prog);
                    String r = runProgram(prog, (flags & 1) != 0, (flags & 2) != 0);
                    send(out, r.equals("finding") ? 'C' : 'O', RT.resultsBytes());
                    break;
                }
                case 'D': {
                    int dt = p.getInt(), cap = p.getInt();
                    byte[] ch = new byte[len - 8];
                    p.get(ch);
                    Registry.Datatype d = dt < dts.size() ? dts.get(dt) : null;
                    byte[] v = d != null && d.decoder != null ? RT.runDecoder(d, new Source(ch, Math.max(ch.length, cap), false, 0, null, null), cap)
                        : java.util.Arrays.copyOf(ch, Math.min(ch.length, cap));
                    send(out, 'R', v);
                    break;
                }
                case 'G': {
                    int dt = p.getInt();
                    long seed = p.getLong();
                    int cap = p.getInt();
                    byte[] pre = new byte[Math.min(len - 16, cap)];
                    p.get(pre);
                    Registry.Datatype d = dt < dts.size() ? dts.get(dt) : null;
                    byte[] v = new byte[0];
                    if (d != null && d.decoder != null) {
                        Source s = new Source(pre, cap, true, seed, d.dict, null);
                        RT.runDecoder(d, s, -1);
                        v = s.buffer();
                        if (v.length > cap) v = java.util.Arrays.copyOf(v, cap);
                    }
                    send(out, 'R', v);
                    break;
                }
                case 'S': {
                    int dt = p.getInt();
                    byte[] ch = new byte[len - 4];
                    p.get(ch);
                    Registry.Datatype d = dt < dts.size() ? dts.get(dt) : null;
                    List<Integer> spans = new ArrayList<>();
                    if (d != null && d.decoder != null) RT.runDecoder(d, new Source(ch, ch.length, false, 0, null, spans), -1);
                    ByteBuffer b = ByteBuffer.allocate(4 * spans.size()).order(ByteOrder.LITTLE_ENDIAN);
                    for (int x : spans) b.putInt(x);
                    send(out, 'R', b.array());
                    break;
                }
                case 'I': {
                    int dt = p.getInt();
                    byte[] ch = new byte[len - 4];
                    p.get(ch);
                    Registry.Datatype d = dt < dts.size() ? dts.get(dt) : null;
                    List<Integer> items = new ArrayList<>();
                    if (d != null && d.decoder != null) {
                        Source s = new Source(ch, ch.length, false, 0, null, null);
                        s.recordItems(items);
                        RT.runDecoder(d, s, -1);
                        s.finishItems();
                    }
                    ByteBuffer b = ByteBuffer.allocate(4 * items.size()).order(ByteOrder.LITTLE_ENDIAN);
                    for (int x : items) b.putInt(x);
                    send(out, 'R', b.array());
                    break;
                }
                case 'T': {
                    int dt = p.getInt();
                    byte[] tok = new byte[len - 4];
                    p.get(tok);
                    if (dt < dts.size()) {
                        List<byte[]> dict = dts.get(dt).dict;
                        boolean dup = false;
                        for (byte[] x : dict) dup |= java.util.Arrays.equals(x, tok);
                        if (!dup && dict.size() < 4096) dict.add(tok);
                    }
                    break;
                }
                case 'P': {
                    if (RT.plan != null) RT.plan.close();
                    String f = new String(p.array(), StandardCharsets.UTF_8);
                    RT.plan = f.isEmpty() ? null : new BufferedWriter(new FileWriter(f));
                    break;
                }
                case 'Q':
                    if (RT.plan != null) RT.plan.close();
                    return;
                default:
                    break;
            }
        }
    }

    // ------------------------------------------------------------ reproducer
    static int repro(Path app, List<String> argv) throws Exception {
        boolean trace = argv.contains("--trace"), each = argv.contains("--each");
        List<String> files = new ArrayList<>();
        String plan = null;
        int timeout = 10;
        for (int i = 0; i < argv.size(); i++) {
            String a = argv.get(i);
            switch (a) {
                case "--plan": plan = argv.get(++i); break;
                case "--timeout": timeout = Integer.parseInt(argv.get(++i)); break;
                case "--app": i++; break;
                case "--files-from":
                    for (String l : Files.readAllLines(Path.of(argv.get(++i)))) if (!l.isBlank()) files.add(l.trim());
                    break;
                default:
                    if (!a.startsWith("-")) files.add(a);
            }
        }
        loadHarness(app);
        if (argv.contains("--manifest")) {
            System.out.println(RT.reg.manifest());
            return 0;
        }
        if (plan != null) RT.plan = new BufferedWriter(new FileWriter(plan));
        if (each) {
            int n = 0, ok = 0, crashed = 0, timeouts = 0, invalid = 0;
            ExecutorService ex = Executors.newSingleThreadExecutor(r -> {
                Thread t = new Thread(null, r, "stitch", 1L << 28);
                t.setDaemon(true);
                return t;
            });
            for (String f : files) {
                n++;
                byte[] data = Files.readAllBytes(Path.of(f));
                Future<String> fut = ex.submit(() -> runProgram(data, false, false));
                try {
                    String r = fut.get(timeout, TimeUnit.SECONDS);
                    if (r.equals("ok")) ok++;
                    else if (r.equals("finding")) crashed++;
                    else invalid++;
                } catch (TimeoutException e) {
                    timeouts++;
                    // a stuck program keeps its thread: continue on a fresh one
                    ex.shutdownNow();
                    ex = Executors.newSingleThreadExecutor(r -> {
                        Thread t = new Thread(null, r, "stitch", 1L << 28);
                        t.setDaemon(true);
                        return t;
                    });
                }
            }
            System.out.println("{\"programs\":" + n + ",\"ok\":" + ok + ",\"crashed\":" + crashed + ",\"timeouts\":" + timeouts
                + ",\"invalid\":" + invalid + "}");
            return 0;
        }
        for (String f : files) {
            if (trace) System.err.println("[stitch] running " + f);
            byte[] data;
            try {
                data = Files.readAllBytes(Path.of(f));
            } catch (IOException e) {
                System.err.println("[stitch] cannot open " + f + ": " + e.getMessage());
                return 2;
            }
            String r = runProgram(data, trace, false);
            if (r.equals("finding")) return 1;
            if (r.equals("invalid")) return 2;
        }
        if (RT.plan != null) RT.plan.close();
        return 0;
    }

    public static void main(String[] args) throws Exception {
        List<String> argv = new ArrayList<>(List.of(args));
        int ai = argv.indexOf("--app");
        Path app = Path.of(ai >= 0 ? argv.get(ai + 1) : ".");
        int code;
        try {
            if (argv.contains("worker")) {
                worker(app);
                code = 0;
            } else {
                code = repro(app, argv);
            }
        } catch (Registry.HarnessError e) {
            System.err.println("[stitch] error in the harness: " + e.getMessage());
            code = 2;
        }
        System.out.flush();
        System.err.flush();
        // library threads must not keep the JVM alive
        System.exit(code);
    }
}
