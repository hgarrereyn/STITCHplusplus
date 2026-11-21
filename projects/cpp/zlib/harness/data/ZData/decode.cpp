// Compressed streams: deflate fuzzed plaintext with fuzzed parameters (zlib, gzip or raw), then
// optionally corrupt / truncate / concatenate. The decoder calls zlib itself.
#include "../../objects.hpp"
static std::string zdata(stitch::Source& s) {
    std::string plain;
    while (s.more() && plain.size() < 8192) plain += s.coin(0.5) ? s.str(48) : std::string((size_t)s.range(1, 200), (char)s.u8());
    int wrap = (int)s.choice(3);
    int wbits = (int)s.range(9, 15);
    int bits = wrap == 0 ? wbits : wrap == 1 ? 16 + wbits : -wbits;
    z_stream z{};
    if (deflateInit2(&z, (int)s.range(0, 9), Z_DEFLATED, bits, (int)s.range(1, 9), (int)s.choice(5)) != Z_OK) return plain;
    std::string out(deflateBound(&z, plain.size()) + 64, '\0');
    z.next_in = (Bytef*)plain.data();
    z.avail_in = (uInt)plain.size();
    z.next_out = (Bytef*)&out[0];
    z.avail_out = (uInt)out.size();
    int flush = s.coin(0.8) ? Z_FINISH : Z_SYNC_FLUSH;
    deflate(&z, flush);
    out.resize(out.size() - z.avail_out);
    deflateEnd(&z);
    switch (s.choice(6)) {
        case 0: if (!out.empty()) out[(size_t)s.choice(out.size())] ^= (char)(1 << s.choice(8)); break;  // bit flip
        case 1: out.resize((size_t)s.choice(out.size() + 1)); break;                                     // truncate
        case 2: out += out; break;                                                                        // concatenated members
        case 3: if (!out.empty()) out[(size_t)s.choice(out.size())] = (char)s.u8(); break;
        default: break;                                                                                   // intact
    }
    return out;
}
STITCH_DECODER(ZData, zdata);
