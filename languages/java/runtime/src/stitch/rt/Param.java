package stitch.rt;

import java.lang.reflect.Parameter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import stitch.Bytes;
import stitch.FileParam;
import stitch.Flags;
import stitch.Max;
import stitch.OneOf;
import stitch.Range;
import stitch.Source;
import stitch.Str;

/** One fuzzed parameter: its manifest entry and how its bytes decode (same encoding as C++). */
final class Param {
    final String name;
    final Class<?> type;
    String kind; // int float bool range oneof flags bytes str file source
    String ctype;
    int size;
    boolean signed;
    long lo, hi;
    long[] values = new long[0];
    Object[] choices; // oneof over strings / enum constants: the engine sees indices
    long mask;
    String datatype;

    Param(String name, Class<?> type) {
        this.name = name;
        this.type = type;
    }

    static boolean isData(Class<?> t) {
        return t.isPrimitive() || t == String.class || t == byte[].class || t == Source.class || t == Path.class
            || t == java.io.File.class || t.isEnum() || t == Integer.class || t == Long.class || t == Short.class
            || t == Byte.class || t == Boolean.class || t == Double.class || t == Float.class || t == Character.class;
    }

    static Param of(Parameter p, String name) {
        Class<?> t = p.getType();
        Param q = new Param(name, t);
        Range r = p.getAnnotation(Range.class);
        OneOf o = p.getAnnotation(OneOf.class);
        Flags f = p.getAnnotation(Flags.class);
        stitch.Val val = p.getAnnotation(stitch.Val.class);
        if (val != null) {
            q.kind = "value";
            q.ctype = "Val";
            q.size = val.max();
            q.datatype = val.value();
        } else if (r != null) {
            q.kind = "range";
            q.ctype = "Range";
            q.size = 8;
            q.signed = true;
            q.lo = r.lo();
            q.hi = r.hi();
        } else if (o != null && o.strings().length > 0) {
            q.oneof(o.strings());
        } else if (o != null) {
            q.kind = "oneof";
            q.ctype = "OneOf";
            q.size = 8;
            q.values = o.ints();
        } else if (f != null) {
            q.kind = "flags";
            q.ctype = "Flags";
            q.size = 8;
            q.values = f.value();
            for (long x : q.values) q.mask |= x;
        } else if (t.isEnum()) {
            q.oneof(t.getEnumConstants());
        } else if (t == boolean.class || t == Boolean.class) {
            q.kind = "bool";
            q.ctype = "Bool";
            q.size = 1;
        } else if (t == double.class || t == Double.class || t == float.class || t == Float.class) {
            q.kind = "float";
            q.size = (t == float.class || t == Float.class) ? 4 : 8;
            q.ctype = q.size == 4 ? "F32" : "F64";
        } else if (t == String.class) {
            Str s = p.getAnnotation(Str.class);
            q.buf("str", "Str", s == null ? 256 : s.max(), s == null ? "" : s.type());
        } else if (t == byte[].class) {
            Bytes b = p.getAnnotation(Bytes.class);
            q.buf("bytes", "Bytes", b == null ? 256 : b.max(), b == null ? "" : b.type());
        } else if (t == Path.class || t == java.io.File.class) {
            FileParam b = p.getAnnotation(FileParam.class);
            q.buf("file", "File", b == null ? 4096 : b.max(), b == null ? "" : b.type());
        } else if (t == Source.class) {
            Max m = p.getAnnotation(Max.class);
            q.kind = "source";
            q.ctype = "Source";
            q.size = m == null ? 64 : m.value();
        } else {
            q.kind = "int";
            q.signed = t != char.class && t != Character.class;
            q.size = (t == byte.class || t == Byte.class) ? 1 : (t == short.class || t == Short.class || t == char.class
                || t == Character.class) ? 2 : (t == int.class || t == Integer.class) ? 4 : 8;
            q.ctype = (q.signed ? "I" : "U") + (8 * q.size);
        }
        return q;
    }

    private void oneof(Object[] c) {
        kind = "oneof";
        ctype = "OneOf";
        size = 8;
        choices = c;
        values = new long[c.length];
        for (int i = 0; i < c.length; i++) values[i] = i;
    }

    private void buf(String k, String ct, int max, String dt) {
        kind = k;
        ctype = ct;
        size = max;
        datatype = dt == null || dt.isEmpty() ? null : dt;
    }

    static long le(byte[] raw, int n) {
        long v = 0;
        for (int i = Math.min(raw.length, n) - 1; i >= 0; i--) v = (v << 8) | (raw[i] & 0xff);
        return v;
    }

    long rawInt(byte[] raw) {
        long v = le(raw, size);
        if (signed && size < 8 && ((v >>> (8 * size - 1)) & 1) == 1) v |= -1L << (8 * size);
        return v;
    }

    int oneofIndex(byte[] raw) {
        long v = le(raw, 8);
        int n = values.length;
        for (int i = 0; i < n; i++) if (values[i] == v) return i;
        return (int) Long.remainderUnsigned(v, n);
    }

    /** The Java value the stub receives. */
    Object decode(byte[] raw, int form, Runtime rt) {
        switch (kind) {
            case "bool":
                return raw.length > 0 && (raw[0] & 1) == 1;
            case "float":
                return size == 4 ? (Object) Float.intBitsToFloat((int) le(raw, 4)) : (Object) Double.longBitsToDouble(le(raw, 8));
            case "range": {
                long v = le(raw, 8);
                if (v < lo || v > hi) {
                    long span = hi - lo + 1;
                    v = lo + (span != 0 ? Long.remainderUnsigned(v - lo, span) : v - lo);
                }
                return box(v);
            }
            case "oneof": {
                int i = oneofIndex(raw);
                return choices != null ? choices[i] : box(values[i]);
            }
            case "flags":
                return box(le(raw, 8) & mask);
            case "str":
                return new String(rawValue(raw, form, rt), StandardCharsets.UTF_8);
            case "bytes":
                return rawValue(raw, form, rt);
            case "file": {
                String p = rt.tempFile(rawValue(raw, form, rt));
                return type == Path.class ? (Object) Path.of(p) : (Object) new java.io.File(p);
            }
            case "source":
                return rt.newSource(java.util.Arrays.copyOf(raw, Math.min(raw.length, size)), size);
            case "value":
                return rt.decodeValue(datatype, java.util.Arrays.copyOf(raw, Math.min(raw.length, size)), size);
            default:
                return box(rawInt(raw));
        }
    }

    byte[] rawValue(byte[] raw, int form, Runtime rt) {
        return rt.decodeParam(datatype, raw, form, size);
    }

    private Object box(long v) {
        if (type == int.class || type == Integer.class) return (int) v;
        if (type == short.class || type == Short.class) return (short) v;
        if (type == byte.class || type == Byte.class) return (byte) v;
        if (type == char.class || type == Character.class) return (char) v;
        return v;
    }

    String manifest() {
        StringBuilder b = new StringBuilder();
        b.append("{\"kind\":\"").append(kind).append("\",\"type\":\"").append(ctype).append("\",\"size\":").append(size)
            .append(",\"signed\":").append(signed);
        if (kind.equals("range")) b.append(",\"lo\":").append(lo).append(",\"hi\":").append(hi);
        if (values.length > 0) {
            b.append(",\"values\":[");
            for (int i = 0; i < values.length; i++) b.append(i > 0 ? "," : "").append(values[i]);
            b.append("]");
        }
        b.append(",\"datatype\":").append(datatype == null ? "null" : Json.str(datatype));
        b.append(",\"name\":").append(Json.str(name)).append("}");
        return b.toString();
    }
}
