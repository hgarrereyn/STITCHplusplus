import java.io.*;
import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SeekableByteChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.FileTime;
import java.util.*;
import java.util.concurrent.Executors;
import java.util.zip.CRC32;
import org.apache.commons.compress.archivers.*;
import org.apache.commons.compress.archivers.ar.*;
import org.apache.commons.compress.archivers.arj.*;
import org.apache.commons.compress.archivers.cpio.*;
import org.apache.commons.compress.archivers.dump.*;
import org.apache.commons.compress.archivers.examples.Archiver;
import org.apache.commons.compress.archivers.examples.Expander;
import org.apache.commons.compress.archivers.jar.*;
import org.apache.commons.compress.archivers.lha.*;
import org.apache.commons.compress.archivers.sevenz.*;
import org.apache.commons.compress.archivers.tar.*;
import org.apache.commons.compress.archivers.zip.*;
import org.apache.commons.compress.changes.*;
import org.apache.commons.compress.compressors.*;
import org.apache.commons.compress.compressors.bzip2.*;
import org.apache.commons.compress.compressors.deflate.*;
import org.apache.commons.compress.compressors.gzip.*;
import org.apache.commons.compress.compressors.lz4.*;
import org.apache.commons.compress.compressors.lz77support.*;
import org.apache.commons.compress.compressors.lzma.*;
import org.apache.commons.compress.compressors.pack200.*;
import org.apache.commons.compress.compressors.snappy.*;
import org.apache.commons.compress.compressors.xz.*;
import org.apache.commons.compress.compressors.z.ZCompressorInputStream;
import org.apache.commons.compress.huffman.HuffmanDecoder;
import org.apache.commons.compress.utils.*;
import stitch.*;

/**
 * The rest of the Commons Compress API: every entry type's accessors (on entries read from fuzzed
 * archives), ZIP extra fields, 7z / TAR random-access readers and writers, LHA, Pack200, gzip
 * metadata, the LZ77 engine, the utilities, ChangeSet, the Expander/Archiver examples and the
 * parallel ZIP creator.
 * Oracles: entry names resolve inside the target directory (resolveIn) and Expander writes nothing
 * outside it; extra fields re-parse to the same data; written 7z/TAR/ZIP/gzip/pack200/LZ77 data
 * reads back as written; number/time/charset codecs round trip; channels and stream wrappers keep
 * the bytes.
 */
public class Api {
    static final int CAP = Stubs.CAP;

    // ---------------------------------------------------------------- entry accessors
    /** Calls every accessor of an entry read from (possibly malformed) input. */
    static void inspectEntry(ArchiveEntry a) throws IOException {
        a.getName(); a.getSize(); a.isDirectory();
        // (documented: a 7z entry without a modification date throws UnsupportedOperationException)
        if (!(a instanceof SevenZArchiveEntry) || ((SevenZArchiveEntry) a).getHasLastModifiedDate()) a.getLastModifiedDate();
        if (!(a instanceof SevenZArchiveEntry) || ((SevenZArchiveEntry) a).getHasLastModifiedDate()) ArchiveUtils.toString(a);
        // (null names and names with NUL make Path.resolve throw: left to the expand endpoint, which hits it through Expander)
        if (a.getName() != null && a.getName().indexOf('\0') < 0) {
            try {
                Path dir = Paths.get("/tmp/stitch-resolve");
                Path p = a.resolveIn(dir);
                Stitch.check(p.normalize().startsWith(dir), "resolveIn() stays inside the directory");
            } catch (IOException e) { /* documented: an entry name that escapes the directory */ }
            catch (InvalidPathException e) { /* not a path on this platform */ }
        }
        if (a instanceof JarArchiveEntry) {
            JarArchiveEntry e = (JarArchiveEntry) a;
            e.getCertificates(); e.getManifestAttributes();
        }
        if (a instanceof ZipArchiveEntry) {
            ZipArchiveEntry e = (ZipArchiveEntry) a;
            e.getCentralDirectoryExtra(); e.getCommentSource(); e.getDataOffset(); e.getDiskNumberStart(); e.getExternalAttributes(); e.getExtraFields(); e.getGeneralPurposeBit(); e.getInternalAttributes(); e.getLastModifiedDate(); e.getLocalFileDataExtra(); e.getLocalHeaderOffset(); e.getMethod(); e.getName(); e.getNameSource(); e.getPlatform(); e.getRawFlag(); e.getRawName(); e.getSize(); e.getTime(); e.getUnixMode(); e.getUnparseableExtraFieldData(); e.getVersionMadeBy(); e.getVersionRequired(); e.isDirectory(); e.isStreamContiguous(); e.isUnixSymlink();
            e.getExtraFields(true); e.getExtraField(X5455_ExtendedTimestamp.HEADER_ID);
            for (ZipExtraField f : e.getExtraFields(true)) inspectExtra(f);
            inspectGpb(e.getGeneralPurposeBit());
            ZipMethod.getMethodByCode(e.getMethod());
            ZipArchiveEntry c = (ZipArchiveEntry) e.clone();
            Stitch.check(c.getName().equals(e.getName()), "clone keeps the name");
            Stitch.check(e.equals(c), "an entry equals its clone");
        }
        if (a instanceof TarArchiveEntry) {
            TarArchiveEntry e = (TarArchiveEntry) a;
            e.getCreationTime(); e.getDataOffset(); e.getDevMajor(); e.getDevMinor(); e.getDirectoryEntries(); e.getExtraPaxHeaders(); e.getFile(); e.getGroupId(); e.getGroupName(); e.getLastAccessTime(); e.getLastModifiedDate(); e.getLastModifiedTime(); e.getLinkFlag(); e.getLinkName(); e.getLongGroupId(); e.getLongUserId(); e.getModTime(); e.getMode(); e.getName(); e.getPath(); e.getRealSize(); e.getSize(); e.getSparseHeaders(); e.getStatusChangeTime(); e.getUserId(); e.getUserName(); e.isBlockDevice(); e.isCharacterDevice(); e.isCheckSumOK(); e.isDirectory(); e.isExtended(); e.isFIFO(); e.isFile(); e.isGNULongLinkEntry(); e.isGNULongNameEntry(); e.isGNUSparse(); e.isGlobalPaxHeader(); e.isLink(); e.isOldGNUSparse(); e.isPaxGNU1XSparse(); e.isPaxGNUSparse(); e.isPaxHeader(); e.isSparse(); e.isStarSparse(); e.isStreamContiguous(); e.isSymbolicLink();
            e.getExtraPaxHeader("path"); e.isDescendent(e);
            if (e.getSparseHeaders() != null) for (TarArchiveStructSparse s : e.getSparseHeaders()) { s.getNumbytes(); s.getOffset(); }
            try { e.getOrderedSparseHeaders(); } catch (IOException x) { /* documented: overlapping or invalid sparse headers */ }
        }
        if (a instanceof SevenZArchiveEntry) {
            SevenZArchiveEntry e = (SevenZArchiveEntry) a;
            e.getContentMethods(); e.getCrc(); e.getCrcValue(); e.getHasAccessDate(); e.getHasCrc(); e.getHasCreationDate(); e.getHasLastModifiedDate(); e.getHasWindowsAttributes(); e.getName(); e.getSize(); e.getWindowsAttributes(); e.hasStream(); e.isAntiItem(); e.isDirectory(); e.isEmptyStream();
            // (the date getters are documented to throw UnsupportedOperationException when the date is absent)
            if (e.getHasAccessDate()) { e.getAccessDate(); e.getAccessTime(); }
            if (e.getHasCreationDate()) { e.getCreationDate(); e.getCreationTime(); }
            if (e.getHasLastModifiedDate()) { e.getLastModifiedDate(); e.getLastModifiedTime(); }
            if (e.getContentMethods() != null) for (SevenZMethodConfiguration m : e.getContentMethods()) { m.getMethod(); m.getOptions(); }
        }
        if (a instanceof CpioArchiveEntry) {
            CpioArchiveEntry e = (CpioArchiveEntry) a;
            e.getAlignmentBoundary(); e.getDataPadCount(); e.getFormat(); e.getGID(); e.getHeaderSize(); e.getInode(); e.getLastModifiedDate(); e.getMode(); e.getName(); e.getNumberOfLinks(); e.getSize(); e.getTime(); e.getUID();
            // documented: the device getters of the other format family throw UnsupportedOperationException
            if ((e.getFormat() & CpioConstants.FORMAT_NEW_MASK) != 0) { e.getChksum(); e.getDeviceMaj(); e.getDeviceMin(); e.getRemoteDeviceMaj(); e.getRemoteDeviceMin(); }
            else { e.getDevice(); e.getRemoteDevice(); } e.isBlockDevice(); e.isCharacterDevice(); e.isDirectory(); e.isNetwork(); e.isPipe(); e.isRegularFile(); e.isSocket(); e.isSymbolicLink();
            e.getHeaderPadCount(); e.getHeaderPadCount(StandardCharsets.UTF_8); e.getHeaderPadCount(e.getName().length());
        }
        if (a instanceof DumpArchiveEntry) {
            DumpArchiveEntry e = (DumpArchiveEntry) a;
            e.getAccessTime(); e.getCreationTime(); e.getGeneration(); e.getGroupId(); e.getHeaderCount(); e.getHeaderHoles(); e.getHeaderType(); e.getIno(); e.getLastModifiedDate(); e.getMode(); e.getName(); e.getNlink(); e.getOffset(); e.getPermissions(); e.getSimpleName(); e.getSize(); e.getType(); e.getUserId(); e.getVolume(); e.isBlkDev(); e.isChrDev(); e.isDeleted(); e.isDirectory(); e.isFifo(); e.isFile(); e.isSocket();
            for (int i = 0; i < Math.min(e.getHeaderCount(), 512); i++) e.isSparseRecord(i);
        }
        if (a instanceof ArArchiveEntry) {
            ArArchiveEntry e = (ArArchiveEntry) a;
            e.getGroupId(); e.getLastModified(); e.getLastModifiedDate(); e.getLength(); e.getMode(); e.getName(); e.getSize(); e.getUserId(); e.isDirectory();
        }
        if (a instanceof ArjArchiveEntry) {
            ArjArchiveEntry e = (ArjArchiveEntry) a;
            e.getHostOs(); e.getLastModifiedDate(); e.getMode(); e.getName(); e.getSize(); e.getUnixMode(); e.isDirectory(); e.isHostOsUnix();
        }
        if (a instanceof LhaArchiveEntry) {
            LhaArchiveEntry e = (LhaArchiveEntry) a;
            e.getCompressedSize(); e.getCompressionMethod(); e.getCrcValue(); e.getLastModifiedDate(); e.getMsdosFileAttributes(); e.getName(); e.getOsId(); e.getSize(); e.getUnixGroupId(); e.getUnixPermissionMode(); e.getUnixUserId(); e.isDirectory();
        }
    }

    static void inspectGpb(GeneralPurposeBit b) {
        b.usesDataDescriptor(); b.usesEncryption(); b.usesStrongEncryption(); b.usesUTF8ForNames();
        byte[] enc = b.encode();
        Stitch.check(GeneralPurposeBit.parse(enc, 0).equals(b), "GeneralPurposeBit parse(encode()) round trips");
        Stitch.check(b.clone().equals(b), "GeneralPurposeBit clone equals");
    }

