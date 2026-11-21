import kvlib
import stitch
from stitch import Borrow, New, Str, Val

Store = stitch.object("Store")
KvText = stitch.datatype("KvText")


@stitch.decoder(KvText)
def kv_text(s):
    lines = []
    while s.more():
        key = s.token() if s.coin(0.3) else s.str(8)
        lines.append(f"{key}={s.str(12)}")
    return "\n".join(lines)


@stitch.endpoint
def new_store() -> New[Store]:
    return kvlib.Store()


@stitch.endpoint(bails_on=(ValueError,))
def load(text: Str[256, KvText]) -> New[Store]:
    return kvlib.load(text)


@stitch.endpoint(bails_on=(ValueError,))
def put(st: Borrow[Store], key: Str[16], value: Str[32]):
    st.put(key, value)


@stitch.endpoint
def remove(st: Borrow[Store], key: Str[16]):
    st.remove(key)


@stitch.endpoint
def roundtrip(st: Borrow[Store]):
    again = kvlib.load(st.dump())
    stitch.check(dict(again.items()) == dict(st.items()), "dump/load roundtrip")


# A typed data type: Pairs decodes to a list of (key, value) tuples, drawing each with Pair's decoder.
Pair = stitch.datatype("Pair")
Pairs = stitch.datatype("Pairs")


@stitch.decoder(Pair)
def pair(s):
    key = s.token() if s.coin(0.3) else s.str(8)
    return key, (s.str(12) if s.coin() else f"{s.f64(0, 100):.2f}")


@stitch.decoder(Pairs)
def pairs(s):
    out = []
    while s.more():
        out.append(s.draw(Pair))
    return out


@stitch.endpoint(bails_on=(ValueError,))
def put_all(st: Borrow[Store], kvs: Val[Pairs, 256]):
    for k, v in kvs:
        st.put(k, v)
