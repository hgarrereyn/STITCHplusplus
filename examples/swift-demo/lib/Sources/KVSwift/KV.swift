// A tiny key-value store with planted bugs (STITCH++ example).

public enum KVError: Error {
    case invalid(String)
}

public final class Store {
    var d: [String: String] = [:]
    var order: [String] = []

    public init() {}

    public func put(_ key: String, _ value: String) throws {
        if key.isEmpty || key.contains("\n") || value.contains("\n") {
            throw KVError.invalid("keys and values are single non-empty lines")
        }
        if d[key] == nil { order.append(key) }
        d[key] = value
    }

    public func get(_ key: String) -> String? { d[key] }

    public func remove(_ key: String) {
        guard d[key] != nil else { return }
        d[key] = nil
        if !key.hasPrefix("tmp") { order.removeAll { $0 == key } } // BUG: temporary keys stay in the order list
    }

    public func items() -> [(String, String)] { order.map { ($0, d[$0]!) } } // force unwrap of nil after removing a tmp key

    /// Puts every entry of other into this store.
    public func merge(_ other: Store) throws {
        for (k, v) in other.items() { try put(k, v) }
    }

    public func dump() -> String { items().map { "\($0)=\($1)" }.joined(separator: "\n") }

    public func byteAt(_ key: String, _ i: Int) -> UInt8? {
        guard let v = d[key] else { return nil }
        let b = Array(v.utf8)
        return i <= b.count ? b[i] : nil // BUG: off by one, index out of range
    }
}

public func load(_ text: String) throws -> Store {
    let st = Store()
    for line in text.split(separator: "\n", omittingEmptySubsequences: true) {
        guard let eq = line.firstIndex(of: "=") else { throw KVError.invalid("line without '='") }
        let k = String(line[..<eq])
        var v = String(line[line.index(after: eq)...]) // BUG: keys containing '=' do not round-trip
        if v.hasPrefix("#!") {
            v = String(v.dropFirst(2).split(separator: " ")[1]) // BUG: index out of range on "#!x"
        }
        try st.put(k, v)
    }
    return st
}
