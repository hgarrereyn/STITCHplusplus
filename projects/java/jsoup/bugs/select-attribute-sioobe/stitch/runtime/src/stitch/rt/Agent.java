package stitch.rt;

import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.util.ArrayList;
import java.util.List;
import jdk.internal.org.objectweb.asm.ClassReader;
import jdk.internal.org.objectweb.asm.ClassVisitor;
import jdk.internal.org.objectweb.asm.ClassWriter;
import jdk.internal.org.objectweb.asm.Handle;
import jdk.internal.org.objectweb.asm.Label;
import jdk.internal.org.objectweb.asm.MethodVisitor;
import jdk.internal.org.objectweb.asm.Opcodes;

/**
 * Java agent: coverage counters and comparison hooks for the code under test (the Jazzer
 * approach, with the JDK's own copy of ASM). Classes in STITCH_JAVA_INCLUDE packages (comma
 * separated prefixes) and the harness get
 *   - Cov.hit(ID) at method entry and after each label (branch targets, line starts, handlers)
 *   - Cov.icmp before int compare-and-branch, Cov.lcmp instead of LCMP, Cov.sw before switches
 *   - Cov.str* / Cov.map* / Cov.set* instead of String.equals/startsWith/..., Map.get/containsKey
 *     and Set.contains
 * Instrumentation happens only while fuzzing (STITCH_FUZZING=1).
 */
public final class Agent {
    private Agent() {}

    static final String COV = "stitch/rt/Cov";
    private static final List<String> prefixes = new ArrayList<>();

    public static void premain(String args, Instrumentation inst) {
        if (!"1".equals(System.getenv("STITCH_FUZZING"))) return;
        for (String s : (System.getenv().getOrDefault("STITCH_JAVA_INCLUDE", "") + "," + System.getenv().getOrDefault("STITCH_JAVA_HARNESS", "")).split(",")) {
            s = s.trim();
            if (!s.isEmpty()) prefixes.add(s.replace('.', '/'));
        }
        inst.addTransformer(new ClassFileTransformer() {
            @Override
            public byte[] transform(ClassLoader loader, String name, Class<?> redefined, ProtectionDomain pd, byte[] bytes) {
                if (name == null || !wanted(name)) return null;
                try {
                    return instrument(name, bytes);
                } catch (Throwable t) {
                    if (System.getenv("STITCH_DEBUG") != null) System.err.println("[stitch] not instrumented: " + name + ": " + t);
                    return null;
                }
            }
        });
    }

    static boolean wanted(String name) {
        if (name.startsWith("stitch/") || name.startsWith("java/") || name.startsWith("jdk/") || name.startsWith("sun/")) return false;
        if (prefixes.isEmpty()) return true;
        for (String p : prefixes) {
            if (name.equals(p) || name.startsWith(p.endsWith("/") ? p : p + "/") || name.startsWith(p + "$")) return true;
        }
        return false;
    }

    static int hash(String s) {
        int h = 0x811c9dc5;
        for (int i = 0; i < s.length(); i++) h = (h ^ s.charAt(i)) * 16777619;
        return h & (Cov.MAP_SIZE - 1);
    }