    static void inspectExtra(ZipExtraField f) {
        f.getHeaderId(); f.getLocalFileDataLength(); f.getCentralDirectoryLength(); f.getLocalFileDataData(); f.getCentralDirectoryData();
        if (f instanceof AsiExtraField) { AsiExtraField e = (AsiExtraField) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getGroupId(); e.getHeaderId(); e.getLinkedFile(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getMode(); e.getUserId(); e.isDirectory(); e.isLink(); }
        if (f instanceof X5455_ExtendedTimestamp) { X5455_ExtendedTimestamp e = (X5455_ExtendedTimestamp) f; e.getAccessFileTime(); e.getAccessJavaTime(); e.getAccessTime(); e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getCreateFileTime(); e.getCreateJavaTime(); e.getCreateTime(); e.getFlags(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getModifyFileTime(); e.getModifyJavaTime(); e.getModifyTime(); e.isBit0_modifyTimePresent(); e.isBit1_accessTimePresent(); e.isBit2_createTimePresent(); }
        if (f instanceof X7875_NewUnix) { X7875_NewUnix e = (X7875_NewUnix) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getGID(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getUID(); }
        if (f instanceof X000A_NTFS) { X000A_NTFS e = (X000A_NTFS) f; e.getAccessFileTime(); e.getAccessJavaTime(); e.getAccessTime(); e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getCreateFileTime(); e.getCreateJavaTime(); e.getCreateTime(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getModifyFileTime(); e.getModifyJavaTime(); e.getModifyTime(); }
        if (f instanceof Zip64ExtendedInformationExtraField) { Zip64ExtendedInformationExtraField e = (Zip64ExtendedInformationExtraField) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getCompressedSize(); e.getDiskStartNumber(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getRelativeHeaderOffset(); e.getSize(); }
        if (f instanceof ResourceAlignmentExtraField) { ResourceAlignmentExtraField e = (ResourceAlignmentExtraField) f; e.allowMethodChange(); e.getAlignment(); e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); }
        if (f instanceof AbstractUnicodeExtraField) { AbstractUnicodeExtraField e = (AbstractUnicodeExtraField) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getLocalFileDataData(); e.getLocalFileDataLength(); e.getNameCRC32(); e.getUnicodeName(); }
        if (f instanceof UnicodePathExtraField) ((UnicodePathExtraField) f).getHeaderId();
        if (f instanceof UnicodeCommentExtraField) ((UnicodeCommentExtraField) f).getHeaderId();
        if (f instanceof UnrecognizedExtraField) { UnrecognizedExtraField e = (UnrecognizedExtraField) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); }
        if (f instanceof UnparseableExtraFieldData) { UnparseableExtraFieldData e = (UnparseableExtraFieldData) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); }
        if (f instanceof JarMarker) { JarMarker e = (JarMarker) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); }
        if (f instanceof X0015_CertificateIdForFile) { X0015_CertificateIdForFile e = (X0015_CertificateIdForFile) f; e.getHashAlgorithm(); e.getRecordCount(); }
        if (f instanceof X0016_CertificateIdForCentralDirectory) { X0016_CertificateIdForCentralDirectory e = (X0016_CertificateIdForCentralDirectory) f; e.getHashAlgorithm(); e.getRecordCount(); }
        if (f instanceof X0017_StrongEncryptionHeader) { X0017_StrongEncryptionHeader e = (X0017_StrongEncryptionHeader) f; e.getEncryptionAlgorithm(); e.getHashAlgorithm(); e.getRecordCount(); }
        if (f instanceof PKWareExtraHeader) { PKWareExtraHeader e = (PKWareExtraHeader) f; e.getCentralDirectoryData(); e.getCentralDirectoryLength(); e.getHeaderId(); e.getLocalFileDataData(); e.getLocalFileDataLength(); }
    }

    // ---------------------------------------------------------------- more readers
    /** Stream readers through their builders, with the ArchiveInputStream iteration API. */
    @Endpoint(bailsOn = {IOException.class})
    public static void builder_read(@Bytes(max = 65536, type = "Archive") byte[] data, @Range(lo = 0, hi = 7) int fmt0, @OneOf(ints = {1024, 64, 0x7fffffff}) int maxName,
                                    boolean lenient, boolean iterate, boolean auto) throws IOException {
        int fmt = auto ? detected(data, Arrays.asList("tar", "zip", "ar", "cpio", "arj", "dump", "lha", "jar"), fmt0) : fmt0;
        ArchiveInputStream<?> in;
        switch (fmt) {
            case 0: in = TarArchiveInputStream.builder().setByteArray(data).setLenient(lenient).setBlockSize(TarConstants.DEFAULT_BLKSIZE).setRecordSize(TarConstants.DEFAULT_RCDSIZE).setMaxPaxHeaderSize(1 << 16).setMaxEntryNameLength(maxName).get(); break;
            case 1: in = ZipArchiveInputStream.builder().setByteArray(data).setUseUnicodeExtraFields(lenient).setSupportStoredEntryDataDescriptor(true).setSkipSplitSignature(lenient).setMaxEntryNameLength(maxName).get(); break;
            case 2: in = ArArchiveInputStream.builder().setByteArray(data).setMaxEntryNameLength(maxName).get(); break;
            case 3: in = CpioArchiveInputStream.builder().setByteArray(data).setBlockSize(512).setMaxEntryNameLength(maxName).get(); break;
            case 4: in = ArjArchiveInputStream.builder().setByteArray(data).setSelfExtracting(lenient).get(); break;
            case 5: in = DumpArchiveInputStream.builder().setByteArray(data).get(); break;
            case 6: in = LhaArchiveInputStream.builder().setByteArray(data).get(); break;
            default: in = new JarArchiveInputStream(new ByteArrayInputStream(data));
        }
        try (ArchiveInputStream<?> a = in) {
            a.getCharset(); a.markSupported();
            if (iterate) {
                int[] n = {0};
                a.forEach(e -> { if (++n[0] > 5000) throw Stitch.bail("too many entries"); inspectEntry(e); if (a.canReadEntryData(e)) Stubs.drain(a); });
            } else {
                org.apache.commons.io.function.IOIterator<?> it = a.iterator();
                int n = 0;
                while (it.hasNext()) { ArchiveEntry e = (ArchiveEntry) it.next(); if (++n > 5000) Stitch.bail("too many entries"); inspectEntry(e); if (a.canReadEntryData(e)) { if (a.read() >= 0) Stubs.drain(a); } }
            }
            Stitch.check(a.getBytesRead() >= 0 && a.getCount() >= 0, "byte counts are non-negative");
            if (a instanceof ArjArchiveInputStream) { ((ArjArchiveInputStream) a).getArchiveComment(); ((ArjArchiveInputStream) a).getArchiveName(); }
            if (a instanceof DumpArchiveInputStream) inspectSummary(((DumpArchiveInputStream) a).getSummary());
            if (a instanceof TarArchiveInputStream) { TarArchiveInputStream t = (TarArchiveInputStream) a; t.getRecordSize(); t.getCurrentEntry(); t.available(); }
            if (a instanceof ZipArchiveInputStream) { ZipArchiveInputStream z = (ZipArchiveInputStream) a; z.getCompressedCount(); z.getUncompressedCount(); }
            if (a instanceof CpioArchiveInputStream) ((CpioArchiveInputStream) a).available();
        }
    }

    static void inspectSummary(DumpArchiveSummary e) {
        if (e == null) return;
        e.getDevname(); e.getDumpDate(); e.getFilesystem(); e.getFirstRecord(); e.getFlags(); e.getHostname(); e.getLabel(); e.getLevel(); e.getNTRec(); e.getPreviousDumpDate(); e.getVolume(); e.isCompressed(); e.isExtendedAttributes(); e.isMetaDataOnly(); e.isNewHeader(); e.isNewInode();
    }

    /** The format ArchiveStreamFactory detects, as an index into names (fallback when none). */
    static int detected(byte[] data, List<String> names, int fallback) {
        try {
            int i = names.indexOf(ArchiveStreamFactory.detect(new BufferedInputStream(new ByteArrayInputStream(data))));
            return i >= 0 ? i : fallback;
        } catch (ArchiveException e) {
            return fallback;
        }
    }

    /** The per-format readers' typed iteration and matches() signature checks. */
    @Endpoint(bailsOn = {IOException.class})
    public static void typed_read(@Bytes(max = 65536, type = "Archive") byte[] data, @Range(lo = 0, hi = 5) int fmt0, @Range(lo = 0, hi = 64) int skip, boolean auto) throws IOException {
        int fmt = auto ? detected(data, Arrays.asList("ar", "cpio", "dump", "tar", "zip", "jar"), fmt0) : fmt0;
        int n = 0;
        switch (fmt) {
            case 0: try (ArArchiveInputStream in = new ArArchiveInputStream(new ByteArrayInputStream(data))) { ArArchiveEntry e; while ((e = in.getNextArEntry()) != null && n++ < 5000) { inspectEntry(e); in.read(new byte[8], 0, 8); } } break;
            case 1: try (CpioArchiveInputStream in = new CpioArchiveInputStream(new ByteArrayInputStream(data))) { CpioArchiveEntry e; while ((e = in.getNextCPIOEntry()) != null && n++ < 5000) { inspectEntry(e); in.skip(skip); } } break;
            case 2: try (DumpArchiveInputStream in = new DumpArchiveInputStream(new ByteArrayInputStream(data))) { DumpArchiveEntry e; while ((e = in.getNextDumpEntry()) != null && n++ < 5000) inspectEntry(e); } break;
            case 3: try (TarArchiveInputStream in = new TarArchiveInputStream(new ByteArrayInputStream(data))) { TarArchiveEntry e; while ((e = in.getNextTarEntry()) != null && n++ < 5000) { inspectEntry(e); in.skip(skip); } } break;
            case 4: try (ZipArchiveInputStream in = new ZipArchiveInputStream(new ByteArrayInputStream(data))) { in.setExtraFieldSupport(id -> ExtraFieldUtils.createExtraFieldNoDefault(id)); ZipArchiveEntry e; while ((e = in.getNextZipEntry()) != null && n++ < 5000) { inspectEntry(e); in.skip(skip); } } break;
            default: try (JarArchiveInputStream in = JarArchiveInputStream.jarInputStreamBuilder().setByteArray(data).get()) { JarArchiveEntry e; while ((e = in.getNextJarEntry()) != null && n++ < 5000) inspectEntry(e); } break;
        }
        ArArchiveInputStream.matches(data, data.length); ArjArchiveInputStream.matches(data, data.length); CpioArchiveInputStream.matches(data, data.length);
        DumpArchiveInputStream.matches(data, data.length); TarArchiveInputStream.matches(data, data.length); ZipArchiveInputStream.matches(data, data.length);
        JarArchiveInputStream.matches(data, data.length); SevenZFile.matches(data, data.length); LhaArchiveInputStream.matches(data, data.length);
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void lha(@Bytes(max = 65536, type = "Lha") byte[] data) throws IOException {
        try (LhaArchiveInputStream in = LhaArchiveInputStream.builder().setByteArray(data).get()) {
            Stubs.walk(in);
        }
    }

    /** ZipFile through its builder, with the random-access reading API. */
    @Endpoint(bailsOn = {IOException.class})
    public static void zip_file_builder(@Bytes(max = 65536, type = "Zip") byte[] data, boolean ignoreLocal, boolean unicode, @OneOf(ints = {1, 2, 100}) int disks, boolean raw) throws IOException {
        try (ZipFile z = ZipFile.builder().setSeekableByteChannel(new SeekableInMemoryByteChannel(data)).setIgnoreLocalFileHeader(ignoreLocal).setUseUnicodeExtraFields(unicode).setMaxNumberOfDisks(disks).get()) {
            z.getEncoding(); z.getFirstLocalFileHeaderOffset();
            try (InputStream pre = z.getContentBeforeFirstLocalFileHeader()) { if (pre != null) Stubs.drain(pre); }
            int n = 0;
            for (ZipArchiveEntry e : Collections.list(z.getEntries())) {
                if (++n > 5000) Stitch.bail("too many entries");
                inspectEntry(e);
                z.getEntries(e.getName());
                z.getEntriesInPhysicalOrder(e.getName());
                if (e.isUnixSymlink()) z.getUnixSymlink(e);
            }
            z.stream().count();
            if (raw) {
                ByteArrayOutputStream b = new ByteArrayOutputStream();
                try (ZipArchiveOutputStream out = new ZipArchiveOutputStream(b)) {
                    z.copyRawEntries(out, e -> e.getSize() < 4096);
                }
            }
        }
    }

    /** TarFile (random access) and its stream. */
    @Endpoint(bailsOn = {IOException.class})
    public static void tar_file(@Bytes(max = 65536, type = "Tar") byte[] data, boolean lenient, @Range(lo = 0, hi = 2) int how) throws IOException {
        TarFile t = how == 0 ? new TarFile(data, lenient) : how == 1 ? new TarFile(new SeekableInMemoryByteChannel(data), TarConstants.DEFAULT_BLKSIZE, TarConstants.DEFAULT_RCDSIZE, null, lenient)
                                                          : TarFile.builder().setByteArray(data).setLenient(lenient).get();
        try (TarFile f = t) {
            int n = 0;
            for (TarArchiveEntry e : f.getEntries()) {
                if (++n > 5000) Stitch.bail("too many entries");
                inspectEntry(e);
                try (InputStream in = f.getInputStream(e)) { Stubs.drain(in); }
            }
            f.stream().count();
        }
    }

    /** SevenZFile through its builder, with random-access entry streams. */
    @Endpoint(bailsOn = {IOException.class})
    public static void sevenz_builder(@Bytes(max = 65536, type = "SevenZ") byte[] data, @OneOf(strings = {"", "secret", "foo"}) String password,
                                      boolean recover, boolean defaultName, @OneOf(ints = {65536, 1024, 16}) int memKiB, boolean random) throws IOException {
        SevenZFile.Builder b = SevenZFile.builder().setSeekableByteChannel(new SeekableInMemoryByteChannel(data)).setTryToRecoverBrokenArchives(recover)
            .setUseDefaultNameForUnnamedEntries(defaultName).setDefaultName("unnamed").setMaxMemoryLimitKiB(memKiB);
        if (!password.isEmpty()) b.setPassword(password);
        try (SevenZFile f = b.get()) {
            f.getDefaultName();
            int n = 0;
            long total = 0;
            for (SevenZArchiveEntry e : f.getEntries()) {
                if (++n > 5000) Stitch.bail("too many entries");
                inspectEntry(e);
                if (random && e.hasStream()) {
                    try (InputStream in = f.getInputStream(e)) { total += Stubs.drain(in).length; }
                    if (total > CAP) Stitch.bail("output over 4 MB");
                }
            }
            if (!random) {
                SevenZArchiveEntry e;
                while ((e = f.getNextEntry()) != null) {
                    f.getStatisticsForCurrentEntry().getCompressedCount();
                    byte[] buf = new byte[4096];
                    int r;
                    while ((r = f.read(buf, 0, buf.length)) > 0) { total += r; if (total > CAP) Stitch.bail("output over 4 MB"); }
                    f.read();
                }
            }
            f.stream().count();
            f.toString();
        }
    }

    /** Pack200 (the bundled Harmony unpacker) on fuzzed pack files. */
    @Endpoint(bailsOn = {IOException.class})
    public static void pack200(@Bytes(max = 16384, type = "Pack200") byte[] data, Pack200Strategy strategy, boolean props) throws IOException {
        Map<String, String> p = props ? Collections.singletonMap("pack.unknown.attribute", "pass") : null;
        try (Pack200CompressorInputStream in = p == null ? new Pack200CompressorInputStream(new ByteArrayInputStream(data), strategy)
                                                         : new Pack200CompressorInputStream(new ByteArrayInputStream(data), strategy, p)) {
            in.available();
            in.markSupported();
            byte[] jar = Stubs.drain(in);
            try (JarArchiveInputStream j = new JarArchiveInputStream(new ByteArrayInputStream(jar))) { Stubs.walk(j); }
        }
        Pack200CompressorInputStream.matches(data, data.length);
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void pack200_unpacker(@Bytes(max = 16384, type = "Pack200") byte[] data) throws IOException {
        org.apache.commons.compress.java.util.jar.Pack200.Unpacker u = org.apache.commons.compress.java.util.jar.Pack200.newUnpacker();
        u.properties();
        java.beans.PropertyChangeListener l = ev -> { };
        u.addPropertyChangeListener(l);
        u.removePropertyChangeListener(l);
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        try (java.util.jar.JarOutputStream out = new java.util.jar.JarOutputStream(b)) {
            u.unpack(new ByteArrayInputStream(data), out);
        }
    }

    /** Oracle: a jar of resources packs with Pack200 and unpacks to the same resources. */
    @Endpoint(bailsOn = {IOException.class})
    public static void pack200_roundtrip(@Bytes(max = 2048) byte[] content, @Range(lo = 1, hi = 4) int files, Pack200Strategy strategy) throws IOException {
        ByteArrayOutputStream jar = new ByteArrayOutputStream();
        try (JarArchiveOutputStream out = new JarArchiveOutputStream(jar)) {
            for (int i = 0; i < files; i++) {
                JarArchiveEntry e = new JarArchiveEntry("r/f" + i + ".txt");
                out.putArchiveEntry(e);
                out.write(content, 0, content.length * (i + 1) / files);
                out.closeArchiveEntry();
            }
        }
        ByteArrayOutputStream packed = new ByteArrayOutputStream();
        try (Pack200CompressorOutputStream out = new Pack200CompressorOutputStream(packed, strategy)) {
            out.write(jar.toByteArray());
            out.finish();
        }
        Map<String, byte[]> got = new TreeMap<>();
        try (Pack200CompressorInputStream in = new Pack200CompressorInputStream(new ByteArrayInputStream(packed.toByteArray()), strategy);
             JarArchiveInputStream j = new JarArchiveInputStream(in)) {
            ArchiveEntry e;
            while ((e = j.getNextEntry()) != null) if (!e.isDirectory() && !e.getName().startsWith("META-INF")) got.put(e.getName(), Stubs.drain(j));
        }
        Stitch.check(got.size() == files, "pack200 round trip keeps the files");
        for (int i = 0; i < files; i++) Stitch.check(Arrays.equals(got.get("r/f" + i + ".txt"), Arrays.copyOf(content, content.length * (i + 1) / files)), "pack200 round trip keeps the contents");
        org.apache.commons.compress.java.util.jar.Pack200.Packer pk = org.apache.commons.compress.java.util.jar.Pack200.newPacker();
        pk.properties();
        java.beans.PropertyChangeListener l = ev -> { };
        pk.addPropertyChangeListener(l);
        pk.removePropertyChangeListener(l);
        try (java.util.jar.JarInputStream ji = new java.util.jar.JarInputStream(new ByteArrayInputStream(jar.toByteArray()))) {
            pk.pack(ji, new ByteArrayOutputStream());
        }
    }

    // ---------------------------------------------------------------- ZIP extra fields
    static final ExtraFieldParsingBehavior[] BEHAVIORS = {
        ZipArchiveEntry.ExtraFieldParsingMode.BEST_EFFORT, ZipArchiveEntry.ExtraFieldParsingMode.STRICT_FOR_KNOW_EXTRA_FIELDS,
        ZipArchiveEntry.ExtraFieldParsingMode.ONLY_PARSEABLE_LENIENT, ZipArchiveEntry.ExtraFieldParsingMode.ONLY_PARSEABLE_STRICT,
        ZipArchiveEntry.ExtraFieldParsingMode.DRACONIC};
    static final ExtraFieldUtils.UnparseableExtraField[] UNPARSEABLE = {ExtraFieldUtils.UnparseableExtraField.THROW, ExtraFieldUtils.UnparseableExtraField.SKIP, ExtraFieldUtils.UnparseableExtraField.READ};

    /** Raw extra-field blocks: parse, then merge and parse again gives the same header ids. */
    @Endpoint(bailsOn = {java.util.zip.ZipException.class})
    public static void extra_fields(@Bytes(max = 512) byte[] data, boolean local, @Range(lo = 0, hi = 8) int mode) throws IOException {
        ZipExtraField[] f;
        if (mode < 5) f = ExtraFieldUtils.parse(data, local, BEHAVIORS[mode]);
        else if (mode < 8) f = ExtraFieldUtils.parse(data, local, UNPARSEABLE[mode - 5]);
        else f = ExtraFieldUtils.parse(data, local);
        for (ZipExtraField x : f) inspectExtra(x);
        byte[] merged = local ? ExtraFieldUtils.mergeLocalFileDataData(f) : ExtraFieldUtils.mergeCentralDirectoryData(f);
        ZipExtraField[] again = ExtraFieldUtils.parse(merged, local, ExtraFieldUtils.UnparseableExtraField.READ);
        Stitch.check(again.length == f.length, "merged extra fields parse back to as many fields");
        for (int i = 0; i < f.length; i++) Stitch.check(again[i].getHeaderId().equals(f[i].getHeaderId()), "merged extra fields keep their header ids");
        ExtraFieldUtils.parse(data);
        ExtraFieldUtils.createExtraField(new ZipShort(data.length > 1 ? ZipShort.getValue(data) : 0));
    }

    static ZipExtraField newExtra(int kind) {
        switch (kind) {
            case 0: return new AsiExtraField();
            case 1: return new X5455_ExtendedTimestamp();
            case 2: return new X7875_NewUnix();
            case 3: return new X000A_NTFS();
            case 4: return new Zip64ExtendedInformationExtraField();
            case 5: return new ResourceAlignmentExtraField();
            case 6: return new UnicodePathExtraField();
            case 7: return new UnicodeCommentExtraField();
            case 8: return new UnrecognizedExtraField();
            case 9: return new UnparseableExtraFieldData();
            case 10: return JarMarker.getInstance();
            case 11: return new X0015_CertificateIdForFile();
            case 12: return new X0016_CertificateIdForCentralDirectory();
            case 13: return new X0017_StrongEncryptionHeader();
            case 14: return new X0014_X509Certificates();
            default: return new X0019_EncryptionRecipientCertificateList();
        }
    }

    /** One extra-field type on fuzzed bytes; what it writes, it parses back to the same bytes. */
    @Endpoint(bailsOn = {java.util.zip.ZipException.class})
    public static void extra_field(@Range(lo = 0, hi = 15) int kind, @Bytes(max = 128) byte[] data, boolean local) throws IOException {
        ZipExtraField f = newExtra(kind);
        if (local) f.parseFromLocalFileData(data, 0, data.length); else f.parseFromCentralDirectoryData(data, 0, data.length);
        inspectExtra(f);
        byte[] out = local ? f.getLocalFileDataData() : f.getCentralDirectoryData();
        Stitch.require(out != null, "no data of this kind");
        Stitch.check(out.length == (local ? f.getLocalFileDataLength() : f.getCentralDirectoryLength()).getValue(), "data length matches getXxxLength()");
        ZipExtraField g = newExtra(kind);
        if (local) g.parseFromLocalFileData(out, 0, out.length); else g.parseFromCentralDirectoryData(out, 0, out.length);
        byte[] out2 = local ? g.getLocalFileDataData() : g.getCentralDirectoryData();
        // the first parse may normalize (flags without data, ...); from then on the data is stable
        ZipExtraField h = newExtra(kind);
        if (local) h.parseFromLocalFileData(out2, 0, out2.length); else h.parseFromCentralDirectoryData(out2, 0, out2.length);
        byte[] out3 = local ? h.getLocalFileDataData() : h.getCentralDirectoryData();
        if (kind != 4) Stitch.check(Arrays.equals(out2, out3), "an extra field re-parses to the same data");
        ExtraFieldUtils.fillExtraField(newExtra(kind), data, 0, data.length, local);
        if (f instanceof X0017_StrongEncryptionHeader) {
            X0017_StrongEncryptionHeader x = new X0017_StrongEncryptionHeader();
            if (local) x.parseFileFormat(data, 0, data.length); else x.parseCentralDirectoryFormat(data, 0, data.length);
        }
    }

    /** Extra fields built through their setters write and re-parse to the same values. */
    @Endpoint(bailsOn = {java.util.zip.ZipException.class})
    public static void extra_field_build(@Range(lo = 0, hi = 6) int kind, long a, long b, @Range(lo = 0, hi = 0177777) int mode, @Str(max = 32) String s, boolean flag) throws IOException, CloneNotSupportedException {
        ZipExtraField f;
        switch (kind) {
            case 0: { AsiExtraField x = new AsiExtraField(); x.setUserId((int) a); x.setGroupId((int) b); x.setLinkedFile(s); x.setDirectory(flag); x.setMode(mode);
                      AsiExtraField c = (AsiExtraField) x.clone(); Stitch.check(c.getUserId() == x.getUserId() && c.getLinkedFile().equals(x.getLinkedFile()), "AsiExtraField clone"); f = x; break; }
            case 1: { X5455_ExtendedTimestamp x = new X5455_ExtendedTimestamp(); x.setFlags((byte) mode); x.setModifyTime(new ZipLong(a & 0xffffffffL)); x.setAccessTime(new ZipLong(b & 0xffffffffL));
                      x.setCreateJavaTime(new Date((a & 0x7fffffffL) * 1000)); x.setModifyJavaTime(new Date((b & 0x7fffffffL) * 1000)); x.setAccessJavaTime(null);
                      x.setCreateFileTime(FileTime.fromMillis((a & 0x7fffffffL) * 1000)); x.setModifyFileTime(FileTime.fromMillis((b & 0x7fffffffL) * 1000)); x.setAccessFileTime(null); x.setCreateTime(new ZipLong(mode)); x.clone(); f = x; break; }
            case 2: { X7875_NewUnix x = new X7875_NewUnix(); x.setUID(a & 0xffffffffL); x.setGID(b & 0xffffffffL); x.clone(); f = x; break; }
            case 3: { X000A_NTFS x = new X000A_NTFS(); x.setModifyTime(new ZipEightByteInteger(a)); x.setAccessTime(new ZipEightByteInteger(b)); x.setCreateTime(new ZipEightByteInteger(mode));
                      x.setModifyJavaTime(new Date(a & 0xffffffffffL)); x.setAccessFileTime(FileTime.fromMillis(b & 0xffffffffffL)); x.setCreateFileTime(null); x.setCreateJavaTime(null); x.setModifyFileTime(FileTime.fromMillis(mode)); x.setAccessJavaTime(new Date(mode)); f = x; break; }
            case 4: { Zip64ExtendedInformationExtraField x = new Zip64ExtendedInformationExtraField(new ZipEightByteInteger(a), new ZipEightByteInteger(b));
                      x.setRelativeHeaderOffset(flag ? new ZipEightByteInteger(mode) : null); x.setDiskStartNumber(flag ? new ZipLong(mode) : null); x.setSize(new ZipEightByteInteger(a)); x.setCompressedSize(new ZipEightByteInteger(b)); f = x; break; }
            case 5: f = new ResourceAlignmentExtraField(mode & 0x7fff, flag); break;
            default: { UnrecognizedExtraField x = new UnrecognizedExtraField(); x.setHeaderId(new ZipShort(mode)); x.setLocalFileDataData(s.getBytes(StandardCharsets.UTF_8)); x.setCentralDirectoryData(flag ? null : new byte[] {1, 2}); f = x; }
        }
        byte[] local = f.getLocalFileDataData();
        ZipExtraField g = ExtraFieldUtils.createExtraField(f.getHeaderId());
        g.parseFromLocalFileData(local, 0, local.length);
        Stitch.check(Arrays.equals(g.getLocalFileDataData(), local), "an extra field built with setters re-parses to the same data");
        byte[] cd = f.getCentralDirectoryData();
        ZipExtraField h = ExtraFieldUtils.createExtraField(f.getHeaderId());
        h.parseFromCentralDirectoryData(cd, 0, cd.length);
        new UnicodePathExtraField(s, s.getBytes(StandardCharsets.UTF_8)).getUnicodeName();
        UnicodeCommentExtraField uc = new UnicodeCommentExtraField(s, s.getBytes(StandardCharsets.UTF_8), 0, s.getBytes(StandardCharsets.UTF_8).length);
        uc.setUnicodeName(s.getBytes(StandardCharsets.UTF_8)); uc.setNameCRC32(s.length());
        ExtraFieldUtils.register(AsiExtraField.class);
        PKWareExtraHeader.EncryptionAlgorithm.getAlgorithmByCode(mode & 0xffff);
        PKWareExtraHeader.HashAlgorithm.getAlgorithmByCode(mode & 0xffff);
        for (PKWareExtraHeader.EncryptionAlgorithm e : PKWareExtraHeader.EncryptionAlgorithm.values()) e.getCode();
        for (PKWareExtraHeader.HashAlgorithm e : PKWareExtraHeader.HashAlgorithm.values()) e.getCode();
    }

    // ---------------------------------------------------------------- ZIP entries with every setting
    /** A ZIP entry with comment, extra fields, attributes, alignment; read back with ZipFile. */
    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void zip_entry_roundtrip(@Str(max = 64, type = "EntryName") String name, @Str(max = 32) String comment, @Bytes(max = 2048) byte[] content,
                                           @Range(lo = 0, hi = 0177777) int unixMode, boolean stored, @OneOf(ints = {0, 4, 64}) int align, @Range(lo = 0, hi = 9) int level,
                                           boolean utf8Flag, boolean fallback, @Range(lo = 0, hi = 2) int zip64, @Range(lo = 0, hi = 3) int extras, boolean raw) throws IOException {
        Stitch.require(!name.isEmpty() && name.indexOf('\0') < 0, "not a file name");
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        SeekableInMemoryByteChannel ch = new SeekableInMemoryByteChannel();
        ZipArchiveOutputStream out = raw ? new ZipArchiveOutputStream(b) : new ZipArchiveOutputStream(ch);
        out.setComment(comment); out.setLevel(level); out.setUseLanguageEncodingFlag(utf8Flag); out.setFallbackToUTF8(fallback); out.setUseZip64(Zip64Mode.values()[zip64]);
        out.getEncoding(); out.isSeekable();
        ZipArchiveEntry e = new ZipArchiveEntry(name);
        e.setMethod(stored ? ZipArchiveEntry.STORED : ZipArchiveEntry.DEFLATED);
        e.setUnixMode(unixMode); e.setComment(comment); e.setAlignment(align); e.setInternalAttributes(1); e.setVersionMadeBy(0x314); e.setVersionRequired(20);
        e.setTime(FileTime.fromMillis(1_000_000_000_000L)); e.setLastModifiedTime(FileTime.fromMillis(1_000_000_000_000L)); e.setCreationTime(FileTime.fromMillis(999_000_000_000L)); e.setLastAccessTime(FileTime.fromMillis(998_000_000_000L));
        if (stored) { e.setSize(content.length); CRC32 crc = new CRC32(); crc.update(content); e.setCrc(crc.getValue()); }
        if (extras >= 1) { X7875_NewUnix u = new X7875_NewUnix(); u.setUID(1000); u.setGID(1000); e.addExtraField(u); }
        if (extras >= 2) { AsiExtraField asi = new AsiExtraField(); asi.setMode(unixMode); e.addAsFirstExtraField(asi); }
        if (extras >= 3) { e.removeExtraField(new ZipShort(0x756e)); e.setCentralDirectoryExtra(new byte[0]); }
        e.setNameSource(ZipArchiveEntry.NameSource.NAME); e.setCommentSource(ZipArchiveEntry.CommentSource.COMMENT);
        Stitch.check(out.canWriteEntryData(e), "deflated/stored entries are writable");
        out.putArchiveEntry(e);
        out.write(content);
        out.closeArchiveEntry();
        out.flush();
        out.getBytesWritten();
        out.finish();
        int size = raw ? 0 : (int) ch.size();   // (closing the stream closes the channel)
        out.close();
        byte[] data = raw ? b.toByteArray() : Arrays.copyOf(ch.array(), size);
        try (ZipFile z = ZipFile.builder().setByteArray(data).get()) {
            ZipArchiveEntry back = z.getEntry(name);
            Stitch.require(back != null, "name not encodable");
            Stitch.check(back.getMethod() == e.getMethod(), "method read back");
            try (InputStream in = z.getInputStream(back)) { Stitch.check(Arrays.equals(Stubs.drain(in), content), "contents read back"); }
            Stitch.check(back.getUnixMode() == unixMode, "unix mode read back");
            if (align > 0 && !stored) { /* alignment applies to stored entries only */ }
            else if (align > 0) Stitch.check(back.getDataOffset() % align == 0, "a stored entry's data is aligned as requested");
            inspectEntry(back);
        }
        ZipArchiveEntry c = new ZipArchiveEntry(e);
        c.setExtraFields(new ZipExtraField[] {new X7875_NewUnix()});
        c.setGeneralPurposeBit(new GeneralPurposeBit());
        c.setRawFlag(0); c.setExternalAttributes(0); c.setDiskNumberStart(0);
        if (c.getUnparseableExtraFieldData() != null) c.removeUnparseableExtraFieldData();
    }

    @Endpoint
    public static void gpb(boolean a, boolean b, boolean c, boolean d, @Bytes(max = 4) byte[] raw) {
        GeneralPurposeBit g = new GeneralPurposeBit();
        g.useDataDescriptor(a); g.useEncryption(b); g.useStrongEncryption(c); g.useUTF8ForNames(d);
        inspectGpb(g);
        if (raw.length >= 2) inspectGpb(GeneralPurposeBit.parse(raw, 0));
    }

    // ---------------------------------------------------------------- 7z writer
    static final SevenZMethod[] SEVENZ = {SevenZMethod.COPY, SevenZMethod.LZMA2, SevenZMethod.LZMA, SevenZMethod.DEFLATE, SevenZMethod.BZIP2};

    /** Oracle: what SevenZOutputFile writes, SevenZFile reads back (names, contents, attributes). */
    @Endpoint(bailsOn = {IOException.class})
    public static void sevenz_roundtrip(@Bytes(max = 4096) byte[] content, @Range(lo = 1, hi = 4) int files, @Range(lo = 0, hi = 4) int method, @Range(lo = 0, hi = 3) int filter,
                                        boolean dir, boolean anti, long time, boolean perEntry) throws IOException {
        SeekableInMemoryByteChannel ch = new SeekableInMemoryByteChannel();
        int[] size = {0};
        List<String> names = new ArrayList<>();
        try (SevenZOutputFile out = new SevenZOutputFile(ch)) {
            List<SevenZMethodConfiguration> methods = new ArrayList<>();
            if (filter == 1) methods.add(new SevenZMethodConfiguration(SevenZMethod.DELTA_FILTER, 2));
            if (filter == 2) methods.add(new SevenZMethodConfiguration(SevenZMethod.BCJ_X86_FILTER));
            methods.add(new SevenZMethodConfiguration(SEVENZ[method], method == 3 ? (Object) 6 : null));
            if (filter == 3) out.setContentCompression(SEVENZ[method]); else out.setContentMethods(methods);
            for (int i = 0; i < files; i++) {
                SevenZArchiveEntry e = new SevenZArchiveEntry();
                String n = "d/f" + i;
                e.setName(n);
                e.setLastModifiedDate(new Date(time & 0x3fffffffffffL));
                e.setHasLastModifiedDate(true);
                e.setWindowsAttributes(0x20); e.setHasWindowsAttributes(true);
                if (i == 1) { e.setCreationDate(time & 0x3fffffffffffL); e.setAccessDate(new Date(1000)); e.setAccessTime(FileTime.fromMillis(2000)); e.setCreationTime(FileTime.fromMillis(3000)); e.setLastModifiedTime(FileTime.fromMillis(4000)); e.setLastModifiedDate(5000L); e.setAccessDate(6000L); e.setCreationDate(new Date(7000)); }
                if (perEntry) e.setContentMethods(new SevenZMethodConfiguration(SevenZMethod.LZMA2));
                out.putArchiveEntry(e);
                int len = content.length * (i + 1) / files;
                if (i % 2 == 0) out.write(content, 0, len); else { out.write(Arrays.copyOf(content, len)); }
                out.closeArchiveEntry();
                names.add(n);
            }
            if (dir) { SevenZArchiveEntry d = out.createArchiveEntry(new File("/tmp"), "d"); d.setDirectory(true); out.putArchiveEntry(d); out.closeArchiveEntry(); }
            if (anti) { SevenZArchiveEntry d = new SevenZArchiveEntry(); d.setName("gone"); d.setAntiItem(true); d.setHasStream(false); out.putArchiveEntry(d); out.closeArchiveEntry(); }
            out.finish();
            size[0] = (int) ch.size();   // (closing the file closes the channel)
        }
        byte[] data = Arrays.copyOf(ch.array(), size[0]);
        try (SevenZFile f = SevenZFile.builder().setByteArray(data).get()) {
            int i = 0;
            SevenZArchiveEntry e;
            while ((e = f.getNextEntry()) != null) {
                inspectEntry(e);
                if (e.isDirectory() || e.isAntiItem()) continue;
                Stitch.check(i < files && e.getName().equals(names.get(i)), "7z entry names read back in order");
                byte[] got = new byte[(int) e.getSize()];
                int off = 0, r;
                while (off < got.length && (r = f.read(got, off, got.length - off)) > 0) off += r;
                Stitch.check(Arrays.equals(got, Arrays.copyOf(content, content.length * (i + 1) / files)), "7z entry contents read back");
                i++;
            }
            Stitch.check(i == files, "7z file count read back");
        }
    }

    // ---------------------------------------------------------------- TAR entries with every setting
    /** Oracle: a TAR entry with PAX headers, long names, ids and devices reads back the same. */
    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void tar_entry_roundtrip(@Str(max = 200, type = "EntryName") String name, @Str(max = 40) String user, @Str(max = 40) String link,
                                           long uid, @Range(lo = 0, hi = 3) int kind, @OneOf(ints = {0, 1, 2, 3}) int longMode, @Bytes(max = 1024) byte[] content,
                                           @Str(max = 16) String paxKey, @Str(max = 32) String paxValue, @Range(lo = 0, hi = 2) int bigNumbers) throws IOException {
        Stitch.require(!name.isEmpty() && name.indexOf('\0') < 0 && !name.endsWith("/"), "not a file name");
        Stitch.require(user.indexOf('\0') < 0 && link.indexOf('\0') < 0, "NUL in a name");
        byte flag = new byte[] {TarConstants.LF_NORMAL, TarConstants.LF_SYMLINK, TarConstants.LF_CHR, TarConstants.LF_FIFO}[kind];
        TarArchiveEntry e = new TarArchiveEntry(name, flag);
        e.setUserName(user); e.setGroupName(user); e.setIds(1, 2); e.setUserId(bigNumbers == 0 ? uid & 03777777 : uid & 0x7fffffffffffL); e.setGroupId((int) (uid & 0x7ffff));
        e.setModTime(new Date(1_000_000_000_000L)); e.setModTime(FileTime.fromMillis(1_000_000_000_000L)); e.setMode(0100644);
        e.setLastAccessTime(FileTime.fromMillis(1_000_000_100_000L)); e.setCreationTime(FileTime.fromMillis(1_000_000_200_000L)); e.setStatusChangeTime(FileTime.fromMillis(1_000_000_300_000L));
        if (kind == 0) e.setSize(content.length);
        if (kind == 1) e.setLinkName(link);
        if (kind == 2) { e.setDevMajor(8); e.setDevMinor((int) (uid & 0xff)); }
        if (!paxKey.isEmpty() && paxKey.indexOf('=') < 0 && paxKey.indexOf('\n') < 0 && paxValue.indexOf('\n') < 0) { e.addPaxHeader("SCHILY." + paxKey, paxValue); }
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        try (TarArchiveOutputStream out = new TarArchiveOutputStream(b, TarConstants.DEFAULT_BLKSIZE, "UTF-8")) {
            out.setLongFileMode(longMode); out.setBigNumberMode(bigNumbers); out.setAddPaxHeadersForNonAsciiNames(true);
            try {
                out.putArchiveEntry(e);
            } catch (IllegalArgumentException x) {
                throw Stitch.bail("refused: " + x.getMessage());   // documented: a too long name with LONGFILE_ERROR
            }
            if (kind == 0) out.write(content);
            out.closeArchiveEntry();
            out.getRecordSize(); out.getCount(); out.getBytesWritten();
        }
        try (TarFile f = new TarFile(b.toByteArray())) {
            Stitch.require(f.getEntries().size() == 1, "an entry the writer dropped");
            TarArchiveEntry back = f.getEntries().get(0);
            if (longMode >= TarArchiveOutputStream.LONGFILE_GNU) Stitch.check(back.getName().equals(name), "TAR entry name read back");
            Stitch.check(back.getLinkFlag() == flag, "TAR link flag read back");
            if (kind == 0) try (InputStream in = f.getInputStream(back)) { Stitch.check(Arrays.equals(Stubs.drain(in), content), "TAR contents read back"); }
            if (kind == 1 && longMode >= TarArchiveOutputStream.LONGFILE_GNU) Stitch.check(back.getLinkName().equals(link), "TAR link name read back");
            Stitch.check(back.getLongUserId() == (bigNumbers == 0 ? uid & 03777777 : uid & 0x7fffffffffffL), "TAR user id read back");
            inspectEntry(back);
        }
        e.clearExtraPaxHeaders();
        e.setNames(user, user); e.setName(name); e.setDataOffset(0);
        e.setSparseHeaders(Collections.singletonList(new TarArchiveStructSparse(0, content.length)));
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void tar_header(@Bytes(max = 1024, type = "Tar") byte[] data, boolean lenient) throws IOException {
        Stitch.require(data.length >= 512, "not a header block");
        byte[] header = Arrays.copyOf(data, 512);
        TarArchiveEntry e = new TarArchiveEntry(header, ZipEncodingHelper.getZipEncoding("UTF-8"), lenient);
        inspectEntry(e);
        new TarArchiveEntry(Collections.singletonMap("path", "x"), header, ZipEncodingHelper.getZipEncoding("ISO-8859-1"), lenient, 512L);
        TarUtils.verifyCheckSum(header);
        TarUtils.computeCheckSum(header);
        TarArchiveSparseEntry s = new TarArchiveSparseEntry(header);
        s.isExtended(); s.getSparseHeaders();
    }

    // ---------------------------------------------------------------- codecs and utilities
    @Endpoint(bailsOn = {IOException.class})
    public static void tar_utils(@Bytes(max = 64) byte[] buf, long v, @Range(lo = 1, hi = 16) int len, @Str(max = 16) String name) throws IOException {
        int n = Math.min(len, buf.length);
        if (n > 0) {
            try { TarUtils.parseOctal(buf, 0, n); } catch (ArchiveException e) { /* documented: not an octal number */ }
            try { TarUtils.parseOctalOrBinary(buf, 0, n); } catch (ArchiveException e) { /* documented */ }
            TarUtils.parseBoolean(buf, 0);
            TarUtils.parseName(buf, 0, n);
            TarUtils.parseName(buf, 0, n, ZipEncodingHelper.getZipEncoding("UTF-8"));
            if (buf.length >= 24) try { TarUtils.parseSparse(buf, 0); } catch (ArchiveException e) { /* documented */ }
        }
        long u = v & 077777777777L;
        byte[] out = new byte[12];
        TarUtils.formatLongOctalBytes(u, out, 0, 12);
        Stitch.check(TarUtils.parseOctal(out, 0, 12) == u, "parseOctal(formatLongOctalBytes(v)) == v");
        byte[] out2 = new byte[12];
        TarUtils.formatLongOctalOrBinaryBytes(v, out2, 0, 12);
        Stitch.check(TarUtils.parseOctalOrBinary(out2, 0, 12) == v, "parseOctalOrBinary(formatLongOctalOrBinaryBytes(v)) == v");
        byte[] out3 = new byte[8];
        TarUtils.formatOctalBytes(v & 07777777, out3, 0, 8);
        TarUtils.formatCheckSumOctalBytes(v & 0777777, out3, 0, 8);
        TarUtils.formatUnsignedOctalString(v & 0777, out3, 0, 8);
        byte[] nm = new byte[32];
        TarUtils.formatNameBytes(name, nm, 0, 32);
        TarUtils.formatNameBytes(name, nm, 0, 32, ZipEncodingHelper.getZipEncoding("UTF-8"));
    }

    @Endpoint
    public static void zip_numbers(long v, int s, @Bytes(max = 16) byte[] b) {
        long u32 = v & 0xffffffffL;
        Stitch.check(ZipLong.getValue(ZipLong.getBytes(u32)) == u32 && new ZipLong(u32).getValue() == u32 && new ZipLong(new ZipLong(u32).getBytes()).getValue() == u32, "ZipLong round trips");
        Stitch.check(new ZipLong((int) v).getIntValue() == (int) v, "ZipLong int value");
        int u16 = s & 0xffff;
        Stitch.check(ZipShort.getValue(ZipShort.getBytes(u16)) == u16 && new ZipShort(new ZipShort(u16).getBytes()).getValue() == u16, "ZipShort round trips");
        Stitch.check(ZipEightByteInteger.getLongValue(ZipEightByteInteger.getBytes(v)) == v && new ZipEightByteInteger(v).getLongValue() == v, "ZipEightByteInteger round trips");
        BigInteger big = BigInteger.valueOf(v).and(BigInteger.ONE.shiftLeft(64).subtract(BigInteger.ONE));
        Stitch.check(ZipEightByteInteger.getValue(ZipEightByteInteger.getBytes(big)).equals(big) && new ZipEightByteInteger(big).getValue().equals(big), "ZipEightByteInteger BigInteger round trips");
        new ZipEightByteInteger(ZipEightByteInteger.getBytes(v)).getBytes();
        byte[] buf = new byte[8];
        ZipLong.putLong(u32, buf, 0); ZipShort.putShort(u16, buf, 4);
        Stitch.check(ZipLong.getValue(buf, 0) == u32 && ZipShort.getValue(buf, 4) == u16, "put/get at an offset");
        new ZipLong(u32).putLong(buf, 0); new ZipLong(u32).clone(); new ZipShort(u16).clone();
        if (b.length >= 8) { ZipLong.getValue(b); ZipShort.getValue(b); ZipEightByteInteger.getLongValue(b, 0); ZipEightByteInteger.getValue(b); new ZipLong(b, 0); new ZipShort(b, 0); new ZipEightByteInteger(b, 0); }
        ZipUtil.reverse(b.clone());
        Stitch.check(ZipUtil.signedByteToUnsignedInt(ZipUtil.unsignedIntToSignedByte(u16 & 0xff)) == (u16 & 0xff), "byte conversions round trip");
        ZipUtil.adjustToLong(s);
        long t = 315532800000L + (u32 % 4000000000000L);
        byte[] dos = ZipUtil.toDosTime(t);
        long back = ZipUtil.dosToJavaTime(ZipLong.getValue(dos));
        Stitch.check(Math.abs(back - t) <= 2000 || t >= 4354819200000L, "DOS time round trips to two seconds");
        ZipUtil.toDosTime(t, buf, 0); ZipUtil.fromDosTime(new ZipLong(dos)); ZipUtil.toDosTime(new Date(t)); ZipUtil.isDosTime(t);
        ZipMethod.getMethodByCode(s & 0xffff);
        for (ZipMethod m : ZipMethod.values()) Stitch.check(ZipMethod.getMethodByCode(m.getCode()) == m || m.getCode() < 0, "getMethodByCode(getCode())");
        ByteUtils.toLittleEndian(buf, v, 0, 8);
        Stitch.check(ByteUtils.fromLittleEndian(buf) == v && ByteUtils.fromLittleEndian(buf, 0, 8) == v, "ByteUtils little endian round trips");
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void byte_utils(long v, @Range(lo = 1, hi = 8) int len) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        ByteUtils.toLittleEndian(b, v, len);
        ByteUtils.toLittleEndian((DataOutput) new DataOutputStream(b), v, len);
        ByteUtils.ByteConsumer cons = new ByteUtils.OutputStreamByteConsumer(b);
        ByteUtils.toLittleEndian(cons, v, len);
        cons.accept(1);
        byte[] a = b.toByteArray();
        long mask = len == 8 ? -1L : (1L << (8 * len)) - 1;
        Stitch.check(ByteUtils.fromLittleEndian(new ByteArrayInputStream(a), len) == (v & mask), "stream little endian round trips");
        ByteUtils.ByteSupplier sup = new ByteUtils.InputStreamByteSupplier(new ByteArrayInputStream(a, len, a.length - len));
        Stitch.check(ByteUtils.fromLittleEndian(sup, len) == (v & mask), "supplier little endian round trips");
        sup.getAsByte();
        ByteUtils.fromLittleEndian((DataInput) new DataInputStream(new ByteArrayInputStream(a)), len);
    }

    @Endpoint
    public static void times(long v) {
        long ms = v % 253402300799000L;
        Date d = new Date(ms);
        Stitch.check(TimeUtils.ntfsTimeToDate(TimeUtils.toNtfsTime(d)).getTime() == ms, "NTFS time round trips");
        FileTime ft = FileTime.fromMillis(ms);
        Stitch.check(TimeUtils.ntfsTimeToFileTime(TimeUtils.toNtfsTime(ft)).toMillis() == ms, "NTFS FileTime round trips");
        Stitch.check(TimeUtils.toDate(TimeUtils.toFileTime(d)).equals(d), "Date/FileTime round trips");
        long sec = Math.floorDiv(ms, 1000);
        Stitch.check(TimeUtils.toUnixTime(TimeUtils.unixTimeToFileTime(sec)) == sec, "unix time round trips");
        TimeUtils.isUnixTime(ft); TimeUtils.isUnixTime(sec); TimeUtils.truncateToHundredNanos(ft); TimeUtils.toNtfsTime(ms);
    }

    @Endpoint
    public static void encodings(@Str(max = 32) String s, @OneOf(strings = {"UTF-8", "CP437", "ISO-8859-1", "US-ASCII", "UTF-16", "Cp1252", "x-unknown"}) String cs) throws IOException {
        ZipEncoding enc = ZipEncodingHelper.getZipEncoding(cs);
        ZipEncodingHelper.getZipEncoding(StandardCharsets.UTF_8);
        if (enc.canEncode(s)) {
            ByteBuffer b = enc.encode(s);
            byte[] raw = Arrays.copyOfRange(b.array(), b.arrayOffset() + b.position(), b.arrayOffset() + b.limit());
            Stitch.check(enc.decode(raw).equals(s), "decode(encode(s)) == s when canEncode(s)");
        }
        if (enc instanceof CharsetAccessor) ((CharsetAccessor) enc).getCharset();
        Charsets.toCharset(cs.equals("x-unknown") ? null : cs);
        Charsets.toCharset(StandardCharsets.UTF_8);
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void archive_utils(@Str(max = 32) String s, @Bytes(max = 32) byte[] b, @Range(lo = 0, hi = 40) int len) throws IOException {
        String san = ArchiveUtils.sanitize(s);
        Stitch.check(san.length() <= s.length() * 2 + 1, "sanitize length");
        byte[] ascii = ArchiveUtils.toAsciiBytes(s);
        ArchiveUtils.toAsciiString(ascii); ArchiveUtils.toAsciiString(b, 0, b.length);
        Stitch.check(ArchiveUtils.matchAsciiBuffer(new String(ascii, StandardCharsets.US_ASCII), ascii), "matchAsciiBuffer of its own bytes");
        ArchiveUtils.matchAsciiBuffer(s, b, 0, b.length);
        ArchiveUtils.isEqual(b, ascii); ArchiveUtils.isEqual(b, ascii, true); ArchiveUtils.isEqual(b, 0, b.length, ascii, 0, ascii.length);
        ArchiveUtils.isEqual(b, 0, b.length, ascii, 0, ascii.length, true); ArchiveUtils.isEqualWithNull(b, 0, b.length, ascii, 0, ascii.length);
        ArchiveUtils.isArrayZero(b, b.length);
        try { ArchiveUtils.checkEntryNameLength(len, 32, "x"); } catch (ArchiveException e) { Stitch.check(len > 32, "checkEntryNameLength rejects only long names"); }
        try { ParsingUtils.parseIntValue(s); ParsingUtils.parseLongValue(s, 16); ParsingUtils.parseIntValue(s, 8); ParsingUtils.parseLongValue(s); } catch (IOException e) { /* documented: not a number */ }
        try { ExactMath.add(len, (long) Integer.MAX_VALUE); } catch (IOException e) { /* documented: overflow */ }
        if (s.indexOf('\0') < 0) {   // (commons-io rejects NUL in file names)
            FileNameUtils.getBaseName(s); FileNameUtils.getExtension(s);
            try { Path p = Paths.get(s.isEmpty() ? "x" : s); FileNameUtils.getBaseName(p); FileNameUtils.getExtension(p); } catch (InvalidPathException e) { /* not a path */ }
        }
        Lists.newArrayList(); Sets.newHashSet("a", "b"); Iterators.addAll(new ArrayList<>(), Arrays.asList(1, 2).iterator());
    }

    @Endpoint
    public static void file_names(@Str(max = 32) String s) {
        String c = GzipUtils.getCompressedFileName(s);
        if (!s.isEmpty() && !s.startsWith(".")) Stitch.check(GzipUtils.isCompressedFileName(c), "gzip compressed name is recognized");
        GzipUtils.getUncompressedFileName(s); GzipUtils.getCompressedFilename(s); GzipUtils.getUncompressedFilename(s); GzipUtils.isCompressedFilename(s);
        BZip2Utils.getCompressedFileName(s); BZip2Utils.getUncompressedFileName(s); BZip2Utils.isCompressedFileName(s); BZip2Utils.getCompressedFilename(s); BZip2Utils.getUncompressedFilename(s); BZip2Utils.isCompressedFilename(s);
        XZUtils.getCompressedFileName(s); XZUtils.getUncompressedFileName(s); XZUtils.isCompressedFileName(s); XZUtils.getCompressedFilename(s); XZUtils.getUncompressedFilename(s); XZUtils.isCompressedFilename(s);
        LZMAUtils.getCompressedFileName(s); LZMAUtils.getUncompressedFileName(s); LZMAUtils.isCompressedFileName(s); LZMAUtils.getCompressedFilename(s); LZMAUtils.getUncompressedFilename(s); LZMAUtils.isCompressedFilename(s);
        FileNameUtil u = new FileNameUtil(Collections.singletonMap(".tgz", ".tar"), ".gz");
        u.getCompressedFileName(s); u.getUncompressedFileName(s); u.isCompressedFileName(s); u.getCompressedFilename(s); u.getUncompressedFilename(s); u.isCompressedFilename(s);
        XZUtils.isXZCompressionAvailable(); LZMAUtils.isLZMACompressionAvailable(); BrotliUtilsHelper.check();
        XZUtils.setCacheXZAvailablity(true); LZMAUtils.setCacheLZMAAvailablity(true);
    }

    static class BrotliUtilsHelper {
        static void check() {
            org.apache.commons.compress.compressors.brotli.BrotliUtils.isBrotliCompressionAvailable();
            org.apache.commons.compress.compressors.brotli.BrotliUtils.setCacheBrotliAvailablity(true);
            org.apache.commons.compress.compressors.zstandard.ZstdUtils.setCacheZstdAvailablity(false);
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void bits(@Bytes(max = 64) byte[] data, boolean little, @Max(32) Source s) throws IOException {
        try (BitInputStream in = new BitInputStream(new ByteArrayInputStream(data), little ? ByteOrder.LITTLE_ENDIAN : ByteOrder.BIG_ENDIAN)) {
            in.getByteOrder();
            long avail = in.bitsAvailable();
            Stitch.check(avail == 8L * data.length, "bitsAvailable() counts every bit");
            while (s.more()) {
                switch (s.choice(5)) {
                    case 0: in.readBit(); break;
                    case 1: in.readBits((int) s.range(1, 63)); break;
                    case 2: in.alignWithByteBoundary(); break;
                    case 3: in.clearBitCache(); break;
                    default: in.bitsCached(); in.getBytesRead();
                }
            }
        }
    }

    /** Canonical Huffman decoding from fuzzed code lengths. */
    @Endpoint(bailsOn = {IOException.class})
    public static void huffman(@Bytes(max = 32) byte[] lengths, @Bytes(max = 64) byte[] data) throws IOException {
        // a complete prefix code: start from one leaf and split leaves the fuzzer picks (sometimes raw lengths)
        int[] l;
        if (lengths.length > 0 && (lengths[0] & 7) == 0) {
            l = new int[lengths.length];
            for (int i = 0; i < l.length; i++) l[i] = lengths[i] & 0x0f;
        } else {
            List<Integer> leaves = new ArrayList<>(Collections.singletonList(0));
            for (byte x : lengths) {
                int k = (x & 0xff) % leaves.size();
                int d = leaves.get(k);
                if (d >= 15) continue;
                leaves.set(k, d + 1); leaves.add(k + 1, d + 1);
            }
            l = new int[leaves.size()];
            for (int i = 0; i < l.length; i++) l[i] = leaves.get(i);
        }
        HuffmanDecoder d = lengths.length % 2 == 0 ? new HuffmanDecoder(l) : new HuffmanDecoder(l, 0, 15);
        d.getMaxLength(); d.getMinLength();
        try (BitInputStream in = new BitInputStream(new ByteArrayInputStream(data), ByteOrder.LITTLE_ENDIAN)) {
            for (int i = 0; i < 64; i++) {
                int sym = d.decodeSymbol(in);
                if (sym < 0) break;
                Stitch.check(sym < l.length && l[sym] > 0, "decoded symbol has a code");
            }
        }
    }

    // ---------------------------------------------------------------- channels and stream wrappers
    @Endpoint(bailsOn = {IOException.class})
    public static void channels(@Bytes(max = 256) byte[] a, @Bytes(max = 256) byte[] b, @Range(lo = 0, hi = 600) int pos, @Range(lo = 0, hi = 300) int len) throws IOException {
        Stitch.check(new SeekableInMemoryByteChannel(16).size() == 16, "a channel over a 16-byte array has size 16");
        SeekableInMemoryByteChannel c = new SeekableInMemoryByteChannel(new byte[0]);
        c.write(ByteBuffer.wrap(a));
        Stitch.check(c.size() == a.length && c.position() == a.length, "write grows the channel");
        c.position(pos);
        c.write(ByteBuffer.wrap(b));
        Stitch.check(c.size() == Math.max(a.length, pos + b.length), "write past the end grows the channel");
        c.position(pos);
        ByteBuffer r = ByteBuffer.allocate(b.length);
        while (r.hasRemaining() && c.read(r) > 0) { }
        Stitch.check(Arrays.equals(r.array(), b), "read back what was written");
        c.truncate(len);
        Stitch.check(c.size() == Math.min(len, Math.max(a.length, pos + b.length)), "truncate");
        c.array(); c.isOpen(); c.close();
        SeekableByteChannel m = MultiReadOnlySeekableByteChannel.forSeekableByteChannels(new SeekableInMemoryByteChannel(a), new SeekableInMemoryByteChannel(b));
        Stitch.check(m.size() == a.length + b.length, "concatenated size");
        byte[] all = new byte[a.length + b.length];
        System.arraycopy(a, 0, all, 0, a.length); System.arraycopy(b, 0, all, a.length, b.length);
        long p = Math.min(pos, all.length);
        m.position(p);
        ByteBuffer rb = ByteBuffer.allocate(all.length - (int) p);
        while (rb.hasRemaining() && m.read(rb) > 0) { }
        Stitch.check(Arrays.equals(rb.array(), Arrays.copyOfRange(all, (int) p, all.length)), "concatenated channel reads the concatenation");
        m.position(); m.isOpen();
        try { m.write(ByteBuffer.wrap(a)); } catch (java.nio.channels.NonWritableChannelException e) { /* documented */ }
        try { m.truncate(0); } catch (java.nio.channels.NonWritableChannelException e) { /* documented */ }
        ((MultiReadOnlySeekableByteChannel) m).position(1, 0);
        m.close();
        new MultiReadOnlySeekableByteChannel(Arrays.asList(new SeekableInMemoryByteChannel(a))).size();
    }

    /** Documented: the no-argument constructor makes an empty channel. */
    @Endpoint(bailsOn = {IOException.class})
    public static void channel_new() throws IOException {
        try (SeekableInMemoryByteChannel c = new SeekableInMemoryByteChannel()) {
            Stitch.check(c.size() == 0, "new SeekableInMemoryByteChannel() is empty");
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void zip_split(@Bytes(max = 65536, type = "Zip") byte[] data, @Range(lo = 1, hi = 4) int parts) throws IOException {
        Stitch.require(data.length >= 4 && parts >= 1, "too short");
        byte[] first = Arrays.copyOf(data, data.length);
        // a split archive's first segment starts with the split signature
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        b.write(new byte[] {0x50, 0x4b, 0x07, 0x08});
        b.write(first);
        byte[] all = b.toByteArray();
        List<SeekableByteChannel> segs = new ArrayList<>();
        int step = (all.length + parts - 1) / parts;
        for (int i = 0; i < all.length; i += step) segs.add(new SeekableInMemoryByteChannel(Arrays.copyOfRange(all, i, Math.min(all.length, i + step))));
        SeekableByteChannel last = segs.remove(segs.size() - 1);
        try (SeekableByteChannel ch = ZipSplitReadOnlySeekableByteChannel.forOrderedSeekableByteChannels(last, segs)) {
            try (ZipFile z = ZipFile.builder().setSeekableByteChannel(ch).setMaxNumberOfDisks(parts).get()) {
                for (ZipArchiveEntry e : Collections.list(z.getEntries())) inspectEntry(e);
            }
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void streams(@Bytes(max = 1024) byte[] data, @Range(lo = 0, hi = 1100) int n, @Range(lo = 1, hi = 64) int block) throws IOException {
        Stitch.check(Arrays.equals(IOUtils.toByteArray(new ByteArrayInputStream(data)), data), "toByteArray");
        byte[] buf = new byte[Math.min(n, 2048)];
        int r = IOUtils.readFully(new ByteArrayInputStream(data), buf);
        Stitch.check(r == Math.min(data.length, buf.length), "readFully reads what is there");
        IOUtils.readFully(new ByteArrayInputStream(data), buf, 0, buf.length);
        Stitch.check(IOUtils.skip(new ByteArrayInputStream(data), n) == Math.min(n, data.length), "skip skips what is there");
        Stitch.check(Arrays.equals(IOUtils.readRange(new ByteArrayInputStream(data), n), Arrays.copyOf(data, Math.min(n, data.length))), "readRange");
        IOUtils.readRange(java.nio.channels.Channels.newChannel(new ByteArrayInputStream(data)), n);
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        Stitch.check(IOUtils.copy(new ByteArrayInputStream(data), o) == data.length && IOUtils.copy(new ByteArrayInputStream(data), o, block) == data.length, "copy");
        Stitch.check(IOUtils.copyRange(new ByteArrayInputStream(data), n, o) == Math.min(n, data.length) && IOUtils.copyRange(new ByteArrayInputStream(data), n, o, block) == Math.min(n, data.length), "copyRange");
        IOUtils.closeQuietly(o);
        IOUtils.readFully(java.nio.channels.Channels.newChannel(new ByteArrayInputStream(data)), ByteBuffer.allocate(Math.min(n, data.length)));
        CountingInputStream ci = new CountingInputStream(new ByteArrayInputStream(data));
        Stubs.drain(ci); ci.read(); ci.read(new byte[4]);
        Stitch.check(ci.getBytesRead() == data.length, "CountingInputStream counts");
        CountingOutputStream co = new CountingOutputStream(new ByteArrayOutputStream());
        co.write(data); co.write(1); co.write(data, 0, data.length);
        Stitch.check(co.getBytesWritten() == 2L * data.length + 1, "CountingOutputStream counts");
        BoundedInputStream bi = new BoundedInputStream(new ByteArrayInputStream(data), n);
        Stitch.check(Stubs.drain(bi).length == Math.min(n, data.length), "BoundedInputStream bounds");
        bi.getBytesRemaining();
        CRC32 crc = new CRC32(); crc.update(data);
        ChecksumCalculatingInputStream cc = new ChecksumCalculatingInputStream(new CRC32(), new ByteArrayInputStream(data));
        Stubs.drain(cc);
        Stitch.check(cc.getValue() == crc.getValue(), "ChecksumCalculatingInputStream");
        try (CRC32VerifyingInputStream cv = new CRC32VerifyingInputStream(new ByteArrayInputStream(data), data.length, crc.getValue())) { Stubs.drain(cv); cv.getBytesRemaining(); }
        try (ChecksumVerifyingInputStream cv = new ChecksumVerifyingInputStream(new CRC32(), new ByteArrayInputStream(data), data.length, crc.getValue() ^ (n == 0 ? 0 : 1))) {
            Stubs.drain(cv);
            Stitch.check(n == 0, "a wrong checksum is detected");
        } catch (IOException e) { Stitch.check(n != 0, "only a wrong checksum is rejected"); }
        new CRC32VerifyingInputStream(new ByteArrayInputStream(data), data.length, (int) crc.getValue()).close();
        ByteArrayOutputStream fb = new ByteArrayOutputStream();
        try (FixedLengthBlockOutputStream f = new FixedLengthBlockOutputStream(fb, block)) {
            f.write(data, 0, data.length); f.write(7); f.write(ByteBuffer.wrap(data)); f.isOpen(); f.flushBlock();
        }
        Stitch.check(fb.size() % block == 0 && fb.size() >= 2 * data.length + 1, "FixedLengthBlockOutputStream writes whole blocks");
        new FixedLengthBlockOutputStream(java.nio.channels.Channels.newChannel(new ByteArrayOutputStream()), block).close();
        new CloseShieldFilterInputStream(new ByteArrayInputStream(data)).close();
        new FlushShieldFilterOutputStream(new ByteArrayOutputStream()).flush();
        new SkipShieldingInputStream(new ByteArrayInputStream(data)).skip(n);
        BoundedArchiveInputStream ba = new BoundedArchiveInputStream(0, data.length) {
            @Override protected int read(long pos, ByteBuffer buf) { int k = Math.min(buf.remaining(), data.length - (int) pos); if (k <= 0) return -1; buf.put(data, (int) pos, k); buf.flip(); return k; }
        };
        Stitch.check(Arrays.equals(Stubs.drain(ba), data), "BoundedArchiveInputStream reads the range");
        new BoundedSeekableByteChannelInputStream(0, data.length, new SeekableInMemoryByteChannel(data)).read();
    }

    // ---------------------------------------------------------------- compressors: settings and metadata
    static String latin1(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) b.append(c == 0 ? '_' : c > 0xff ? '?' : c);
        return b.toString();
    }

    /** Oracle: gzip metadata written through GzipParameters is read back by getMetaData(). */
    @Endpoint(bailsOn = {IOException.class})
    public static void gzip_meta(@Bytes(max = 2048) byte[] data, @Str(max = 24) String name, @Str(max = 24) String comment, @Range(lo = 0, hi = 0x7fffffff) int mtime,
                                 GzipParameters.OS os, boolean headerCrc, @Range(lo = 0, hi = 3) int subfields, @Range(lo = -1, hi = 9) int level, boolean builder) throws IOException {
        name = latin1(name); comment = latin1(comment);   // header strings are NUL-terminated ISO-8859-1
        GzipParameters p = new GzipParameters();
        p.setFileName(name); p.setComment(comment); p.setModificationTime(mtime); p.setOS(os); p.setHeaderCRC(headerCrc);
        p.setCompressionLevel(level); p.setBufferSize(512); p.setDeflateStrategy(0); p.setFileNameCharset(StandardCharsets.ISO_8859_1);
        ExtraField ef = new ExtraField();
        for (int i = 0; i < subfields; i++) ef.addSubField("A" + (char) ('a' + i), Arrays.copyOf(data, Math.min(data.length, 16 * i)));
        p.setExtraField(ef);
        ef.size(); ef.isEmpty(); ef.getEncodedSize(); ef.findFirstSubField("Aa"); ef.iterator();
        if (subfields > 0) { ExtraField.SubField sf = ef.getSubField(0); sf.getId(); sf.getPayload(); }
        p.getBufferSize(); p.getComment(); p.getCompressionLevel(); p.getDeflateStrategy(); p.getExtraField(); p.getFileName(); p.getFileNameCharset(); p.getFilename(); p.getHeaderCRC(); p.getModificationInstant(); p.getModificationTime(); p.getOS(); p.getOperatingSystem(); p.getTrailerCrc(); p.getTrailerISize();
        p.toString();
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        try (GzipCompressorOutputStream out = new GzipCompressorOutputStream(b, p)) { out.write(data); out.write(data, 0, data.length / 2); out.finish(); }
        int[] members = {0, 0};
        GzipCompressorInputStream in = builder
            ? GzipCompressorInputStream.builder().setByteArray(b.toByteArray()).setDecompressConcatenated(true).setFileNameCharset(StandardCharsets.ISO_8859_1).setIgnoreExtraField(false)
                .setOnMemberStart(g -> members[0]++).setOnMemberEnd(g -> members[1]++).get()
            : new GzipCompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
        try (GzipCompressorInputStream g = in) {
            byte[] back = Stubs.drain(g);
            Stitch.check(back.length == data.length + data.length / 2, "gzip contents read back");
            GzipParameters m = g.getMetaData();
            Stitch.check(Objects.equals(m.getFileName(), name.isEmpty() ? null : name) || Objects.equals(m.getFileName(), name), "gzip file name read back");
            Stitch.check(Objects.equals(m.getComment(), comment.isEmpty() ? null : comment) || Objects.equals(m.getComment(), comment), "gzip comment read back");
            Stitch.check(m.getModificationTime() == mtime, "gzip mtime (seconds) read back");
            Stitch.check(!builder || m.getExtraField() == null && subfields == 0 || m.getExtraField() != null && m.getExtraField().size() == subfields, "gzip extra subfields read back");
            g.getCompressedCount();
            if (builder) Stitch.check(members[0] == 1 && members[1] == 1, "one member start and end");
        }
        GzipParameters.OS.from(os.type());
        GzipCompressorInputStream.matches(b.toByteArray(), b.size());
        p.setFilename(name); p.setModificationInstant(java.time.Instant.ofEpochSecond(mtime)); p.setOperatingSystem(3);
        Stitch.check(p.equals(p) && p.hashCode() == p.hashCode(), "equals");
        ef.clear();
    }

    /** Oracle: the LZ77 engine's blocks reconstruct the input. */
    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static void lz77(@Bytes(max = 4096) byte[] data, @OneOf(ints = {256, 1024, 65536}) int window, @Range(lo = 0, hi = 3) int tune, @Range(lo = 3, hi = 300) int maxBack,
                            @Range(lo = 1, hi = 300) int maxLit, @Range(lo = 1, hi = 300) int candidates, boolean prefill) throws IOException {
        Parameters.Builder pb = Parameters.builder(window);
        if (tune == 1) pb.tunedForSpeed(); else if (tune == 2) pb.tunedForCompressionRatio();
        else if (tune == 3) pb.withMinBackReferenceLength(3).withMaxBackReferenceLength(maxBack).withMaxOffset(window - 1).withMaxLiteralLength(maxLit).withNiceBackReferenceLength(maxBack).withMaxNumberOfCandidates(candidates).withLazyMatching(maxLit % 2 == 0).withLazyThreshold(maxBack / 2);
        Parameters p = pb.build();
        p.getLazyMatching(); p.getLazyMatchingThreshold(); p.getMaxBackReferenceLength(); p.getMaxCandidates(); p.getMaxLiteralLength(); p.getMaxOffset(); p.getMinBackReferenceLength(); p.getNiceBackReferenceLength(); p.getWindowSize();
        ByteArrayOutputStream rebuilt = new ByteArrayOutputStream();
        byte[] dict = prefill && data.length > 8 ? Arrays.copyOf(data, data.length / 4) : new byte[0];
        rebuilt.write(dict);
        boolean[] done = {false};
        LZ77Compressor c = new LZ77Compressor(p, block -> {
            block.getType();
            if (block instanceof LZ77Compressor.LiteralBlock) {
                LZ77Compressor.LiteralBlock l = (LZ77Compressor.LiteralBlock) block;
                rebuilt.write(l.getData(), l.getOffset(), l.getLength());
            } else if (block instanceof LZ77Compressor.BackReference) {
                LZ77Compressor.BackReference r = (LZ77Compressor.BackReference) block;
                byte[] cur = rebuilt.toByteArray();
                Stitch.check(r.getOffset() >= 1 && r.getOffset() <= cur.length, "back reference within the output");
                for (int i = 0; i < r.getLength(); i++) { cur = rebuilt.toByteArray(); rebuilt.write(cur[cur.length - r.getOffset()]); }
            } else if (block instanceof LZ77Compressor.EOD) done[0] = true;
        });
        if (dict.length > 0) c.prefill(dict);
        byte[] rest = Arrays.copyOfRange(data, dict.length, data.length);
        c.compress(rest, 0, rest.length / 2);
        c.compress(Arrays.copyOfRange(rest, rest.length / 2, rest.length));
        c.finish();
        Stitch.check(done[0], "EOD block at the end");
        Stitch.check(Arrays.equals(rebuilt.toByteArray(), data), "LZ77 blocks rebuild the input");
        new LZ77Compressor.BackReference(1, 4).toString();
        new LZ77Compressor.LiteralBlock(data, 0, data.length).getData();
    }

    /** The other compressor streams and their settings; round trips through each. */
    @Endpoint(bailsOn = {IOException.class})
    public static void compress_more(@Bytes(max = 4096) byte[] data, @Range(lo = 0, hi = 8) int kind, @Range(lo = 1, hi = 9) int level, boolean useBuilder) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        InputStream back;
        switch (kind) {
            case 0: {
                try (BZip2CompressorOutputStream o = new BZip2CompressorOutputStream(b, BZip2CompressorOutputStream.chooseBlockSize(data.length))) { o.getBlockSize(); o.write(data); o.flush(); o.finish(); }
                back = new BZip2CompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                BZip2CompressorInputStream.matches(b.toByteArray(), b.size());
                break;
            }
            case 1: {
                try (XZCompressorOutputStream o = useBuilder ? XZCompressorOutputStream.builder().setOutputStream(b).setLzma2Options(new org.tukaani.xz.LZMA2Options(level % 7)).get() : new XZCompressorOutputStream(b, level % 7)) { o.write(data); o.finish(); }
                back = useBuilder ? XZCompressorInputStream.builder().setByteArray(b.toByteArray()).setDecompressConcatenated(true).setMemoryLimitKiB(1 << 16).get() : new XZCompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                XZCompressorInputStream.matches(b.toByteArray(), b.size()); XZUtils.matches(b.toByteArray(), b.size());
                break;
            }
            case 2: {
                try (LZMACompressorOutputStream o = useBuilder ? LZMACompressorOutputStream.builder().setOutputStream(b).setLzma2Options(new org.tukaani.xz.LZMA2Options(level % 7)).get() : new LZMACompressorOutputStream(b)) { o.write(data); o.flush(); o.finish(); }
                back = useBuilder ? LZMACompressorInputStream.builder().setByteArray(b.toByteArray()).setMemoryLimitKiB(1 << 16).get() : new LZMACompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                LZMACompressorInputStream.matches(b.toByteArray(), b.size()); LZMAUtils.matches(b.toByteArray(), b.size());
                break;
            }
            case 3: {
                try (DeflateCompressorOutputStream o = new DeflateCompressorOutputStream(b, new DeflateParameters())) { o.write(data); o.flush(); o.finish(); }
                DeflateParameters dp = new DeflateParameters(); dp.setCompressionLevel(level); dp.getCompressionLevel(); dp.withZlibHeader();
                back = new DeflateCompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                DeflateCompressorInputStream.matches(b.toByteArray(), b.size());
                break;
            }
            case 4: {
                try (SnappyCompressorOutputStream o = new SnappyCompressorOutputStream(b, data.length, SnappyCompressorOutputStream.createParameterBuilder(1 << 15).build())) { o.write(data); o.finish(); }
                SnappyCompressorInputStream si = new SnappyCompressorInputStream(new ByteArrayInputStream(b.toByteArray()), 1 << 15);
                Stitch.check(si.getSize() == data.length, "snappy size header");
                back = si;
                break;
            }
            case 5: {
                try (BlockLZ4CompressorOutputStream o = new BlockLZ4CompressorOutputStream(b, BlockLZ4CompressorOutputStream.createParameterBuilder().tunedForSpeed().build())) {
                    if (data.length > 16) o.prefill(data, 0, 8);
                    o.write(data); o.finish();
                }
                back = null;
                break;
            }
            case 6: {
                try (FramedSnappyCompressorOutputStream o = new FramedSnappyCompressorOutputStream(b)) { o.write(data); o.finish(); }
                back = new FramedSnappyCompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                FramedSnappyCompressorInputStream.matches(b.toByteArray(), b.size());
                break;
            }
            case 7: {
                try (FramedLZ4CompressorOutputStream o = new FramedLZ4CompressorOutputStream(b)) { o.write(data); o.finish(); }
                back = new FramedLZ4CompressorInputStream(new ByteArrayInputStream(b.toByteArray()));
                FramedLZ4CompressorInputStream.matches(b.toByteArray(), b.size());
                break;
            }
            default: {
                CompressorStreamFactory f = new CompressorStreamFactory(true, 1 << 16);
                String name = new String[] {CompressorStreamFactory.GZIP, CompressorStreamFactory.BZIP2, CompressorStreamFactory.XZ, CompressorStreamFactory.LZMA, CompressorStreamFactory.DEFLATE,
                                            CompressorStreamFactory.SNAPPY_FRAMED, CompressorStreamFactory.LZ4_BLOCK, CompressorStreamFactory.LZ4_FRAMED, CompressorStreamFactory.PACK200}[level - 1];
                try {
                    try (OutputStream o = f.createCompressorOutputStream(name, b)) { if (!name.equals(CompressorStreamFactory.PACK200)) o.write(data); }
                    back = name.equals(CompressorStreamFactory.PACK200) ? null : f.createCompressorInputStream(name, new ByteArrayInputStream(b.toByteArray()));
                } catch (CompressorException e) {
                    throw Stitch.bail("compressor not available");
                }
            }
        }
        if (back != null) {
            try (InputStream in = back) {
                Stitch.check(Arrays.equals(Stubs.drain(in), data), "decompress(compress(data)) == data");
                if (in instanceof CompressorInputStream) { CompressorInputStream ci = (CompressorInputStream) in; ci.getBytesRead(); ci.getCount(); ci.getUncompressedCount(); }
                if (in instanceof InputStreamStatistics) { ((InputStreamStatistics) in).getCompressedCount(); ((InputStreamStatistics) in).getUncompressedCount(); }
                if (in instanceof SnappyCompressorInputStream) ((SnappyCompressorInputStream) in).getUncompressedSize();
            }
        } else if (kind == 5) {
            try (BlockLZ4CompressorInputStream in = new BlockLZ4CompressorInputStream(new ByteArrayInputStream(b.toByteArray()))) {
                byte[] got = Stubs.drain(in);
                Stitch.check(data.length <= 16 ? Arrays.equals(got, data) : got.length == data.length, "LZ4 block round trip");
                in.getCompressedCount(); in.getSize();
            }
        }
    }

    /** Stream readers' skip/available/mark and statistics on fuzzed input. */
    @Endpoint(bailsOn = {IOException.class})
    public static void decompress_api(@Bytes(max = 32768, type = "Gz") byte[] d0, @Range(lo = 0, hi = 9) int kind, @Range(lo = 0, hi = 4096) int skip,
                                      @Bytes(max = 4096) byte[] raw, boolean valid, @Range(lo = -1, hi = 512) int flip) throws IOException {
        byte[] d = d0;
        if (valid && kind <= 7 && kind != 5) {   // a valid stream of the kind, maybe with one byte flipped
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            try (OutputStream o = kind == 0 ? new GzipCompressorOutputStream(b) : kind == 1 ? new BZip2CompressorOutputStream(b) : kind == 2 ? new XZCompressorOutputStream(b)
                    : kind == 3 ? new LZMACompressorOutputStream(b) : kind == 4 ? new DeflateCompressorOutputStream(b) : kind == 6 ? new FramedLZ4CompressorOutputStream(b)
                    : new SnappyCompressorOutputStream(b, raw.length)) {
                o.write(raw);
            }
            d = b.toByteArray();
            if (flip >= 0 && flip < d.length) d[flip] ^= 0x5a;
        }
        InputStream in;
        ByteArrayInputStream src = new ByteArrayInputStream(d);
        switch (kind) {
            case 0: in = GzipCompressorInputStream.builder().setInputStream(src).setDecompressConcatenated(true).get(); break;
            case 1: in = new BZip2CompressorInputStream(src, true); break;
            case 2: in = XZCompressorInputStream.builder().setInputStream(src).setMemoryLimitKiB(1 << 16).get(); break;
            case 3: in = LZMACompressorInputStream.builder().setInputStream(src).setMemoryLimitKiB(1 << 16).get(); break;
            case 4: in = new DeflateCompressorInputStream(src); break;
            case 5: in = new org.apache.commons.compress.compressors.deflate64.Deflate64CompressorInputStream(src); break;
            case 6: in = new FramedLZ4CompressorInputStream(src); break;
            case 7: in = new SnappyCompressorInputStream(src); break;
            case 8: in = new ZCompressorInputStream(src, 1 << 16); break;
            default: in = new org.apache.commons.compress.compressors.brotli.BrotliCompressorInputStream(src);
        }
        try (InputStream i = in) {
            i.available();
            i.markSupported();
            i.skip(skip);
            i.read();
            Stubs.drain(i);
            if (i instanceof InputStreamStatistics) { ((InputStreamStatistics) i).getCompressedCount(); ((InputStreamStatistics) i).getUncompressedCount(); }
            if (i instanceof CompressorInputStream) ((CompressorInputStream) i).getBytesRead();
        }
        ZCompressorInputStream.matches(d, d.length);
    }

    @Endpoint
    public static void factories(@Str(max = 16) String enc) {
        CompressorStreamFactory c = CompressorStreamFactory.getSingleton();
        c.getCompressorInputStreamProviders(); c.getCompressorOutputStreamProviders(); c.getDecompressUntilEOF(); c.getInputStreamCompressorNames(); c.getOutputStreamCompressorNames();
        CompressorStreamFactory.findAvailableCompressorInputStreamProviders(); CompressorStreamFactory.findAvailableCompressorOutputStreamProviders();
        CompressorStreamFactory.getBrotli(); CompressorStreamFactory.getBzip2(); CompressorStreamFactory.getDeflate(); CompressorStreamFactory.getDeflate64(); CompressorStreamFactory.getGzip();
        CompressorStreamFactory.getLZ4Block(); CompressorStreamFactory.getLZ4Framed(); CompressorStreamFactory.getLzma(); CompressorStreamFactory.getPack200(); CompressorStreamFactory.getSnappyFramed();
        CompressorStreamFactory.getSnappyRaw(); CompressorStreamFactory.getXz(); CompressorStreamFactory.getZ(); CompressorStreamFactory.getZstandard();
        new CompressorStreamFactory().setDecompressConcatenated(true);
        for (CompressorStreamProvider p : CompressorStreamFactory.findAvailableCompressorInputStreamProviders().values()) { p.getInputStreamCompressorNames(); p.getOutputStreamCompressorNames(); }
        ArchiveStreamFactory a = new ArchiveStreamFactory(enc.isEmpty() ? null : enc);
        a.getArchiveInputStreamProviders(); a.getArchiveOutputStreamProviders(); a.getEntryEncoding(); a.getInputStreamArchiveNames(); a.getOutputStreamArchiveNames();
        new ArchiveStreamFactory().setEntryEncoding("UTF-8");
        ArchiveStreamFactory.findAvailableArchiveInputStreamProviders(); ArchiveStreamFactory.findAvailableArchiveOutputStreamProviders();
        for (ArchiveStreamProvider p : ArchiveStreamFactory.findAvailableArchiveInputStreamProviders().values()) { p.getInputStreamArchiveNames(); p.getOutputStreamArchiveNames(); }
        Stitch.check(ArchiveStreamFactory.DEFAULT != null, "default factory");
    }

    // ---------------------------------------------------------------- ChangeSet, Expander, Archiver, parallel ZIP
    /** Oracle: ChangeSet deletes and adds entries of a written archive. */
    @Endpoint(bailsOn = {IOException.class})
    public static void change_set(Stubs.Archive a, @Range(lo = 0, hi = 8) int del, boolean deleteDir, @Bytes(max = 256) byte[] add, boolean replace) throws Exception {
        Stitch.require(!a.w.names.isEmpty() && (a.w.format.equals("zip") || a.w.format.equals("tar") || a.w.format.equals("jar")), "a non-empty zip or tar");
        String victim = a.w.names.get(del % a.w.names.size());
        Stitch.require(a.w.names.indexOf(victim) == a.w.names.lastIndexOf(victim) && !victim.equals("added.bin"), "a duplicate name");
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ChangeSetResults res;
        if (a.w.format.equals("tar")) {
            ChangeSet<TarArchiveEntry> cs = new ChangeSet<>();
            if (deleteDir) cs.deleteDir(victim); else cs.delete(victim);
            TarArchiveEntry e = new TarArchiveEntry("added.bin"); e.setSize(add.length);
            if (replace) cs.add(e, new ByteArrayInputStream(add), true); else cs.add(e, new ByteArrayInputStream(add));
            try (TarArchiveInputStream in = new TarArchiveInputStream(new ByteArrayInputStream(a.data), a.w.encoding);
                 TarArchiveOutputStream o = new TarArchiveOutputStream(out, a.w.encoding)) {
                o.setLongFileMode(TarArchiveOutputStream.LONGFILE_POSIX);
                res = new ChangeSetPerformer<TarArchiveInputStream, TarArchiveOutputStream, TarArchiveEntry>(cs).perform(in, o);
            }
        } else {
            ChangeSet<ZipArchiveEntry> cs = new ChangeSet<>();
            if (deleteDir) cs.deleteDir(victim); else cs.delete(victim);
            cs.add(new ZipArchiveEntry("added.bin"), new ByteArrayInputStream(add), replace);
            try (ZipFile zf = ZipFile.builder().setByteArray(a.data).setCharset(a.w.encoding).get();
                 ZipArchiveOutputStream o = new ZipArchiveOutputStream(out)) {
                res = new ChangeSetPerformer<ZipArchiveInputStream, ZipArchiveOutputStream, ZipArchiveEntry>(cs).perform(zf, o);
            }
        }
        res.getAddedFromChangeSet(); res.getAddedFromStream(); res.getDeleted();
        Stitch.check(res.getAddedFromChangeSet().contains("added.bin"), "the added entry is reported");
        if (!deleteDir) Stitch.check(!res.getAddedFromStream().contains(victim), "a deleted entry is not copied");
    }

    static Path tempRoot() throws IOException {
        Path root = Paths.get(System.getProperty("java.io.tmpdir"), "stitch-expand");
        Files.createDirectories(root);
        return root;
    }

    static void deleteTree(Path p) throws IOException {
        if (!Files.exists(p, LinkOption.NOFOLLOW_LINKS)) return;
        Files.walk(p).sorted(Comparator.reverseOrder()).forEach(x -> { try { Files.delete(x); } catch (IOException e) { /* best effort */ } });
    }

    /** Oracle: Expander writes nothing outside the target directory (path traversal, symlinks). */
    @Endpoint(bailsOn = {IOException.class})
    public static void expand(@Bytes(max = 32768, type = "Zip") byte[] data, @OneOf(strings = {"zip", "tar", "ar", "cpio", "7z", "auto"}) String format, @Range(lo = 0, hi = 2) int how) throws IOException {
        Path root = tempRoot();
        Path sandbox = Files.createTempDirectory(root, "s");
        Path target = sandbox.resolve("target");
        Files.createDirectories(target);
        try {
            Expander x = new Expander();
            if (format.equals("7z")) {
                try (SevenZFile f = SevenZFile.builder().setByteArray(data).get()) { x.expand(f, target); }
            } else if (format.equals("zip") && how == 1) {
                try (ZipFile f = ZipFile.builder().setByteArray(data).get()) { x.expand(f, target.toFile()); }
            } else if (format.equals("tar") && how == 1) {
                try (TarFile f = new TarFile(data)) { x.expand(f, target); }
            } else if (format.equals("auto")) {
                x.expand(new BufferedInputStream(new ByteArrayInputStream(data)), target.toFile());
            } else if (how == 2) {
                x.expand(format, new SeekableInMemoryByteChannel(data), target, ch -> { });
            } else {
                x.expand(format, new ByteArrayInputStream(data), target.toFile());
            }
        } catch (InvalidPathException e) {
            // an entry name this platform cannot represent as a path (NUL, or unmappable in the JVM's
            // file-name encoding): Expander lets Path's unchecked exception through
            throw Stitch.bail("entry name is not a path here");
        } finally {
            try (java.util.stream.Stream<Path> s = Files.list(sandbox)) {
                List<Path> outside = new ArrayList<>();
                s.forEach(outside::add);
                boolean ok = outside.size() == 1 && outside.get(0).equals(target);
                deleteTree(sandbox);
                Stitch.check(ok, "Expander writes only inside the target directory");
            }
        }
    }

    /** Oracle: Archiver archives a directory tree that Expander restores. */
    @Endpoint(bailsOn = {IOException.class})
    public static void archive_dir(@Bytes(max = 1024) byte[] content, @Range(lo = 1, hi = 3) int files, @OneOf(strings = {"zip", "tar", "cpio", "7z", "jar"}) String format, boolean useChannel) throws IOException {
        Path sandbox = Files.createTempDirectory(tempRoot(), "a");
        try {
            Path src = sandbox.resolve("src");
            Files.createDirectories(src.resolve("sub"));
            for (int i = 0; i < files; i++) Files.write(src.resolve(i % 2 == 0 ? "f" + i : "sub/f" + i), Arrays.copyOf(content, content.length * (i + 1) / files));
            Path arc = sandbox.resolve("out.arc");
            Archiver ar = new Archiver();
            if (format.equals("7z")) { try (SevenZOutputFile o = new SevenZOutputFile(Files.newByteChannel(arc, StandardOpenOption.CREATE, StandardOpenOption.WRITE))) { ar.create(o, src); } }
            else if (useChannel && format.equals("zip")) { try (SeekableByteChannel ch = Files.newByteChannel(arc, StandardOpenOption.CREATE, StandardOpenOption.WRITE)) { ar.create(format, ch, src); } }
            else ar.create(format, arc, src);
            Path dst = sandbox.resolve("dst");
            Files.createDirectories(dst);
            Expander x = new Expander();
            if (format.equals("7z")) { try (SevenZFile f = SevenZFile.builder().setPath(arc).get()) { x.expand(f, dst); } }
            else x.expand(format.equals("jar") ? "zip" : format, arc, dst);
            for (int i = 0; i < files; i++) {
                Path p = dst.resolve(i % 2 == 0 ? "f" + i : "sub/f" + i);
                Stitch.check(Files.exists(p) && Arrays.equals(Files.readAllBytes(p), Arrays.copyOf(content, content.length * (i + 1) / files)), "Archiver/Expander restore the files");
            }
        } finally {
            deleteTree(sandbox);
        }
    }

    /** Oracle: entries written through the parallel / scatter ZIP API read back. */
    @Endpoint(bailsOn = {IOException.class})
    public static void scatter_zip(@Bytes(max = 2048) byte[] content, @Range(lo = 1, hi = 4) int files, boolean parallel, @Range(lo = 0, hi = 9) int level) throws Exception {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        Path tmp = Files.createTempDirectory(tempRoot(), "z");
        try {
            try (ZipArchiveOutputStream out = new ZipArchiveOutputStream(b)) {
                if (parallel) {
                    ParallelScatterZipCreator c = new ParallelScatterZipCreator(Executors.newSingleThreadExecutor(), new DefaultBackingStoreSupplier(tmp), level);
                    for (int i = 0; i < files; i++) {
                        ZipArchiveEntry e = new ZipArchiveEntry("p" + i); e.setMethod(ZipArchiveEntry.DEFLATED);
                        byte[] part = Arrays.copyOf(content, content.length * (i + 1) / files);
                        c.addArchiveEntry(e, () -> new ByteArrayInputStream(part));
                    }
                    c.writeTo(out);
                    ScatterStatistics st = c.getStatisticsMessage();
                    st.getCompressionElapsed(); st.getMergingElapsed();
                } else {
                    try (ScatterZipOutputStream s = ScatterZipOutputStream.pathBased(tmp.resolve("scatter"), level)) {
                        for (int i = 0; i < files; i++) {
                            ZipArchiveEntry e = new ZipArchiveEntry("p" + i); e.setMethod(ZipArchiveEntry.DEFLATED);
                            byte[] part = Arrays.copyOf(content, content.length * (i + 1) / files);
                            ZipArchiveEntryRequest r = ZipArchiveEntryRequest.createZipArchiveEntryRequest(e, () -> new ByteArrayInputStream(part));
                            r.getMethod();
                            s.addArchiveEntry(r);
                        }
                        s.writeTo(out);
                    }
                }
            }
            try (ZipFile z = ZipFile.builder().setByteArray(b.toByteArray()).get()) {
                for (int i = 0; i < files; i++) {
                    ZipArchiveEntry e = z.getEntry("p" + i);
                    Stitch.check(e != null, "scatter entry present");
                    try (InputStream in = z.getInputStream(e)) { Stitch.check(Arrays.equals(Stubs.drain(in), Arrays.copyOf(content, content.length * (i + 1) / files)), "scatter entry contents"); }
                }
            }
            try (FileBasedScatterGatherBackingStoreHelper h = new FileBasedScatterGatherBackingStoreHelper(tmp.resolve("store"))) { h.run(content); }
            try (StreamCompressor sc = StreamCompressor.create(level, new org.apache.commons.compress.parallel.FileBasedScatterGatherBackingStore(tmp.resolve("sc")))) {
                sc.deflate(new ByteArrayInputStream(content), ZipArchiveEntry.DEFLATED);
                sc.writeCounted(content); sc.writeCounted(content, 0, content.length);
                sc.getBytesRead(); sc.getBytesWrittenForLastEntry(); sc.getCrc32(); sc.getTotalBytesWritten();
            }
        } finally {
            deleteTree(tmp);
        }
    }

    static class FileBasedScatterGatherBackingStoreHelper implements Closeable {
        final org.apache.commons.compress.parallel.FileBasedScatterGatherBackingStore s;
        FileBasedScatterGatherBackingStoreHelper(Path p) throws IOException { s = new org.apache.commons.compress.parallel.FileBasedScatterGatherBackingStore(p); }
        void run(byte[] data) throws IOException {
            s.writeOut(data, 0, data.length);
            s.closeForWriting();
            try (InputStream in = s.getInputStream()) { Stitch.check(Arrays.equals(Stubs.drain(in), data), "backing store returns what was written"); }
        }
        public void close() throws IOException { s.close(); }
    }

    // ---------------------------------------------------------------- other writers
    /** Oracle: ar/cpio/jar/tar output streams through the factory read back (createArchiveEntry from files). */
    @Endpoint(bailsOn = {IOException.class})
    public static void writer_from_files(@Bytes(max = 1024) byte[] content, @OneOf(strings = {"ar", "cpio", "jar", "tar", "zip"}) String format) throws Exception {
        Path sandbox = Files.createTempDirectory(tempRoot(), "w");
        try {
            Path f = sandbox.resolve("file.bin");
            Files.write(f, content);
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            try (ArchiveOutputStream<ArchiveEntry> out = ArchiveStreamFactory.DEFAULT.createArchiveOutputStream(format, b)) {
                ArchiveEntry e = out.createArchiveEntry(f, "file.bin");
                out.createArchiveEntry(f.toFile(), "file2.bin");
                out.canWriteEntryData(e);
                out.putArchiveEntry(e);
                out.write(content);
                out.closeArchiveEntry();
                out.getBytesWritten(); out.getCount();
                out.finish();
            }
            try (ArchiveInputStream<?> in = ArchiveStreamFactory.DEFAULT.createArchiveInputStream(format.equals("jar") ? "zip" : format, new ByteArrayInputStream(b.toByteArray()))) {
                ArchiveEntry e;
                boolean found = false;
                while ((e = in.getNextEntry()) != null) if (e.getName().equals("file.bin")) { found = Arrays.equals(Stubs.drain(in), content); }
                Stitch.check(found, "the file reads back");
            }
        } finally {
            deleteTree(sandbox);
        }
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void filter_output(@Str(max = 32) String s, @Bytes(max = 256) byte[] data, boolean gz) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        Path sandbox = Files.createTempDirectory(tempRoot(), "f");
        try {
            Path f = sandbox.resolve("x");
            Files.write(f, data);
            if (gz) {
                try (GzipCompressorOutputStream o = new GzipCompressorOutputStream(b)) { o.writeUtf8(s); o.writeUsAscii(s); o.writeUsAsciiRaw(s); o.write(f); o.write(f.toFile()); Stitch.check(!o.isClosed(), "open"); }
            } else {
                try (ArArchiveOutputStream o = new ArArchiveOutputStream(b)) {
                    o.setLongFileMode(ArArchiveOutputStream.LONGFILE_BSD);
                    ArArchiveEntry e = new ArArchiveEntry("x", data.length);
                    o.putArchiveEntry(e); o.write(f); o.closeArchiveEntry(); o.isClosed();
                }
            }
        } finally {
            deleteTree(sandbox);
        }
    }

    @Endpoint
    public static void misc_constructors(@Str(max = 16) String s, int v) {
        DumpArchiveConstants.COMPRESSION_TYPE.find(v & 3);
        DumpArchiveConstants.SEGMENT_TYPE.find(v);
        DumpArchiveEntry.TYPE.find(v & 15);
        DumpArchiveEntry.PERMISSION.find(v & 07777);
        new DumpArchiveEntry(s, s).getSimpleName();
        new ArjArchiveEntry().getName();
        new ArjArchiveEntry.HostOs();
        new XXHash32().update(1);
        new CharsetNames();
        new OsgiUtils();
        OsgiUtils.isRunningInOsgiEnvironment();
    }
}
