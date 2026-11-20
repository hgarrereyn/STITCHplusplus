// zlib harness: streams keep their own input/output history so stubs can check round trips.
#pragma once
#include <stitch.h>
#include <zlib.h>
#include <string.h>
#include <unistd.h>
#include <memory>
#include <vector>

// gzip header storage: zlib keeps a pointer to it, and deflateCopy copies that pointer, so it is
// shared by a stream and all its copies (and outlives each of them).
struct GzHeader {
    gz_header head{};
    std::string name, comment;
    std::vector<uint8_t> extra;
};
struct Deflater {
    z_stream s{};
    std::vector<uint8_t> in, out;  // everything fed / produced
    int wbits = 15;
    bool finished = false;
    std::shared_ptr<GzHeader> hdr;
};
struct Inflater {
    z_stream s{};
    std::vector<uint8_t> out;
    int wbits = 15;
    bool ended = false;  // Z_STREAM_END seen
    gz_header head{};
    std::vector<uint8_t> extra, name, comment;
};
STITCH_OBJECT(Deflate, Deflater*);
STITCH_DROP(Deflate, [](Deflater* d) { deflateEnd(&d->s); delete d; });
STITCH_OBJECT(Inflate, Inflater*);
STITCH_DROP(Inflate, [](Inflater* d) { inflateEnd(&d->s); delete d; });
STITCH_OBJECT(Gz, gzFile);
STITCH_DROP(Gz, [](gzFile f) { gzclose(f); });

STITCH_DATATYPE(Plain);  // plaintext with repetition (compressible)
STITCH_DATATYPE(ZData);  // compressed streams: zlib/gzip/raw deflate of fuzzed plaintext, sometimes corrupted

namespace zh {
using Flush = OneOf<Z_NO_FLUSH, Z_PARTIAL_FLUSH, Z_SYNC_FLUSH, Z_FULL_FLUSH, Z_FINISH, Z_BLOCK, Z_TREES>;
using Level = Range<-1, 9>;
using Strategy = OneOf<Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED>;

// Run deflate over `data` with output chunks of `chunk` bytes; appends to d->out. Returns last rc.
inline int run_deflate(Deflater* d, const uint8_t* data, size_t len, int flush, size_t chunk) {
    std::vector<uint8_t> buf(chunk);
    d->s.next_in = (Bytef*)data;
    d->s.avail_in = (uInt)len;
    int rc;
    int guard = 0;
    do {
        d->s.next_out = buf.data();
        d->s.avail_out = (uInt)chunk;
        rc = deflate(&d->s, flush);
        d->out.insert(d->out.end(), buf.data(), buf.data() + (chunk - d->s.avail_out));
        if (rc == Z_STREAM_END) d->finished = true;
        if (rc != Z_OK) break;
    } while ((d->s.avail_in > 0 || d->s.avail_out == 0) && ++guard < 100000);
    d->in.insert(d->in.end(), data, data + (len - d->s.avail_in));
    // never leave pointers to stub buffers in the stream
    d->s.next_in = nullptr;
    d->s.avail_in = 0;
    d->s.next_out = nullptr;
    d->s.avail_out = 0;
    return rc;
}

inline int run_inflate(Inflater* f, const uint8_t* data, size_t len, int flush, size_t chunk) {
    std::vector<uint8_t> buf(chunk);
    f->s.next_in = (Bytef*)data;
    f->s.avail_in = (uInt)len;
    int rc;
    int guard = 0;
    do {
        f->s.next_out = buf.data();
        f->s.avail_out = (uInt)chunk;
        rc = inflate(&f->s, flush);
        f->out.insert(f->out.end(), buf.data(), buf.data() + (chunk - f->s.avail_out));
        if (rc == Z_STREAM_END) f->ended = true;
        if (rc != Z_OK) break;
        if (f->out.size() > (64u << 20)) break;
    } while ((f->s.avail_in > 0 || f->s.avail_out == 0) && ++guard < 100000);
    f->s.next_in = nullptr;
    f->s.avail_in = 0;
    f->s.next_out = nullptr;
    f->s.avail_out = 0;
    return rc;
}
}  // namespace zh