    static byte[] instrument(String cls, byte[] bytes) {
        ClassReader cr = new ClassReader(bytes);
        ClassWriter cw = new ClassWriter(cr, ClassWriter.COMPUTE_MAXS);
        cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
            @Override
            public MethodVisitor visitMethod(int access, String mname, String desc, String sig, String[] ex) {
                MethodVisitor mv = super.visitMethod(access, mname, desc, sig, ex);
                if ((access & (Opcodes.ACC_ABSTRACT | Opcodes.ACC_NATIVE)) != 0) return mv;
                return new Instr(mv, cls + "." + mname + desc);
            }
        }, 0);
        return cw.toByteArray();
    }

    static final class Instr extends MethodVisitor {
        private final String where;
        private int site;
        private boolean pending = true; // emit a counter before the next instruction

        Instr(MethodVisitor mv, String where) {
            super(Opcodes.ASM9, mv);
            this.where = where;
        }

        private void flush() {
            if (!pending) return;
            pending = false;
            super.visitLdcInsn(hash(where + "#" + site++));
            super.visitMethodInsn(Opcodes.INVOKESTATIC, COV, "hit", "(I)V", false);
        }

        @Override
        public void visitLabel(Label l) {
            super.visitLabel(l);
            pending = true; // after the label (and its frame), before its first instruction
        }

        @Override
        public void visitInsn(int op) {
            flush();
            if (op == Opcodes.LCMP) {
                super.visitMethodInsn(Opcodes.INVOKESTATIC, COV, "lcmp", "(JJ)I", false);
                return;
            }
            super.visitInsn(op);
        }

        @Override
        public void visitIntInsn(int op, int operand) {
            flush();
            super.visitIntInsn(op, operand);
        }

        @Override
        public void visitVarInsn(int op, int var) {
            flush();
            super.visitVarInsn(op, var);
        }

        @Override
        public void visitTypeInsn(int op, String type) {
            if (op == Opcodes.NEW && pending) {
                // stack map frames refer to a NEW by the offset of its label: the counter goes after it
                super.visitTypeInsn(op, type);
                flush();
                return;
            }
            flush();
            super.visitTypeInsn(op, type);
        }

        @Override
        public void visitFieldInsn(int op, String owner, String name, String desc) {
            flush();
            super.visitFieldInsn(op, owner, name, desc);
        }

        @Override
        public void visitMethodInsn(int op, String owner, String name, String desc, boolean itf) {
            flush();
            String hook = null, hdesc = null;
            if (owner.equals("java/lang/String") && op == Opcodes.INVOKEVIRTUAL) {
                switch (name + desc) {
                    case "equals(Ljava/lang/Object;)Z": hook = "strEquals"; hdesc = "(Ljava/lang/String;Ljava/lang/Object;)Z"; break;
                    case "equalsIgnoreCase(Ljava/lang/String;)Z": hook = "strEqualsIgnoreCase"; hdesc = "(Ljava/lang/String;Ljava/lang/String;)Z"; break;
                    case "startsWith(Ljava/lang/String;)Z": hook = "strStartsWith"; hdesc = "(Ljava/lang/String;Ljava/lang/String;)Z"; break;
                    case "endsWith(Ljava/lang/String;)Z": hook = "strEndsWith"; hdesc = "(Ljava/lang/String;Ljava/lang/String;)Z"; break;
                    case "contains(Ljava/lang/CharSequence;)Z": hook = "strContains"; hdesc = "(Ljava/lang/String;Ljava/lang/CharSequence;)Z"; break;
                    case "indexOf(Ljava/lang/String;)I": hook = "strIndexOf"; hdesc = "(Ljava/lang/String;Ljava/lang/String;)I"; break;
                    case "compareTo(Ljava/lang/String;)I": hook = "strCompareTo"; hdesc = "(Ljava/lang/String;Ljava/lang/String;)I"; break;
                    default: break;
                }
            } else if (isMap(owner)) {
                if ((name + desc).equals("get(Ljava/lang/Object;)Ljava/lang/Object;")) {
                    hook = "mapGet";
                    hdesc = "(Ljava/util/Map;Ljava/lang/Object;)Ljava/lang/Object;";
                } else if ((name + desc).equals("containsKey(Ljava/lang/Object;)Z")) {
                    hook = "mapContainsKey";
                    hdesc = "(Ljava/util/Map;Ljava/lang/Object;)Z";
                }
            } else if (isSet(owner) && (name + desc).equals("contains(Ljava/lang/Object;)Z")) {
                hook = "setContains";
                hdesc = "(Ljava/util/Set;Ljava/lang/Object;)Z";
            }
            if (hook != null) super.visitMethodInsn(Opcodes.INVOKESTATIC, COV, hook, hdesc, false);
            else super.visitMethodInsn(op, owner, name, desc, itf);
        }

        private static boolean isMap(String o) {
            return o.equals("java/util/Map") || o.equals("java/util/HashMap") || o.equals("java/util/LinkedHashMap") || o.equals("java/util/TreeMap");
        }

        private static boolean isSet(String o) {
            return o.equals("java/util/Set") || o.equals("java/util/HashSet") || o.equals("java/util/LinkedHashSet") || o.equals("java/util/TreeSet");
        }

        @Override
        public void visitInvokeDynamicInsn(String name, String desc, Handle bsm, Object... args) {
            flush();
            super.visitInvokeDynamicInsn(name, desc, bsm, args);
        }

        @Override
        public void visitJumpInsn(int op, Label l) {
            flush();
            if (op >= Opcodes.IF_ICMPEQ && op <= Opcodes.IF_ICMPLE) {
                super.visitInsn(Opcodes.DUP2);
                super.visitMethodInsn(Opcodes.INVOKESTATIC, COV, "icmp", "(II)V", false);
            }
            super.visitJumpInsn(op, l);
            pending = true; // fall-through edge
        }

        @Override
        public void visitLdcInsn(Object v) {
            flush();
            super.visitLdcInsn(v);
        }

        @Override
        public void visitIincInsn(int var, int inc) {
            flush();
            super.visitIincInsn(var, inc);
        }

        @Override
        public void visitTableSwitchInsn(int min, int max, Label dflt, Label... labels) {
            flush();
            int[] keys = new int[Math.min(max - min + 1, 256)];
            for (int i = 0; i < keys.length; i++) keys[i] = min + i;
            switchHook(keys);
            super.visitTableSwitchInsn(min, max, dflt, labels);
        }

        @Override
        public void visitLookupSwitchInsn(Label dflt, int[] keys, Label[] labels) {
            flush();
            switchHook(keys.length > 256 ? java.util.Arrays.copyOf(keys, 256) : keys);
            super.visitLookupSwitchInsn(dflt, keys, labels);
        }

        private void switchHook(int[] keys) {
            int id = hash(where + "#sw" + site++);
            Cov.SWITCHES.put(id, keys);
            super.visitInsn(Opcodes.DUP);
            super.visitLdcInsn(id);
            super.visitMethodInsn(Opcodes.INVOKESTATIC, COV, "sw", "(II)V", false);
        }

        @Override
        public void visitMultiANewArrayInsn(String desc, int dims) {
            flush();
            super.visitMultiANewArrayInsn(desc, dims);
        }
    }
}
