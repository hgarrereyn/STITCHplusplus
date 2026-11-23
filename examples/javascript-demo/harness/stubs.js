const stitch = require("stitch");
const { Borrow, New, Str } = stitch;
const kv = require("kvjs");

const Store = stitch.object("Store");
const Path = stitch.datatype("Path");
const KvText = stitch.datatype("KvText");

stitch.decoder(KvText, (s) => {
  const lines = [];
  while (s.more()) lines.push(`${s.coin(0.3) ? s.token() : s.str(8)}=${s.str(12)}`);
  return lines.join("\n");
});

stitch.decoder(Path, (s) => {
  const parts = [];
  while (s.more()) parts.push(s.coin(0.4) ? s.token() : s.str(6));
  return parts.join(".");
});

stitch.endpoint("new_store", {}, New(Store), () => new kv.Store());

stitch.endpoint("load", { text: Str(256, KvText) }, New(Store), ({ text }) => kv.load(text), { bailsOn: [RangeError] });

stitch.endpoint("put", { st: Borrow(Store), key: Str(16), value: Str(32) }, null, ({ st, key, value }) => {
  st.put(key, value);
}, { bailsOn: [RangeError] });

stitch.endpoint("remove", { st: Borrow(Store), key: Str(16) }, null, ({ st, key }) => st.remove(key));

stitch.endpoint("roundtrip", { st: Borrow(Store) }, null, ({ st }) => {
  const again = kv.load(st.dump());
  const keys = (o) => JSON.stringify([...o].sort());
  stitch.check(keys(Object.keys(again.data)) === keys(st.order), "dump/load roundtrip");
});

stitch.endpoint("set_path", { st: Borrow(Store), path: Str(64, Path), value: Str(8) }, null, ({ st, path, value }) => {
  kv.setPath(st.data, path, value);
}, { bailsOn: [(e) => e.message === "forbidden key"] });

// A typed data type: Pairs decodes to an array of [key, value], drawing each with Pair's decoder.
const Pair = stitch.datatype("Pair");
const Pairs = stitch.datatype("Pairs");
stitch.decoder(Pair, (s) => [s.coin(0.3) ? s.token() : s.str(8), s.coin() ? s.str(12) : s.f64(0, 100).toFixed(2)]);
stitch.decoder(Pairs, (s) => {
  const out = [];
  while (s.more()) out.push(s.draw(Pair));
  return out;
});

stitch.endpoint("put_all", { st: Borrow(Store), kvs: stitch.Val(Pairs, 256) }, null, ({ st, kvs }) => {
  for (const [k, v] of kvs) st.put(k, v);
}, { bailsOn: [RangeError] });
