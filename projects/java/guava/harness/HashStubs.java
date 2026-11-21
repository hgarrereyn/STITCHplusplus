import com.google.common.hash.*;
import java.nio.charset.StandardCharsets;
import stitch.*;

/** Guava hashing: one stub per hash function; streaming and one-shot hashing agree; known invariants. */
public class HashStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }


    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_murmur3_32_fixed(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.murmur3_32_fixed();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_murmur3_128(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.murmur3_128();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_siphash24(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.sipHash24();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_md5(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.md5();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_sha1(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.sha1();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_sha256(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.sha256();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_sha384(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.sha384();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_sha512(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.sha512();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_crc32(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.crc32();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_crc32c(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.crc32c();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_adler32(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.adler32();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_farmhashfingerprint64(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.farmHashFingerprint64();
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @SuppressWarnings("deprecation")
    @Endpoint
    public static void hash_goodfasthash32(@Bytes(max = 256) byte[] data, @stitch.Max(16) Source s) {
        HashFunction fn = Hashing.goodFastHash(32);
        HashCode one = fn.hashBytes(data);
        Hasher h = fn.newHasher();
        int i = 0;
        while (i < data.length) {
            int n = Math.min(data.length - i, ri(s, 1, 17));
            switch (s.choice(4)) {
                case 0: h.putBytes(data, i, n); break;
                case 1: h.putBytes(java.nio.ByteBuffer.wrap(data, i, n)); break;
                case 2: for (int j = 0; j < n; j++) h.putByte(data[i + j]); break;
                default: if (n >= 8) { h.putLong(java.nio.ByteBuffer.wrap(data, i, 8).order(java.nio.ByteOrder.LITTLE_ENDIAN).getLong()); n = 8; } else h.putBytes(data, i, n);
            }
            i += n;
        }
        HashCode streamed = h.hash();
        Stitch.check(one.equals(streamed), "streamed hashing == hashBytes");
        Stitch.check(one.bits() == fn.bits(), "bits()");
        Stitch.check(HashCode.fromString(one.toString()).equals(one), "HashCode.fromString(toString())");
        Stitch.check(HashCode.fromBytes(one.asBytes()).equals(one), "fromBytes(asBytes())");
        String text = new String(data, StandardCharsets.ISO_8859_1);
        Stitch.check(fn.hashString(text, StandardCharsets.UTF_8).equals(fn.hashBytes(text.getBytes(StandardCharsets.UTF_8))), "hashString(UTF-8) == hashBytes(UTF-8 bytes)");
        Stitch.check(fn.hashUnencodedChars(text).equals(fn.newHasher().putUnencodedChars(text).hash()), "hashUnencodedChars");
        fn.hashInt(data.length);
        fn.hashLong(data.length);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void consistent_hash(@stitch.Range(lo = 0, hi = 1000000) int input, @stitch.Range(lo = 1, hi = 1000) int buckets) {
        int b = Hashing.consistentHash(input, buckets);
        Stitch.check(b >= 0 && b < buckets, "consistentHash in [0, buckets)");
        int b2 = Hashing.consistentHash(input, buckets + 1);
        Stitch.check(b2 == b || b2 == buckets, "growing by one bucket moves a key only to the new bucket");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void combine_hash_codes(@Bytes(max = 64) byte[] a, @Bytes(max = 64) byte[] b) {
        HashFunction f = Hashing.sha256();
        HashCode x = f.hashBytes(a), y = f.hashBytes(b);
        Stitch.check(Hashing.combineUnordered(java.util.Arrays.asList(x, y)).equals(Hashing.combineUnordered(java.util.Arrays.asList(y, x))), "combineUnordered is symmetric");
        Hashing.combineOrdered(java.util.Arrays.asList(x, y));
        Hashing.concatenating(Hashing.crc32(), Hashing.adler32()).hashBytes(a);
        Stitch.check(Hashing.hmacSha256(a.length == 0 ? new byte[] {1} : a).hashBytes(b).bits() == 256, "hmacSha256");
        Hashing.hmacMd5(new byte[] {1, 2}).hashBytes(b);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void bloom_filter(@stitch.Max(64) Source s, @stitch.Range(lo = 1, hi = 1000) int expected) throws java.io.IOException {
        double fpp = s.pick(0.01, 0.1, 0.5, 1e-6);
        BloomFilter<Integer> bf = BloomFilter.create(Funnels.integerFunnel(), expected, fpp);
        java.util.List<Integer> added = new java.util.ArrayList<>();
        while (s.more()) { int v = ri(s, -1000, 1000); bf.put(v); added.add(v); }
        for (int v : added) Stitch.check(bf.mightContain(v), "no false negatives");
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        bf.writeTo(out);
        BloomFilter<Integer> back = BloomFilter.readFrom(new java.io.ByteArrayInputStream(out.toByteArray()), Funnels.integerFunnel());
        Stitch.check(back.equals(bf), "writeTo / readFrom round trip");
        BloomFilter<Integer> other = BloomFilter.create(Funnels.integerFunnel(), expected, fpp);
        other.put(12345);
        if (bf.isCompatible(other)) { bf.putAll(other); Stitch.check(bf.mightContain(12345), "putAll"); }
        Stitch.check(bf.approximateElementCount() >= 0 && bf.expectedFpp() >= 0, "approximate count and fpp");
    }
}
