import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.apache.commons.compress.archivers.*;
import org.apache.commons.compress.archivers.ar.*;
import org.apache.commons.compress.archivers.arj.*;
import org.apache.commons.compress.archivers.cpio.*;
import org.apache.commons.compress.archivers.dump.*;
import org.apache.commons.compress.archivers.sevenz.*;
import org.apache.commons.compress.archivers.tar.*;
import org.apache.commons.compress.archivers.zip.*;
import org.apache.commons.compress.compressors.*;
import org.apache.commons.compress.compressors.brotli.BrotliCompressorInputStream;
import org.apache.commons.compress.compressors.bzip2.*;
import org.apache.commons.compress.compressors.deflate.*;
import org.apache.commons.compress.compressors.deflate64.Deflate64CompressorInputStream;
import org.apache.commons.compress.compressors.gzip.*;
import org.apache.commons.compress.compressors.lz4.*;
import org.apache.commons.compress.compressors.lzma.*;
import org.apache.commons.compress.compressors.snappy.*;
import org.apache.commons.compress.compressors.xz.*;
import org.apache.commons.compress.compressors.z.ZCompressorInputStream;
import org.apache.commons.compress.utils.IOUtils;
import org.apache.commons.compress.utils.SeekableInMemoryByteChannel;
import stitch.*;

/**
 * Apache Commons Compress: read and write archives and compressed streams.
 *
 * Bugs: on malformed input, readers must fail with an IOException (or the library's
 * ArchiveException / CompressorException); any other exception, an OutOfMemoryError or a hang is
 * a bug (the 1.21 release fixed a series of such CVEs). Output is capped at 4 MB (bigger results
 * bail, so decompression bombs by design are not reported).
 * Oracles: archives and streams written by the library read back to what was written.
 * Entry accessors, extra fields, 7z/TAR/ZIP random access, LHA, Pack200, utilities and the
 * examples (Expander/Archiver) are in Api.java.
 */
public class Stubs {
    static final int CAP = 4 << 20;

