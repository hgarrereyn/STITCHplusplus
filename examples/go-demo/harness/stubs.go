package harness

import (
	"fmt"

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
	// a typed data type: Pairs decodes to a []pair, drawing each with Pair's decoder
	stitch.Decoder("Pair", func(s *stitch.Source) pair {
		k := s.Str(8)
		if s.Coin(0.3) {
			k = s.Token()
		}
		if s.Coin(0.5) {
			return pair{k, s.Str(12)}
		}
		return pair{k, fmt.Sprintf("%.2f", s.F64Range(0, 100))}
	})
	stitch.Decoder("Pairs", func(s *stitch.Source) []pair {
		var out []pair
		for s.More() {
			out = append(out, stitch.Draw[pair](s, "Pair"))
		}
		return out
	})
	stitch.Endpoint("put_all", putAll, "st", "kvs:val=Pairs,max=256")
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

type pair struct{ K, V string }

func putAll(st *kvgo.Store, kvs []pair) error {
	for _, kv := range kvs {
		if err := st.Put(kv.K, kv.V); err != nil {
			return err
		}
	}
	return nil
}
