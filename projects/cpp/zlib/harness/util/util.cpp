#include "../objects.hpp"

void compress_round_trip(Bytes<16384, Plain> data, zh::Level level) {
    uLong bound = compressBound(data.size());
    std::vector<uint8_t> comp(bound);
    uLongf clen = bound;
    stitch::require(compress2(comp.data(), &clen, data.data(), data.size(), (int)level) == Z_OK, "compress2 failed");
    stitch::check(clen <= bound, "compressBound holds");
    std::vector<uint8_t> back(data.size() + 1);
    uLongf blen = back.size();
    stitch::check(uncompress(back.data(), &blen, comp.data(), clen) == Z_OK, "compressed data decompresses");
    stitch::check(blen == data.size() && memcmp(back.data(), data.data(), blen) == 0, "uncompress(compress(x)) == x");
}
STITCH_ENDPOINT(compress_round_trip);

void uncompress_data(Bytes<8192, ZData> data, Range<0, 1 << 17> dest_len, Bool v2) {
    std::vector<uint8_t> out((size_t)dest_len + 1);
    uLongf dlen = (uLongf)dest_len;
    uLong slen = data.size();
    int rc = v2 ? uncompress2(out.data(), &dlen, data.data(), &slen) : uncompress(out.data(), &dlen, data.data(), slen);
    stitch::check(dlen <= (uLongf)dest_len, "output length within the buffer");
    stitch::require(rc == Z_OK, "uncompress failed");
}
STITCH_ENDPOINT(uncompress_data);

void checksums(Bytes<4096> a, Bytes<4096> b) {
    uLong ca = crc32(0, a.data(), (uInt)a.size()), cb = crc32(0, b.data(), (uInt)b.size());
    std::vector<uint8_t> ab(a.buf);
    ab.insert(ab.end(), b.buf.begin(), b.buf.end());
    uLong cab = crc32_z(0, ab.data(), ab.size());
    stitch::check(crc32_combine(ca, cb, (z_off_t)b.size()) == cab, "crc32_combine");
    stitch::check(crc32_combine_op(ca, cb, crc32_combine_gen((z_off_t)b.size())) == cab, "crc32_combine_op");
    uLong aa = adler32(1, a.data(), (uInt)a.size()), ab2 = adler32(1, b.data(), (uInt)b.size());
    uLong aab = adler32_z(1, ab.data(), ab.size());
    stitch::check(adler32_combine(aa, ab2, (z_off_t)b.size()) == aab, "adler32_combine");
}
STITCH_ENDPOINT(checksums);