    /** Reads a stream to the end, at most CAP bytes. */
    static byte[] drain(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) != -1) {
            Stitch.check(n >= 0 && n <= buf.length, "read() returns -1 or a count within the buffer");
            out.write(buf, 0, n);
            if (out.size() > CAP) Stitch.bail("output over 4 MB");
        }
        return out.toByteArray();
    }

    /** Walks every entry of an archive stream and reads its data. */
    static int walk(ArchiveInputStream in) throws IOException {
        int entries = 0;
        ArchiveEntry e;
        while ((e = in.getNextEntry()) != null) {
            if (++entries > 5000) Stitch.bail("too many entries");
            Api.inspectEntry(e);
            if (in.canReadEntryData(e)) drain(in);
        }
        return entries;
    }

    // ---- readers (bytes from per-format seed corpora) ----

    @Endpoint(bailsOn = {IOException.class})
    public static void zip_stream(@Bytes(max = 65536, type = "Zip") byte[] data, boolean unicodeExtra, boolean storedWithDD, boolean skipSplitSig) throws IOException {
        try (ZipArchiveInputStream in = new ZipArchiveInputStream(new ByteArrayInputStream(data), "UTF8", unicodeExtra, storedWithDD, skipSplitSig)) {
            walk(in);
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void zip_file(@Bytes(max = 65536, type = "Zip") byte[] data, boolean unicodeExtra) throws IOException {
        try (ZipFile z = new ZipFile(new SeekableInMemoryByteChannel(data), "zip", "UTF8", unicodeExtra)) {
            int n = 0;
            for (Enumeration<ZipArchiveEntry> en = z.getEntriesInPhysicalOrder(); en.hasMoreElements();) {
                ZipArchiveEntry e = en.nextElement();
                if (++n > 5000) Stitch.bail("too many entries");
                Api.inspectEntry(e);
                if (z.canReadEntryData(e)) {
                    try (InputStream in = z.getInputStream(e)) {
                        if (in != null) drain(in);
                    }
                }
                try (InputStream raw = z.getRawInputStream(e)) {
                    if (raw != null) drain(raw);
                }
            }
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void tar(@Bytes(max = 65536, type = "Tar") byte[] data, boolean lenient) throws IOException {
        try (TarArchiveInputStream in = new TarArchiveInputStream(new ByteArrayInputStream(data), TarConstants.DEFAULT_BLKSIZE, TarConstants.DEFAULT_RCDSIZE, null, lenient)) {
            walk(in);
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void sevenz(@Bytes(max = 65536, type = "SevenZ") byte[] data, @OneOf(strings = {"", "secret", "foo"}) String password) throws IOException {
        SevenZFile f = password.isEmpty() ? new SevenZFile(new SeekableInMemoryByteChannel(data))
                                          : new SevenZFile(new SeekableInMemoryByteChannel(data), password.toCharArray());
        try {
            SevenZArchiveEntry e;
            int n = 0;
            byte[] buf = new byte[8192];
            long total = 0;
            while ((e = f.getNextEntry()) != null) {
                if (++n > 5000) Stitch.bail("too many entries");
                Api.inspectEntry(e);
                if (e.hasStream()) {
                    int r;
                    while ((r = f.read(buf)) > 0) {
                        total += r;
                        if (total > CAP) Stitch.bail("output over 4 MB");
                    }
                }
            }
        } finally {
            f.close();
        }
    }

    static void other_archive(@Bytes(max = 32768) byte[] data, @OneOf(strings = {"ar", "arj", "cpio", "dump"}) String format) throws Exception {
        InputStream bin = new ByteArrayInputStream(data);
        ArchiveInputStream in;
        switch (format) {
            case "ar": in = new ArArchiveInputStream(bin); break;
            case "arj": in = new ArjArchiveInputStream(bin); break;
            case "cpio": in = new CpioArchiveInputStream(bin); break;
            default: in = new DumpArchiveInputStream(bin);
        }
        try (ArchiveInputStream a = in) {
            walk(a);
        }
    }

    @Endpoint(bailsOn = {IOException.class, ArchiveException.class})
    public static void ar(@Bytes(max = 32768, type = "Ar") byte[] data) throws Exception { other_archive(data, "ar"); }

    @Endpoint(bailsOn = {IOException.class, ArchiveException.class})
    public static void arj(@Bytes(max = 32768, type = "Arj") byte[] data) throws Exception { other_archive(data, "arj"); }

    @Endpoint(bailsOn = {IOException.class, ArchiveException.class})
    public static void cpio(@Bytes(max = 32768, type = "Cpio") byte[] data) throws Exception { other_archive(data, "cpio"); }

    @Endpoint(bailsOn = {IOException.class, ArchiveException.class})
    public static void dump(@Bytes(max = 65536, type = "Dump") byte[] data) throws Exception { other_archive(data, "dump"); }

    /** Format detection and the generic factory on any bytes. */
    @Endpoint(bailsOn = {IOException.class, ArchiveException.class, CompressorException.class})
    public static void detect(@Bytes(max = 32768) byte[] data) throws Exception {
        String a = null, c = null;
        try {
            a = ArchiveStreamFactory.detect(new BufferedInputStream(new ByteArrayInputStream(data)));
        } catch (ArchiveException e) { /* not an archive */ }
        try {
            c = CompressorStreamFactory.detect(new BufferedInputStream(new ByteArrayInputStream(data)));
        } catch (CompressorException e) { /* not compressed */ }
        Stitch.log("archive=" + a + " compressor=" + c);
        if (a != null && !a.equals(ArchiveStreamFactory.SEVEN_Z)) {
            try (ArchiveInputStream in = new ArchiveStreamFactory().createArchiveInputStream(a, new ByteArrayInputStream(data))) {
                walk(in);
            }
        }
        if (c != null) {
            try (InputStream in = new CompressorStreamFactory().createCompressorInputStream(c, new ByteArrayInputStream(data))) {
                drain(in);
            }
        }
    }

    static InputStream decompressor(String algo, InputStream in, boolean concat) throws IOException {
        switch (algo) {
            case "bzip2": return new BZip2CompressorInputStream(in, concat);
            case "gzip": return new GzipCompressorInputStream(in, concat);
            case "xz": return new XZCompressorInputStream(in, concat, 1 << 16);  // 64 MB memory limit (KiB)
            case "lzma": return new LZMACompressorInputStream(in, 1 << 16);
            case "deflate": return new DeflateCompressorInputStream(in, new DeflateParameters());
            case "deflate-raw": { DeflateParameters p = new DeflateParameters(); p.setWithZlibHeader(false); return new DeflateCompressorInputStream(in, p); }
            case "deflate64": return new Deflate64CompressorInputStream(in);
            case "lz4-framed": return new FramedLZ4CompressorInputStream(in, concat);
            case "lz4-block": return new BlockLZ4CompressorInputStream(in);
            case "snappy-framed": return new FramedSnappyCompressorInputStream(in);
            case "snappy-raw": return new SnappyCompressorInputStream(in);
            case "z": return new ZCompressorInputStream(in, 1 << 16);
            default: return new BrotliCompressorInputStream(in);
        }
    }

    static void decompress(String algo, byte[] data, boolean concat) throws IOException {
        try (InputStream in = decompressor(algo, new ByteArrayInputStream(data), concat)) {
            drain(in);
        }
    }

    @Endpoint(bailsOn = {IOException.class}) public static void bzip2(@Bytes(max = 32768, type = "Bz2") byte[] d, boolean concat) throws IOException { decompress("bzip2", d, concat); }
    @Endpoint(bailsOn = {IOException.class}) public static void gzip(@Bytes(max = 32768, type = "Gz") byte[] d, boolean concat) throws IOException { decompress("gzip", d, concat); }
    @Endpoint(bailsOn = {IOException.class}) public static void xz(@Bytes(max = 32768, type = "Xz") byte[] d, boolean concat) throws IOException { decompress("xz", d, concat); }
    @Endpoint(bailsOn = {IOException.class}) public static void lzma(@Bytes(max = 32768, type = "Lzma") byte[] d) throws IOException { decompress("lzma", d, false); }
    @Endpoint(bailsOn = {IOException.class}) public static void lz4(@Bytes(max = 32768, type = "Lz4") byte[] d, @OneOf(strings = {"lz4-framed", "lz4-block"}) String kind, boolean concat) throws IOException { decompress(kind, d, concat); }
    @Endpoint(bailsOn = {IOException.class}) public static void snappy(@Bytes(max = 32768, type = "Snappy") byte[] d, @OneOf(strings = {"snappy-framed", "snappy-raw"}) String kind) throws IOException { decompress(kind, d, false); }
    @Endpoint(bailsOn = {IOException.class}) public static void z(@Bytes(max = 32768, type = "Zc") byte[] d) throws IOException { decompress("z", d, false); }
    @Endpoint(bailsOn = {IOException.class}) public static void deflate(@Bytes(max = 32768, type = "Deflate") byte[] d, @OneOf(strings = {"deflate", "deflate-raw", "deflate64"}) String kind) throws IOException { decompress(kind, d, false); }
    @Endpoint(bailsOn = {IOException.class}) public static void brotli(@Bytes(max = 32768, type = "Brotli") byte[] d) throws IOException { decompress("brotli", d, false); }

    // ---- writers: what the library writes, it reads back ----

    /** An archive being written, with what was put in it. */
    public static class Writer {
        final String format;
        final ByteArrayOutputStream bytes;
        final ArchiveOutputStream out;
        final short cpioFormat;
        String encoding = "UTF-8";
        final List<String> names = new ArrayList<>();
        final List<byte[]> contents = new ArrayList<>();
        Writer(String format, ArchiveOutputStream out, ByteArrayOutputStream bytes, short cpioFormat) {
            this.format = format;
            this.out = out;
            this.bytes = bytes;
            this.cpioFormat = cpioFormat;
        }
    }

    /** A finished archive. */
    public static class Archive {
        final Writer w;
        final byte[] data;
        Archive(Writer w, byte[] data) { this.w = w; this.data = data; }
    }

    @Endpoint(bailsOn = {IOException.class, ArchiveException.class})
    public static Writer writer(@OneOf(strings = {"zip", "tar", "cpio", "ar", "jar"}) String format, @Range(lo = 0, hi = 3) int mode, @OneOf(strings = {"UTF-8", "CP437", "ISO-8859-1", "US-ASCII"}) String encoding) throws Exception {
        Writer w = makeWriter(format, mode, encoding);
        w.encoding = encoding;
        return w;
    }

    static Writer makeWriter(String format, int mode, String encoding) throws Exception {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        switch (format) {
            case "zip": {
                ZipArchiveOutputStream z = new ZipArchiveOutputStream(b);
                z.setEncoding(encoding);
                z.setUseZip64(Zip64Mode.values()[mode % 3]);
                z.setMethod(mode == 3 ? java.util.zip.ZipEntry.STORED : java.util.zip.ZipEntry.DEFLATED);
                z.setCreateUnicodeExtraFields(mode == 1 ? ZipArchiveOutputStream.UnicodeExtraFieldPolicy.ALWAYS : ZipArchiveOutputStream.UnicodeExtraFieldPolicy.NEVER);
                return new Writer(format, z, b, (short) 0);
            }
            case "tar": {
                TarArchiveOutputStream t = new TarArchiveOutputStream(b, encoding);
                t.setLongFileMode(new int[] {TarArchiveOutputStream.LONGFILE_ERROR, TarArchiveOutputStream.LONGFILE_TRUNCATE, TarArchiveOutputStream.LONGFILE_GNU, TarArchiveOutputStream.LONGFILE_POSIX}[mode]);
                t.setBigNumberMode(mode % 3);
                t.setAddPaxHeadersForNonAsciiNames(mode >= 2);
                return new Writer(format, t, b, (short) 0);
            }
            case "cpio": {
                short[] fmts = {CpioConstants.FORMAT_NEW, CpioConstants.FORMAT_NEW_CRC, CpioConstants.FORMAT_OLD_ASCII, CpioConstants.FORMAT_OLD_BINARY};
                return new Writer(format, new CpioArchiveOutputStream(b, fmts[mode], 512, encoding), b, fmts[mode]);
            }
            case "ar": {
                ArArchiveOutputStream a = new ArArchiveOutputStream(b);
                a.setLongFileMode(mode % 2 == 0 ? ArArchiveOutputStream.LONGFILE_BSD : ArArchiveOutputStream.LONGFILE_ERROR);
                return new Writer(format, a, b, (short) 0);
            }
            default:
                return new Writer(format, new ArchiveStreamFactory(encoding).createArchiveOutputStream("jar", b), b, (short) 0);
        }
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void add_entry(Writer w, @Str(max = 128, type = "EntryName") String name, @Bytes(max = 2048) byte[] content, long mtime, @Range(lo = 0, hi = 0777) int mode) throws IOException {
        Stitch.require(!name.isEmpty() && !name.endsWith("/") && name.indexOf('\0') < 0, "not a file name");
        Stitch.require(!w.format.equals("ar") || (!name.contains("/") && !name.endsWith(" ") && !name.startsWith("#1/")), "not an ar member name");
        Stitch.require(w.format.equals("zip") || w.format.equals("jar") || mtime >= 0, "negative mtime");
        ArchiveEntry e;
        switch (w.format) {
            case "tar": { TarArchiveEntry t = new TarArchiveEntry(name); t.setSize(content.length); t.setModTime(mtime); t.setMode(0100000 | mode); e = t; break; }
            case "cpio": { CpioArchiveEntry c = new CpioArchiveEntry(w.cpioFormat, name, content.length); c.setTime(mtime / 1000); c.setMode(0100000 | mode); e = c; break; }
            case "ar": e = new ArArchiveEntry(name, content.length, 0, 0, 0100000 | mode, mtime / 1000); break;
            default: { ZipArchiveEntry z = new ZipArchiveEntry(name); z.setSize(content.length); z.setTime(mtime); z.setUnixMode(0100000 | mode); e = z; }
        }
        try {
            w.out.putArchiveEntry(e);
        } catch (RuntimeException x) {
            // tar's documented refusals: "file name ... is too long", "... is too big"
            if (x.getClass() == RuntimeException.class) Stitch.bail("refused: " + x.getMessage());
            throw x;
        }
        w.out.write(content);
        w.out.closeArchiveEntry();
        w.names.add(name);
        w.contents.add(content);
    }

    @Endpoint(bailsOn = {IOException.class})
    public static Archive finish(@Take Writer w) throws IOException {
        w.out.finish();
        w.out.close();
        return new Archive(w, w.bytes.toByteArray());
    }

    @Endpoint(bailsOn = {})
    public static void read_back(Archive a) throws Exception {
        Stitch.require(!a.w.names.isEmpty(), "empty archive (ar writes nothing at all)");
        List<String> names = new ArrayList<>();
        List<byte[]> contents = new ArrayList<>();
        try (ArchiveInputStream in = new ArchiveStreamFactory(a.w.encoding).createArchiveInputStream(a.w.format.equals("jar") ? "zip" : a.w.format, new ByteArrayInputStream(a.data))) {
            ArchiveEntry e;
            while ((e = in.getNextEntry()) != null) {
                names.add(e.getName());
                contents.add(drain(in));
            }
        }
        Stitch.log("wrote " + a.w.names + ", read " + names);
        List<String> expect = new ArrayList<>(a.w.names);
        if (a.w.format.equals("jar") && !names.isEmpty() && names.get(0).equals("META-INF/")) names.remove(0);
        Stitch.check(names.size() == expect.size(), "an archive reads back with the entries written");
        for (int i = 0; i < names.size(); i++) {
            Stitch.check(Arrays.equals(contents.get(i), a.w.contents.get(i)), "entry contents read back as written");
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void zip_file_back(Archive a) throws IOException {
        Stitch.require(a.w.format.equals("zip") || a.w.format.equals("jar"), "not a zip");
        try (ZipFile z = new ZipFile(new SeekableInMemoryByteChannel(a.data), a.w.encoding)) {
            for (int i = 0; i < a.w.names.size(); i++) {
                ZipArchiveEntry e = z.getEntry(a.w.names.get(i));
                if (e == null) continue;  // (names that a charset cannot encode come back changed)
                try (InputStream in = z.getInputStream(e)) {
                    byte[] got = drain(in);
                    boolean unique = a.w.names.indexOf(a.w.names.get(i)) == a.w.names.lastIndexOf(a.w.names.get(i));
                    if (unique) Stitch.check(Arrays.equals(got, a.w.contents.get(i)), "ZipFile reads entries as written");
                }
            }
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void compress_roundtrip(@Bytes(max = 8192) byte[] data, @OneOf(strings = {"bzip2", "gzip", "xz", "lzma", "deflate", "lz4-framed", "lz4-block", "snappy-framed", "deflate-raw"}) String algo, @Range(lo = 1, hi = 9) int level) throws Exception {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        OutputStream out;
        switch (algo) {
            case "bzip2": out = new BZip2CompressorOutputStream(b, level); break;
            case "gzip": { GzipParameters p = new GzipParameters(); p.setCompressionLevel(level); p.setFilename(level % 2 == 0 ? "fé.txt" : null); p.setComment(level > 5 ? "c" : null); out = new GzipCompressorOutputStream(b, p); break; }
            case "xz": out = new XZCompressorOutputStream(b, level % 7); break;
            case "lzma": out = new LZMACompressorOutputStream(b); break;
            case "deflate": { DeflateParameters p = new DeflateParameters(); p.setCompressionLevel(level); out = new DeflateCompressorOutputStream(b, p); break; }
            case "deflate-raw": { DeflateParameters p = new DeflateParameters(); p.setCompressionLevel(level); p.setWithZlibHeader(false); out = new DeflateCompressorOutputStream(b, p); break; }
            case "lz4-framed": {
                FramedLZ4CompressorOutputStream.BlockSize[] sizes = FramedLZ4CompressorOutputStream.BlockSize.values();
                out = new FramedLZ4CompressorOutputStream(b, new FramedLZ4CompressorOutputStream.Parameters(sizes[level % sizes.length], level % 2 == 0, level % 3 == 0, level > 6));
                break;
            }
            case "lz4-block": out = new BlockLZ4CompressorOutputStream(b); break;
            default: out = new FramedSnappyCompressorOutputStream(b);
        }
        // write in pieces of varying size
        int off = 0, step = 1 + level * 97;
        while (off < data.length) {
            int n = Math.min(step, data.length - off);
            if (n == 1) out.write(data[off]); else out.write(data, off, n);
            off += n;
        }
        out.close();
        byte[] back;
        try (InputStream in = decompressor(algo, new ByteArrayInputStream(b.toByteArray()), false)) {
            back = drain(in);
        }
        Stitch.check(Arrays.equals(back, data), "decompress(compress(data)) == data");
    }
}
