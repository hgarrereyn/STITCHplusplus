#pragma once
#include <stitch.h>
extern "C" {
#include <msg.h>
}

STITCH_OBJECT(Msg, msg_t*);
STITCH_DROP(Msg, [](msg_t* m) { msg_free(m); });

STITCH_DATATYPE(KvText);   // "key=value;key=value" headers (decoder in harness/data/KvText/decode.cpp)
