package stitch;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;

/**
 * Runs a standalone reproducer (`stitch write`) without the fuzzer: each harness call with its
 * recorded arguments, crashes reported in the fuzzer's format.
 *
 * <pre>
 *   var doc0 = Standalone.call(0, "parse", () -> Stubs.parse("..."));   // succeeded originally
 *   Standalone.exec(1, "select", () -> Stubs.select(doc0, "a"));        // void stub
 *   Standalone.execBails(2, "remove", () -> ...);                         // bailed originally
 * </pre>
 */
public final class Standalone {
    private Standalone() {}

    public interface Call<T> {
        T run() throws Throwable;
    }

    public interface Exec {
        void run() throws Throwable;
    }

    private static int curNode = -1;
    private static String curName = "?";
    private static final boolean TRACE = System.getenv("STITCH_REPRO_TRACE") != null;

    private static void diverged(String why) {
        System.err.println("\n[repro] DIVERGED at call #" + curNode + " " + curName + ": " + why);
        System.exit(99);
    }

    public static <T> T call(int n, String name, Call<T> f) throws Throwable {
        curNode = n;
        curName = name;
        try {
            T r = f.run();
            if (TRACE) System.err.println("[repro] #" + n + " " + name + " -> ok");
            return r;
        } catch (Bail b) {
            diverged("bailed: \"" + b.reason + "\" (it succeeded in the original run)");
            return null;
        }
    }

    public static void exec(int n, String name, Exec f) throws Throwable {
        call(n, name, () -> {
            f.run();
            return null;
        });
    }

    /** A call that bailed in the original run: a bail or one of `expected` is fine. */
    @SafeVarargs
    public static void execBails(int n, String name, Exec f, Class<? extends Throwable>... expected) throws Throwable {
        curNode = n;
        curName = name;
        try {
            f.run();
        } catch (Bail b) {
            // as recorded
        } catch (Throwable t) {
            boolean ok = false;
            for (Class<? extends Throwable> c : expected) ok |= c.isInstance(t);
            if (!ok) throw t;
        }
        if (TRACE) System.err.println("[repro] #" + n + " " + name + " -> bail");
    }

    public static byte[] hex(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(h.substring(2 * i, 2 * i + 2), 16);
        return b;
    }

    public static Path file(byte[] data) throws java.io.IOException {
        Path p = Files.createTempFile("stitch-repro-", "");
        Files.write(p, data);
        return p;
    }

    /** Run the program; report a crash the way the fuzzer does. */
    public static void run(Exec main) {
        Set<String> harness = new HashSet<>();
        String app = System.getProperty("stitch.app");
        if (app != null) {
            try {
                for (String l : Files.readAllLines(Path.of(app, "harness-classes.txt"))) if (!l.isBlank()) harness.add(l.split("\t")[0]);
            } catch (java.io.IOException e) {
                // no harness list: harness frames count as library frames
            }
        }
        try {
            main.run();
        } catch (CheckFailed c) {
            System.err.println("\n[stitch] CHECK FAILED: " + c.what + " (node " + curNode + ")");
            System.exit(1);
        } catch (Throwable t) {
            String kind = t.getClass().getName();
            if (kind.startsWith("java.lang.") && kind.indexOf('.', 10) < 0) kind = kind.substring(10);
            String msg = String.valueOf(t.getMessage()).replace('\n', ' ');
            System.err.println("\n[stitch] EXCEPTION in " + curName + ": " + kind + ": " + msg);
            int k = 0;
            for (StackTraceElement e : t.getStackTrace()) {
                String c = e.getClassName();
                if (c.startsWith("stitch.") || c.startsWith("jdk.internal.") || c.startsWith("java.lang.reflect")) continue;
                String top = c.contains("$") ? c.substring(0, c.indexOf('$')) : c;
                String simple = c.substring(c.lastIndexOf('.') + 1);
                String file = e.getFileName() == null ? simple + ".java" : e.getFileName();
                String loc = harness.contains(top) || top.equals("Repro") ? "/standalone/" + file
                    : "/" + (c.contains(".") ? c.substring(0, c.lastIndexOf('.')).replace('.', '/') + "/" : "") + file;
                System.err.println("    #" + k++ + " 0x0 in " + simple + "." + e.getMethodName() + " " + loc + ":" + e.getLineNumber());
            }
            System.exit(1);
        }
        System.exit(0);
    }
}
