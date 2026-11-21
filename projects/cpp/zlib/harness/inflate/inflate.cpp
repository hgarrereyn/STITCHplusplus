#include "../objects.hpp"

New<Inflate> inflate_init(Range<-15, 47> wbits) {
    auto* f = new Inflater();
    f->wbits = (int)wbits;
    if (inflateInit2(&f->s, f->wbits) != Z_OK) {
        delete f;
        stitch::bail("inflateInit2 rejected windowBits");
    }
    return f;
}
STITCH_ENDPOINT(inflate_init);

void inflate_feed(Borrow<Inflate> f, Bytes<8192, ZData> data, OneOf<Z_NO_FLUSH, Z_SYNC_FLUSH, Z_FINISH, Z_BLOCK, Z_TREES> flush,
                  Range<1, 4096> chunk) {
    int rc = zh::run_inflate(*f, data.data(), data.size(), (int)flush, (size_t)chunk);
    if (rc == Z_NEED_DICT) stitch::set(f, "needs_dict", 1);
    stitch::require(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR || rc == Z_NEED_DICT, "inflate error");
}
STITCH_ENDPOINT(inflate_feed);

// Inflate what a deflate stream produced; if that stream was finished and this inflater saw
// nothing else, the output must equal the deflater's input.
void inflate_from(Borrow<Inflate> f, Borrow<Deflate> d, Range<1, 4096> chunk) {
    bool fresh = f->s.total_in == 0 && f->out.empty() && stitch::get(f, "primed", 0) == 0 && !stitch::has(f, "dict_set");
    int rc = zh::run_inflate(*f, d->out.data(), d->out.size(), Z_NO_FLUSH, (size_t)chunk);
    if (rc == Z_NEED_DICT) stitch::set(f, "needs_dict", 1);
    // inflate must expect the same wrapper and at least the deflater's window (8 is widened to 9
    // by deflateInit2, so skip it)
    int dw = d->wbits, fw = f->wbits;
    bool same_format = dw != 8 && dw != 24 && dw != -8 &&
                       (fw == dw || (fw >= 40 && dw > 0 && (fw - 32) >= (dw & 15) && (dw & 15) >= 9));
    if (fresh && d->finished && same_format && !stitch::has(d, "has_dict") && d->s.total_in == d->in.size() &&
        stitch::get(d, "primed", 0) == 0) {
        stitch::check(rc == Z_STREAM_END, "deflate output inflates to stream end");
        stitch::check(f->out == d->in, "inflate(deflate(x)) == x");
    }
    stitch::require(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR, "inflate error");
}
STITCH_ENDPOINT(inflate_from);

void inflate_set_dictionary(Borrow<Inflate> f, Bytes<1024, Plain> dict) {
    int rc = inflateSetDictionary(&f->s, dict.data(), (uInt)dict.size());
    stitch::require(rc == Z_OK, "inflateSetDictionary rejected");
    stitch::set(f, "dict_set", 1);
}
STITCH_ENDPOINT(inflate_set_dictionary);

void inflate_get_dictionary(Borrow<Inflate> f) {
    std::vector<uint8_t> buf(32768);
    uInt len = 0;
    stitch::require(inflateGetDictionary(&f->s, buf.data(), &len) == Z_OK, "inflateGetDictionary failed");
    stitch::check(len <= 32768, "dictionary fits the window");
}
STITCH_ENDPOINT(inflate_get_dictionary);

void inflate_get_header(Borrow<Inflate> f, Range<0, 256> extra_max, Range<0, 256> name_max, Range<0, 256> comm_max) {
    f->extra.assign((size_t)extra_max, 0);
    f->name.assign((size_t)name_max, 0);
    f->comment.assign((size_t)comm_max, 0);
    memset(&f->head, 0, sizeof f->head);
    f->head.extra = extra_max ? f->extra.data() : nullptr;
    f->head.extra_max = (uInt)extra_max;
    f->head.name = name_max ? f->name.data() : nullptr;
    f->head.name_max = (uInt)name_max;
    f->head.comment = comm_max ? f->comment.data() : nullptr;
    f->head.comm_max = (uInt)comm_max;
    stitch::require(inflateGetHeader(&f->s, &f->head) == Z_OK, "inflateGetHeader rejected (not a gzip stream?)");
}
STITCH_ENDPOINT(inflate_get_header);

void inflate_sync(Borrow<Inflate> f, Bytes<4096, ZData> data) {
    f->s.next_in = (Bytef*)data.data();
    f->s.avail_in = (uInt)data.size();
    int rc = inflateSync(&f->s);
    f->s.next_in = nullptr;
    f->s.avail_in = 0;
    (void)inflateSyncPoint(&f->s);
    stitch::require(rc == Z_OK, "no sync point");
}
STITCH_ENDPOINT(inflate_sync);

// Documented: "should only be used with raw inflate, and before the first inflate() call after
// inflateInit2() or inflateReset()".
void inflate_prime(Borrow<Inflate> f, Range<-1, 16> bits, I32 value) {
    stitch::require(f->wbits < 0, "inflatePrime is for raw inflate");
    stitch::require(f->s.total_in == 0 && f->out.empty() && stitch::get(f, "primed", 0) == 0, "inflatePrime only before the first inflate()");
    stitch::require(inflatePrime(&f->s, (int)bits, (int)value) == Z_OK, "inflatePrime failed");
    stitch::set(f, "primed", 1);  // injected bits: output no longer mirrors a deflate stream
}
STITCH_ENDPOINT(inflate_prime);

void inflate_inspect(Borrow<Inflate> f) {
    long mark = inflateMark(&f->s);
    (void)inflateCodesUsed(&f->s);
}
STITCH_ENDPOINT(inflate_inspect);

void inflate_validate(Borrow<Inflate> f, Bool check) { inflateValidate(&f->s, check); }
STITCH_ENDPOINT(inflate_validate);

void inflate_reset(Borrow<Inflate> f, Bool keep, Range<-15, 47> wbits, Bool change_window) {
    int rc = keep ? inflateResetKeep(&f->s) : change_window ? inflateReset2(&f->s, (int)wbits) : inflateReset(&f->s);
    stitch::require(rc == Z_OK, "inflateReset failed");
    if (change_window && !keep) f->wbits = (int)wbits;
    f->out.clear();
    f->ended = false;
    stitch::del(f, "needs_dict");
    stitch::del(f, "primed");
    stitch::del(f, "dict_set");
}
STITCH_ENDPOINT(inflate_reset);

New<Inflate> inflate_copy(Borrow<Inflate> src) {
    auto* f = new Inflater(**src);
    memset(&f->s, 0, sizeof f->s);
    if (inflateCopy(&f->s, &src->s) != Z_OK) {
        delete f;
        stitch::bail("inflateCopy failed");
    }
    if (src->head.extra_max || src->head.name_max || src->head.comm_max) {
        f->head.extra = f->extra.empty() ? nullptr : f->extra.data();
        f->head.name = f->name.empty() ? nullptr : f->name.data();
        f->head.comment = f->comment.empty() ? nullptr : f->comment.data();
        inflateGetHeader(&f->s, &f->head);  // inflateCopy copied the pointer to src's header
    }
    New<Inflate> r(f);
    for (const char* k : {"primed", "dict_set", "needs_dict"})  // a copy has the same history
        if (stitch::has(src, k)) stitch::set(r, k, stitch::get(src, k, 0));
    return r;
}
STITCH_ENDPOINT(inflate_copy);

void inflate_end(Take<Inflate> f) {
    inflateEnd(&f->s);
    delete *f;
}
STITCH_ENDPOINT(inflate_end);
