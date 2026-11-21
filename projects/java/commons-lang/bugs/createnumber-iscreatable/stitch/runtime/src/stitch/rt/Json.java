package stitch.rt;

/** Minimal JSON writing. */
final class Json {
    private Json() {}

    static String str(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                default:
                    if (c < 0x20) b.append(String.format("\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }

    static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format("%02x", x & 0xff));
        return s.toString();
    }

    /** A Java source literal for a recorded value (standalone reproducers), or null. */
    static String javaLiteral(Object v) {
        if (v == null) return "null";
        if (v instanceof String) return str((String) v);
        if (v instanceof Boolean) return v.toString();
        if (v instanceof Integer) return v.toString();
        if (v instanceof Long) return v + "L";
        if (v instanceof Short) return "(short) " + v;
        if (v instanceof Byte) return "(byte) " + v;
        if (v instanceof Character) return "(char) " + (int) (Character) v;
        if (v instanceof Double) {
            double d = (Double) v;
            return Double.isNaN(d) ? "Double.NaN" : Double.isInfinite(d) ? (d > 0 ? "Double.POSITIVE_INFINITY" : "Double.NEGATIVE_INFINITY")
                : Double.toString(d);
        }
        if (v instanceof Float) {
            float f = (Float) v;
            return Float.isNaN(f) ? "Float.NaN" : Float.isInfinite(f) ? (f > 0 ? "Float.POSITIVE_INFINITY" : "Float.NEGATIVE_INFINITY")
                : f + "f";
        }
        if (v instanceof Enum) return ((Enum<?>) v).getDeclaringClass().getCanonicalName() + "." + ((Enum<?>) v).name();
        return null;
    }
}
