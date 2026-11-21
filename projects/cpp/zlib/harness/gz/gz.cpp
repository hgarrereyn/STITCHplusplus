// gzip file API. Files are opened on fuzzed content (read) or on a fresh temp file (write).
#include "../objects.hpp"
#include <fcntl.h>
#include <stdlib.h>

New<Gz> gz_open_read(File<8192, ZData> f, OneOf<0, 1, 2> how) {
    gzFile g;
    if ((int)how == 0) g = gzopen(f.path(), "rb");
    else {
        int fd = open(f.path(), O_RDONLY);
        stitch::require(fd >= 0, "open failed");
        g = gzdopen(fd, (int)how == 1 ? "rb" : "r");
        if (!g) close(fd);
    }
    stitch::require(g, "gzopen failed");
    stitch::set(g, "mode", "r");
    return g;
}
STITCH_ENDPOINT(gz_open_read);

New<Gz> gz_open_write(OneOf<'0', '1', '6', '9'> level, OneOf<'f', 'h', 'R', 'F', 'T', ' '> strategy) {
    char path[] = "/tmp/stitch-gz-XXXXXX";
    int fd = mkstemp(path);
    stitch::require(fd >= 0, "mkstemp failed");
    unlink(path);
    char mode[4] = {'w', (char)level, (char)strategy, 0};
    gzFile g = gzdopen(fd, mode);
    if (!g) close(fd);
    stitch::require(g, "gzdopen failed");
    stitch::set(g, "mode", "w");
    return g;
}
STITCH_ENDPOINT(gz_open_write);

static bool reading(const Borrow<Gz>& g) { return stitch::get_str(g, "mode") == "r"; }

void gz_buffer(Borrow<Gz> g, Range<0, 1 << 16> size) { gzbuffer(g, (unsigned)size); }
STITCH_ENDPOINT(gz_buffer);

void gz_read(Borrow<Gz> g, Range<0, 1 << 16> len, Bool fread) {
    stitch::require(reading(g), "not opened for reading");
    std::vector<uint8_t> buf((size_t)len + 1);
    if (fread) {
        size_t n = gzfread(buf.data(), 1, (size_t)len, g);
        stitch::check(n <= (size_t)len, "gzfread within len");
    } else {
        int n = gzread(g, buf.data(), (unsigned)len);
        stitch::check(n <= (int)len, "gzread within len");
    }
}
STITCH_ENDPOINT(gz_read);

void gz_gets(Borrow<Gz> g, Range<0, 512> len) {
    stitch::require(reading(g), "not opened for reading");
    std::vector<char> buf((size_t)len + 1, 'x');
    char* r = gzgets(g, buf.data(), (int)len);
    if (r) stitch::check(strlen(r) < (size_t)std::max<long long>(len, 1), "gzgets result is terminated within len");
}
STITCH_ENDPOINT(gz_gets);

void gz_getc_ungetc(Borrow<Gz> g, Range<-1, 255> c) {
    stitch::require(reading(g), "not opened for reading");
    gzgetc(g);
    gzungetc((int)c, g);
    gzgetc(g);
}
STITCH_ENDPOINT(gz_getc_ungetc);

void gz_write(Borrow<Gz> g, Bytes<8192, Plain> data, Bool fwrite) {
    stitch::require(!reading(g), "not opened for writing");
    if (fwrite) gzfwrite(data.data(), 1, data.size(), g);
    else gzwrite(g, data.data(), (unsigned)data.size());
}
STITCH_ENDPOINT(gz_write);

void gz_puts(Borrow<Gz> g, Str<512> s, Range<-1, 255> c) {
    stitch::require(!reading(g), "not opened for writing");
    gzputs(g, s.c_str());
    gzputc(g, (int)c);
    gzprintf(g, "%s:%d", s.c_str(), (int)c);
}
STITCH_ENDPOINT(gz_puts);

void gz_set_params(Borrow<Gz> g, zh::Level level, zh::Strategy strategy) {
    stitch::require(!reading(g), "not opened for writing");
    gzsetparams(g, (int)level, (int)strategy);
}
STITCH_ENDPOINT(gz_set_params);

void gz_flush(Borrow<Gz> g, OneOf<Z_NO_FLUSH, Z_PARTIAL_FLUSH, Z_SYNC_FLUSH, Z_FULL_FLUSH, Z_FINISH> flush) {
    stitch::require(!reading(g), "not opened for writing");
    gzflush(g, (int)flush);
}
STITCH_ENDPOINT(gz_flush);

// Seeking forward in a write handle writes zeros: keep offsets small (INT_MAX = 2 GB of output).
void gz_seek(Borrow<Gz> g, Range<-4096, 1 << 20> offset, OneOf<SEEK_SET, SEEK_CUR> whence) {
    gzseek(g, (z_off_t)(int)offset, (int)whence);
    (void)gztell(g);
    (void)gzoffset(g);
}
STITCH_ENDPOINT(gz_seek);

void gz_status(Borrow<Gz> g, Bool clear) {
    int err = 0;
    const char* msg = gzerror(g, &err);
    if (msg) (void)strlen(msg);
    (void)gzeof(g);
    (void)gzdirect(g);
    if (clear) gzclearerr(g);
}
STITCH_ENDPOINT(gz_status);

void gz_rewind(Borrow<Gz> g) {
    stitch::require(reading(g), "not opened for reading");
    gzrewind(g);
}
STITCH_ENDPOINT(gz_rewind);

void gz_close(Take<Gz> g, OneOf<0, 1, 2> how) {
    bool r = stitch::get_str(g, "mode") == "r";
    if ((int)how == 1) r ? gzclose_r(*g) : gzclose_w(*g);
    else gzclose(*g);
}
STITCH_ENDPOINT(gz_close);
