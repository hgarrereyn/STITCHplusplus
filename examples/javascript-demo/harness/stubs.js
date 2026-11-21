const stitch = require("stitch");
const { Borrow, New, Str } = stitch;
const kv = require("kvjs");

const Store = stitch.object("Store");
const Path = stitch.datatype("Path");

stitch.decoder(Path, (s) => {
  const parts = [];
  while (s.more()) parts.push(s.coin(0.4) ? s.token() : s.str(6));
  return parts.join(".");
});

stitch.endpoint("new_store", {}, New(Store), () => new kv.Store());

stitch.endpoint("put", { st: Borrow(Store), key: Str(16), value: Str(32) }, null, ({ st, key, value }) => {
  st.put(key, value);
}, { bailsOn: [RangeError] });

stitch.endpoint("remove", { st: Borrow(Store), key: Str(16) }, null, ({ st, key }) => st.remove(key));

stitch.endpoint("dump", { st: Borrow(Store) }, null, ({ st }) => st.dump());

stitch.endpoint("set_path", { st: Borrow(Store), path: Str(64, Path), value: Str(8) }, null, ({ st, path, value }) => {
  kv.setPath(st.data, path, value);
}, { bailsOn: [(e) => e.message === "forbidden key"] });
