#include "../objects.hpp"

New<Deflate> deflate_init(zh::Level level, Range<8, 31> wbits, Bool raw, Range<1, 9> mem_level, zh::Strategy strategy) {
    auto* d = new Deflater();
    d->wbits = raw ? -(int)(wbits > 15 ? wbits - 16 : wbits) : (int)wbits;
    if (deflateInit2(&d->s, (int)level, Z_DEFLATED, d->wbits, (int)mem_level, (int)strategy) != Z_OK) {
        delete d;
        stitch::bail("deflateInit2 rejected parameters");
    }
    return d;
}
STITCH_ENDPOINT(deflate_init);

New<Deflate> deflate_init_default(zh::Level level) {
    auto* d = new Deflater();
    if (deflateInit(&d->s, (int)level) != Z_OK) {
        delete d;
        stitch::bail("deflateInit failed");
    }
    return d;
}
STITCH_ENDPOINT(deflate_init_default);

void deflate_write(Borrow<Deflate> d, Bytes<8192, Plain> data, zh::Flush flush, Range<1, 4096> chunk) {
    stitch::require(!d->finished || (int)flush == Z_FINISH, "stream finished: only Z_FINISH allowed");
    int rc = zh::run_deflate(*d, data.data(), data.size(), (int)flush, (size_t)chunk);
    stitch::require(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR, "deflate error");
}
STITCH_ENDPOINT(deflate_write);

void deflate_finish(Borrow<Deflate> d, Range<1, 4096> chunk) {
    int rc = zh::run_deflate(*d, nullptr, 0, Z_FINISH, (size_t)chunk);
    stitch::require(rc == Z_STREAM_END, "finish did not end the stream");
}
STITCH_ENDPOINT(deflate_finish);

void deflate_params(Borrow<Deflate> d, zh::Level level, zh::Strategy strategy, Range<1, 4096> chunk) {
    std::vector<uint8_t> buf((size_t)chunk);
    d->s.next_out = buf.data();
    d->s.avail_out = (uInt)chunk;
    int rc = deflateParams(&d->s, (int)level, (int)strategy);
    d->out.insert(d->out.end(), buf.data(), buf.data() + ((size_t)chunk - d->s.avail_out));
    d->s.next_out = nullptr;
    d->s.avail_out = 0;
    stitch::require(rc == Z_OK, "deflateParams failed");
}
STITCH_ENDPOINT(deflate_params);

void deflate_tune(Borrow<Deflate> d, Range<0, 300> good, Range<0, 300> lazy, Range<0, 300> nice, Range<0, 5000> chain) {
    stitch::require(deflateTune(&d->s, (int)good, (int)lazy, (int)nice, (int)chain) == Z_OK, "deflateTune failed");
}
STITCH_ENDPOINT(deflate_tune);

void deflate_set_dictionary(Borrow<Deflate> d, Bytes<1024, Plain> dict) {
    int rc = deflateSetDictionary(&d->s, dict.data(), (uInt)dict.size());
    stitch::require(rc == Z_OK, "deflateSetDictionary rejected");
    stitch::set(d, "has_dict", 1);
}
STITCH_ENDPOINT(deflate_set_dictionary);

void deflate_get_dictionary(Borrow<Deflate> d) {
    std::vector<uint8_t> buf(32768);
    uInt len = 0;
    stitch::require(deflateGetDictionary(&d->s, buf.data(), &len) == Z_OK, "deflateGetDictionary failed");
    stitch::check(len <= 32768, "dictionary fits the window");
}
STITCH_ENDPOINT(deflate_get_dictionary);

void deflate_prime(Borrow<Deflate> d, Range<0, 16> bits, I32 value) {
    stitch::require(deflatePrime(&d->s, (int)bits, (int)value) == Z_OK, "deflatePrime failed");
    stitch::set(d, "primed", 1);  // extra bits: output is no longer a plain stream of the input
}
STITCH_ENDPOINT(deflate_prime);

void deflate_set_header(Borrow<Deflate> d, Bool text, I64 time, Range<0, 255> os, Bytes<128> extra, Str<64> name,
                        Str<64> comment, Bool hcrc) {
    auto h = std::make_shared<GzHeader>();
    h->extra.assign(extra.data(), extra.data() + extra.size());
    h->name = name.s;
    h->comment = comment.s;
    h->head.text = text;
    h->head.time = (uLong)(int64_t)time;
    h->head.os = (int)os;
    h->head.extra = h->extra.data();
    h->head.extra_len = (uInt)h->extra.size();
    h->head.name = (Bytef*)h->name.c_str();
    h->head.comment = (Bytef*)h->comment.c_str();
    h->head.hcrc = hcrc;
    if (deflateSetHeader(&d->s, &h->head) == Z_OK) d->hdr = h;  // keep the previous one alive otherwise
    stitch::require(d->hdr == h, "deflateSetHeader rejected (not a gzip stream?)");
}
STITCH_ENDPOINT(deflate_set_header);

void deflate_pending(Borrow<Deflate> d) {
    unsigned pending = 0;
    int bits = 0;
    stitch::require(deflatePending(&d->s, &pending, &bits) == Z_OK, "deflatePending failed");
    stitch::check(bits >= 0 && bits < 8, "pending bits < 8");
}
STITCH_ENDPOINT(deflate_pending);

void deflate_bound(Borrow<Deflate> d, Range<0, 1 << 20> len) {
    uLong b = deflateBound(&d->s, (uLong)len);
    stitch::check(b >= (uLong)len, "deflateBound >= input length");
}
STITCH_ENDPOINT(deflate_bound);

void deflate_reset(Borrow<Deflate> d) {
    stitch::require(deflateReset(&d->s) == Z_OK, "deflateReset failed");
    d->in.clear();
    d->out.clear();
    d->finished = false;
    stitch::del(d, "has_dict");
    stitch::del(d, "primed");
}
STITCH_ENDPOINT(deflate_reset);

New<Deflate> deflate_copy(Borrow<Deflate> src) {
    auto* d = new Deflater(**src);
    memset(&d->s, 0, sizeof d->s);
    if (deflateCopy(&d->s, &src->s) != Z_OK) {
        delete d;
        stitch::bail("deflateCopy failed");
    }
    // the copy shares src's header storage (d->hdr), which deflateCopy's pointer refers to
    New<Deflate> r(d);
    for (const char* k : {"primed", "has_dict"})  // a copy has the same history
        if (stitch::has(src, k)) stitch::set(r, k, stitch::get(src, k, 0));
    return r;
}
STITCH_ENDPOINT(deflate_copy);

void deflate_end(Take<Deflate> d) {
    deflateEnd(&d->s);
    delete *d;
}
STITCH_ENDPOINT(deflate_end);
