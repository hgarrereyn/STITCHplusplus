package stitch.rt;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.lang.reflect.Parameter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import stitch.Decoder;
import stitch.Drop;
import stitch.Endpoint;
import stitch.Init;
import stitch.Obj;
import stitch.Source;
import stitch.Take;

/** Stubs, object types and data types found in the harness classes. */
final class Registry {
    static final class ObjType {
        final String name;
        Method drop;

        ObjType(String name) {
            this.name = name;
        }
    }

    static final class Datatype {
        final String name;
        Method decoder;
        final List<byte[]> dict = new ArrayList<>();

        Datatype(String name) {
            this.name = name;
        }
    }

    static final class Arg {
        final String name;
        final ObjType obj; // object input, or null for a fuzzed parameter
        final boolean take;
        final Param param;

        Arg(String name, ObjType obj, boolean take, Param param) {
            this.name = name;
            this.obj = obj;
            this.take = take;
            this.param = param;
        }
    }

    static final class Stub {
        String name, file;
        Method method;
        List<Arg> args = new ArrayList<>();
        ObjType output; // null: no output
        boolean optional;
        Class<? extends Throwable>[] bailsOn;

        List<Arg> inputs() {
            List<Arg> l = new ArrayList<>();
            for (Arg a : args) if (a.obj != null) l.add(a);
            return l;
        }

        List<Arg> params() {
            List<Arg> l = new ArrayList<>();
            for (Arg a : args) if (a.obj == null) l.add(a);
            return l;
        }
    }

    final List<ObjType> objects = new ArrayList<>();
    final Map<String, Datatype> datatypes = new LinkedHashMap<>();
    final List<Stub> stubs = new ArrayList<>();
    final Map<String, Stub> byName = new HashMap<>();
    final List<Method> inits = new ArrayList<>();
    final Map<String, String> harnessFiles = new HashMap<>(); // class name -> harness/... path

    ObjType object(String name) {
        for (ObjType o : objects) if (o.name.equals(name)) return o;
        ObjType o = new ObjType(name);
        objects.add(o);
        return o;
    }

    Datatype datatype(String name) {
        return datatypes.computeIfAbsent(name, Datatype::new);
    }

    List<Datatype> dtList() {
        return new ArrayList<>(datatypes.values());
    }

    static String typeName(Class<?> t, Obj o) {
        if (o != null) return o.value();
        return t.isArray() ? t.getComponentType().getSimpleName() + "Array" : t.getSimpleName();
    }

    void scan(Class<?> c) {
        Method[] ms = c.getDeclaredMethods();
        Arrays.sort(ms, Comparator.comparing(Method::getName).thenComparing(Method::toString));
        for (Method m : ms) {
            if (m.isAnnotationPresent(Decoder.class)) {
                need(Modifier.isStatic(m.getModifiers()) && m.getParameterCount() == 1 && m.getParameterTypes()[0] == Source.class,
                    m, "@Decoder must be public static T name(Source s)");
                m.setAccessible(true);
                datatype(m.getAnnotation(Decoder.class).value()).decoder = m;
                Source.registerDecoder(m.getAnnotation(Decoder.class).value(), m);
            }
            if (m.isAnnotationPresent(Init.class)) {
                need(Modifier.isStatic(m.getModifiers()) && m.getParameterCount() == 0, m, "@Init must be static with no parameters");
                m.setAccessible(true);
                inits.add(m);
            }
        }
        for (Method m : ms) {
            Endpoint e = m.getAnnotation(Endpoint.class);
            if (e == null) continue;
            need(Modifier.isStatic(m.getModifiers()), m, "stubs must be static methods");
            m.setAccessible(true);
            Stub s = new Stub();
            s.method = m;
            s.name = e.name().isEmpty() ? m.getName() : e.name();
            s.file = harnessFiles.getOrDefault(c.getName(), "harness/" + c.getName().replace('.', '/') + ".java");
            s.bailsOn = e.bailsOn();
            s.optional = e.optional();
            for (Parameter p : m.getParameters()) {
                String an = p.isNamePresent() ? p.getName() : p.getName();
                if (Param.isData(p.getType()) || p.isAnnotationPresent(stitch.Val.class)) {
                    Param q = Param.of(p, an);
                    if (q.datatype != null) datatype(q.datatype);
                    s.args.add(new Arg(an, null, false, q));
                } else {
                    s.args.add(new Arg(an, object(typeName(p.getType(), p.getAnnotation(Obj.class))), p.isAnnotationPresent(Take.class), null));
                }
            }
            if (m.getReturnType() != void.class) {
                need(!Param.isData(m.getReturnType()), m, "a stub returns an object (or void), not a " + m.getReturnType().getSimpleName());
                s.output = object(typeName(m.getReturnType(), m.getAnnotation(Obj.class)));
            }
            if (byName.containsKey(s.name)) throw new IllegalStateException("duplicate stub name '" + s.name + "' (" + c.getName() + ")");
            stubs.add(s);
            byName.put(s.name, s);
        }
        for (Method m : ms) {
            Drop d = m.getAnnotation(Drop.class);
            if (d == null) continue;
            need(Modifier.isStatic(m.getModifiers()) && m.getParameterCount() == 1, m, "@Drop must be static void name(T obj)");
            m.setAccessible(true);
            object(d.value().isEmpty() ? typeName(m.getParameterTypes()[0], m.getParameters()[0].getAnnotation(Obj.class)) : d.value()).drop = m;
        }
    }

    private static void need(boolean ok, Method m, String msg) {
        if (!ok) throw new IllegalStateException(m.getDeclaringClass().getName() + "." + m.getName() + ": " + msg);
    }

    String manifest() {
        StringBuilder b = new StringBuilder("{\"version\":2,\"objects\":[");
        for (int i = 0; i < objects.size(); i++) {
            b.append(i > 0 ? "," : "").append("{\"name\":").append(Json.str(objects.get(i).name)).append(",\"drop\":")
                .append(objects.get(i).drop != null).append("}");
        }
        b.append("],\"datatypes\":[");
        int k = 0;
        for (Datatype d : datatypes.values()) {
            b.append(k++ > 0 ? "," : "").append("{\"name\":").append(Json.str(d.name)).append(",\"decoder\":").append(d.decoder != null).append("}");
        }
        b.append("],\"endpoints\":[");
        for (int i = 0; i < stubs.size(); i++) {
            Stub s = stubs.get(i);
            b.append(i > 0 ? "," : "").append("{\"name\":").append(Json.str(s.name)).append(",\"file\":").append(Json.str(s.file)).append(",\"inputs\":[");
            int n = 0;
            for (Arg a : s.inputs()) {
                b.append(n++ > 0 ? "," : "").append("{\"type\":").append(Json.str(a.obj.name)).append(",\"mode\":\"").append(a.take ? "take" : "borrow")
                    .append("\",\"name\":").append(Json.str(a.name)).append("}");
            }
            b.append("],\"params\":[");
            n = 0;
            for (Arg a : s.params()) b.append(n++ > 0 ? "," : "").append(a.param.manifest());
            b.append("],\"outputs\":[");
            if (s.output != null) b.append("{\"type\":").append(Json.str(s.output.name)).append(",\"optional\":").append(s.optional).append("}");
            b.append("]}");
        }
        return b.append("]}").toString();
    }
}
