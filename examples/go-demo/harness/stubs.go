package harness

import (
	"example.com/kvgo"
	"stitch"
)

func init() {
	stitch.Object[*kvgo.Store]("Store", nil)
	stitch.Decoder("KvText", kvText)
	stitch.Endpoint("new_store", kvgo.New)
	stitch.Endpoint("load", kvgo.Load, "text:max=256,type=KvText")
	stitch.Endpoint("put", put, "st", "key:max=16", "value:max=32")
	stitch.Endpoint("remove", remove, "st", "key:max=16")
	stitch.Endpoint("roundtrip", roundtrip, "st")
}

func kvText(s *stitch.Source) string {
	out := ""
	for s.More() {
		k := s.Str(8)
		if s.Coin(0.3) {
			k = s.Token()
		}
		out += k + "=" + s.Str(12) + "\n"
	}
	return out
}

func put(st *kvgo.Store, key, value string) error { return st.Put(key, value) }

func remove(st *kvgo.Store, key string) { st.Remove(key) }

func roundtrip(st *kvgo.Store) {
	again, err := kvgo.Load(st.Dump())
	stitch.Require(err == nil, "dump did not load")
	stitch.Check(again.Len() == st.Len(), "dump/load roundtrip")
}
