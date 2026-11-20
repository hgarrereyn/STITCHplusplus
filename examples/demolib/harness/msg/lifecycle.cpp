#include "../objects.hpp"

New<Msg> create(Range<0, 4096> cap) {
    msg_t* m = msg_new((size_t)cap);
    stitch::require(m, "msg_new rejected capacity");
    return m;
}
STITCH_ENDPOINT(create);

namespace stub {
New<Msg> clone(Borrow<Msg> m) {
    msg_t* c = msg_clone(m);
    stitch::require(c, "clone failed");
    stitch::check(msg_len(c) == msg_len(m), "clone keeps length");
    return c;
}
}  // namespace stub
STITCH_ENDPOINT(stub::clone);

void destroy(Take<Msg> m) { msg_free(m); }
STITCH_ENDPOINT(destroy);
