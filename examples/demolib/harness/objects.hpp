#pragma once
#include <stitch.h>
extern "C" {
#include <msg.h>
}

STITCH_OBJECT(Msg, msg_t*);
STITCH_DROP(Msg, [](msg_t* m) { msg_free(m); });

STITCH_DATATYPE(KvText);   // "key=value;key=value" headers (decoder in harness/data/KvText/decode.cpp)

// A typed data type: Chunks decodes to a list of byte strings, drawing each with Chunk's decoder.
STITCH_DATATYPE(Chunk);
STITCH_DECODER(Chunk, [](stitch::Source& s) { return s.coin(0.2) ? s.fixed(8) : s.bytes(64); });
STITCH_DATATYPE(Chunks);
STITCH_DECODER(Chunks, [](stitch::Source& s) {
    std::vector<std::vector<uint8_t>> v;
    while (s.more()) v.push_back(s.draw<Chunk>());
    return v;
});
