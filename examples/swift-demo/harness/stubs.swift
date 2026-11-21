import KVSwift
import Stitch

enum Store: StitchObject { typealias Handle = KVSwift.Store }

enum KvText: Decoded {
    static func decode(_ s: Source) -> String {
        var lines: [String] = []
        while s.more() {
            let key = s.coin(0.3) ? s.token() : s.str(8)
            lines.append("\(key)=\(s.str(12))")
        }
        return lines.joined(separator: "\n")
    }
}

let newStore = endpoint("new_store") { () -> New<Store> in
    New(KVSwift.Store())
}

let load = endpoint("load") { (text: Str<256, KvText>) throws -> New<Store> in
    New(try KVSwift.load(text.value))
}

let put = endpoint("put") { (st: Borrow<Store>, key: Str<16, Plain>, value: Str<32, Plain>) throws in
    try st.value.put(key.value, value.value)
}

let remove = endpoint("remove") { (st: Borrow<Store>, key: Str<16, Plain>) in
    st.value.remove(key.value)
}

let byteAt = endpoint("byte_at") { (st: Borrow<Store>, key: Str<16, Plain>, i: Between<0, 64>) in
    _ = st.value.byteAt(key.value, i.value)
}

let roundtrip = endpoint("roundtrip") { (st: Borrow<Store>) throws in
    let again = try KVSwift.load(st.value.dump())
    try check(Dictionary(again.items(), uniquingKeysWith: { a, _ in a }) == Dictionary(st.value.items(), uniquingKeysWith: { a, _ in a }),
              "dump/load roundtrip")
}

// A typed data type: Pairs decodes to [(key, value)], drawing each with Pair's decoder.
enum Pair: Decoded {
    static func decode(_ s: Source) -> (String, String) {
        let key = s.coin(0.3) ? s.token() : s.str(8)
        return (key, s.coin() ? s.str(12) : String(Int(s.f64(0, 100) * 100) / 100))
    }
}

enum Pairs: Decoded {
    static func decode(_ s: Source) -> [(String, String)] {
        var out: [(String, String)] = []
        while s.more() { out.append(s.draw(Pair.self)) }
        return out
    }
}

let putAll = endpoint("put_all") { (st: Borrow<Store>, kvs: Val<Pairs, 256>) throws in
    for (k, v) in kvs.value { try st.value.put(k, v) }
}
