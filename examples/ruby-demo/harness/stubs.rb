require "stitch"
require "kvrb"
include Stitch::DSL

Store = Stitch.object("Store")
KvText = Stitch.datatype("KvText")

Stitch.decoder(KvText) do |s|
  lines = []
  while s.more
    key = s.coin(0.3) ? s.token : s.str(8)
    lines << "#{key}=#{s.str(12)}"
  end
  lines.join("\n")
end

Stitch.endpoint :new_store, {}, New[Store] do
  Kvrb::Store.new
end

Stitch.endpoint :load, { text: Str[256, KvText] }, New[Store], bails_on: [ArgumentError] do |text|
  Kvrb.load(text)
end

Stitch.endpoint :put, { st: Borrow[Store], key: Str[16], value: Str[32] }, bails_on: [ArgumentError] do |st, key, value|
  st.put(key, value)
end

Stitch.endpoint :remove, { st: Borrow[Store], key: Str[16] } do |st, key|
  st.remove(key)
end

Stitch.endpoint :roundtrip, { st: Borrow[Store] } do |st|
  again = Kvrb.load(st.dump)
  Stitch.check(again.items.to_h == st.items.to_h, "dump/load roundtrip")
end
