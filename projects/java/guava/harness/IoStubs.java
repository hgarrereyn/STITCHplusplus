import com.google.common.io.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import stitch.*;

/** Guava io: BaseEncoding variants (round trips, canDecode agrees with decode), streams, little-endian data, CharStreams. */
public class IoStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }


    @Endpoint
    public static void encoding_base64(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base64();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base64url(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base64Url();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base32(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base32();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base32hex(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base32Hex();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base16(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base16();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base64_nopad(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base64().omitPadding();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base32_lower(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base32().lowerCase();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base64_sep(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base64().withSeparator("\n", 4);
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base16_lower(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base16().lowerCase();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base32hex_sep_nopad(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base32Hex().omitPadding().withSeparator("-", 3);
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base64_pad_char(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base64().withPadChar('*');
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void encoding_base32_ignore_case(@Bytes(max = 96) byte[] data, @Str(max = 64) String garbage) throws IOException {
        BaseEncoding enc = BaseEncoding.base32();
        String s = enc.encode(data);
        Stitch.check(java.util.Arrays.equals(enc.decode(s), data), "decode(encode(x)) == x");
        Stitch.check(enc.canDecode(s), "canDecode(encode(x))");
        StringWriter w = new StringWriter();
        try (OutputStream os = enc.encodingStream(w)) { os.write(data); }
        Stitch.check(w.toString().equals(s), "encodingStream == encode");
        byte[] back = ByteStreams.toByteArray(enc.decodingStream(new StringReader(s)));
        Stitch.check(java.util.Arrays.equals(back, data), "decodingStream == decode");
        boolean can = enc.canDecode(garbage);
        try {
            enc.decode(garbage);
            Stitch.check(can, "decode succeeds only when canDecode");
        } catch (IllegalArgumentException ex) {
            Stitch.check(!can, "decode fails only when !canDecode");
        }
        Stitch.check(enc.encode(data, 0, data.length / 2).equals(enc.encode(java.util.Arrays.copyOf(data, data.length / 2))), "encode(range)");
    }

    @Endpoint
    public static void little_endian_data(@stitch.Max(64) Source s) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        LittleEndianDataOutputStream out = new LittleEndianDataOutputStream(bo);
        long l = s.range(Long.MIN_VALUE, Long.MAX_VALUE);
        int i = (int) ri(s, Integer.MIN_VALUE, Integer.MAX_VALUE);
        double d = s.pick(1.5, -0.0, Double.NaN, 1e300);
        String str = s.str(12);
        out.writeLong(l); out.writeInt(i); out.writeDouble(d); out.writeShort(i); out.writeChar(i); out.writeFloat((float) d); out.writeUTF(str);
        LittleEndianDataInputStream in = new LittleEndianDataInputStream(new ByteArrayInputStream(bo.toByteArray()));
        Stitch.check(in.readLong() == l && in.readInt() == i, "long / int round trip");
        Stitch.check(Double.compare(in.readDouble(), d) == 0, "double round trip");
        Stitch.check(in.readShort() == (short) i && in.readChar() == (char) i, "short / char round trip");
        in.readFloat();
        Stitch.check(in.readUTF().equals(str), "UTF round trip");
        Stitch.check(java.nio.ByteBuffer.wrap(bo.toByteArray()).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong() == l, "little-endian byte order");
    }

    @Endpoint
    public static void byte_streams(@Bytes(max = 512) byte[] data, @stitch.Max(16) Source s) throws IOException {
        byte[] copy = ByteStreams.toByteArray(new ByteArrayInputStream(data));
        Stitch.check(java.util.Arrays.equals(copy, data), "toByteArray");
        long n = ri(s, 0, 600);
        InputStream lim = ByteStreams.limit(new ByteArrayInputStream(data), n);
        Stitch.check(ByteStreams.toByteArray(lim).length == Math.min(n, data.length), "limit");
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        Stitch.check(ByteStreams.copy(new ByteArrayInputStream(data), o) == data.length && java.util.Arrays.equals(o.toByteArray(), data), "copy");
        ByteArrayDataInput in = ByteStreams.newDataInput(data);
        if (data.length >= 4) in.readInt();
        ByteArrayDataOutput out = ByteStreams.newDataOutput();
        out.write(data);
        Stitch.check(java.util.Arrays.equals(out.toByteArray(), data), "newDataOutput");
        Stitch.check(ByteSource.wrap(data).size() == data.length && ByteSource.wrap(data).slice(1, 3).read().length == Math.max(0, Math.min(3, data.length - 1)), "ByteSource slice");
        Stitch.check(ByteSource.wrap(data).contentEquals(ByteSource.concat(ByteSource.wrap(data).slice(0, 5), ByteSource.wrap(data).slice(5, 10000))), "ByteSource concat of slices");
        ByteSource.wrap(data).hash(com.google.common.hash.Hashing.crc32());
        CountingInputStream ci = new CountingInputStream(new ByteArrayInputStream(data));
        ByteStreams.exhaust(ci);
        Stitch.check(ci.getCount() == data.length, "CountingInputStream");
    }

    @Endpoint
    public static void char_streams(@Str(max = 256) String text) throws IOException {
        Stitch.check(CharStreams.toString(new StringReader(text)).equals(text), "CharStreams.toString");
        java.util.List<String> lines = CharStreams.readLines(new StringReader(text));
        java.util.List<String> ref = new java.util.ArrayList<>();
        new BufferedReader(new StringReader(text)).lines().forEach(ref::add);
        Stitch.check(lines.equals(ref), "readLines agrees with BufferedReader");
        Stitch.check(CharSource.wrap(text).lines().count() == ref.size(), "CharSource.lines");
        Stitch.check(CharSource.wrap(text).asByteSource(StandardCharsets.UTF_8).asCharSource(StandardCharsets.UTF_8).read().equals(text) || !com.google.common.base.Utf8.isWellFormed(text.getBytes(StandardCharsets.UTF_8)) || text.chars().anyMatch(c -> Character.isSurrogate((char) c)),
            "CharSource -> ByteSource -> CharSource round trip");
        StringWriter w = new StringWriter();
        CharStreams.copy(new StringReader(text), w);
        Stitch.check(w.toString().equals(text), "CharStreams.copy");
        Stitch.check(CharSource.wrap(text).length() == text.length(), "CharSource length");
    }
}
