// swift-collections 1.1: Deque, OrderedSet, OrderedDictionary, Heap, BitSet, BitArray, TreeSet,
// TreeDictionary. Every collection is paired with a standard-library model; each stub applies one
// operation to both and checks the collection against the model (contents, count, order,
// equality, hashing, index invariants). Traps (index out of range, precondition, overflow) inside
// the library are bugs; operations are only called within their documented preconditions.
import BitCollections
import DequeModule
import HashTreeCollections
import HeapModule
import OrderedCollections
import Stitch

func v(_ s: Source) -> Int { s.coin(0.1) ? s.pick([Int.min, Int.max, 0, -1]) : s.range(-20, 20) }
func w(_ s: Source) -> String { s.pick(["a", "b", "c", "", "é", "日本", "aa", "b\u{301}", "\u{1F600}"]) + (s.coin(0.2) ? String(s.range(0, 9)) : "") }

final class DQ { var d = Deque<Int>(); var m: [Int] = []
    func check() throws {
        try Stitch.check(Array(d) == m, "Deque matches the model")
        try Stitch.check(d.count == m.count && d.isEmpty == m.isEmpty && d.first == m.first && d.last == m.last, "count / first / last")
        try Stitch.check(d == Deque(m) && d.hashValue == Deque(m).hashValue, "equality and hashing follow the contents")
        try Stitch.check(d.startIndex == 0 && d.endIndex == m.count, "indices")
        try Stitch.check(Array(d.reversed()) == m.reversed(), "reversed")
    }
}
final class OS { var s = OrderedSet<Int>(); var m: [Int] = []
    func check() throws {
        try Stitch.check(Array(s) == m, "OrderedSet matches the model")
        try Stitch.check(Set(m).count == m.count, "the model has no duplicates")
        for (i, x) in m.enumerated() { try Stitch.check(s.firstIndex(of: x) == i && s.contains(x), "firstIndex(of:) / contains") }
        try Stitch.check(s.unordered == OrderedSet(m.reversed()).unordered && s.count == m.count, "unordered view ignores order")
        try Stitch.check(s == OrderedSet(m) && s.hashValue == OrderedSet(m).hashValue, "equality and hashing")
    }
    func add(_ x: Int) -> Bool { if m.contains(x) { return false }; m.append(x); return true }
}
final class OSS { var s = OrderedSet<String>(); var m: [String] = []
    func check() throws {
        try Stitch.check(Array(s) == m, "OrderedSet<String> matches the model")
        for (i, x) in m.enumerated() { try Stitch.check(s.firstIndex(of: x) == i, "firstIndex(of:)") }
        try Stitch.check(s == OrderedSet(m) && s.hashValue == OrderedSet(m).hashValue, "equality and hashing")
    }
}
final class OD { var d = OrderedDictionary<Int, Int>(); var k: [Int] = []; var vals: [Int: Int] = [:]
    func check() throws {
        try Stitch.check(Array(d.keys) == k, "keys in insertion order")
        for key in k { try Stitch.check(d[key] == vals[key], "values match the model") }
        try Stitch.check(d.count == k.count && d.values.count == k.count, "count")
        for (i, key) in k.enumerated() { try Stitch.check(d.index(forKey: key) == i, "index(forKey:)"); try Stitch.check(d.elements[i].key == key, "elements view") }
        try Stitch.check(d == OrderedDictionary(uniqueKeys: k, values: k.map { vals[$0]! }), "equality")
    }
    func set(_ key: Int, _ val: Int?) { if let val = val { if vals[key] == nil { k.append(key) }; vals[key] = val } else if vals[key] != nil { k.removeAll { $0 == key }; vals[key] = nil } }
}
final class HP { var h = Heap<Int>(); var m: [Int] = []
    func check() throws {
        try Stitch.check(h.count == m.count && h.isEmpty == m.isEmpty, "count")
        try Stitch.check(h.min == m.min() && h.max == m.max(), "min / max match the model")
        try Stitch.check(h.unordered.sorted() == m.sorted(), "the heap holds the model's elements")
    }
}
final class BS { var b = BitSet(); var m = Set<Int>()
    func check() throws {
        try Stitch.check(Set(b) == m && b.count == m.count, "BitSet matches the model")
        try Stitch.check(Array(b) == m.sorted(), "BitSet iterates in increasing order")
        try Stitch.check(b.min() == m.min() && b.max() == m.max(), "min / max")
        try Stitch.check(b == BitSet(m) && b.hashValue == BitSet(m).hashValue, "equality and hashing")
        for x in m { try Stitch.check(b.contains(x), "contains") }
    }
}
final class BA { var b = BitArray(); var m: [Bool] = []
    func check() throws {
        try Stitch.check(Array(b) == m && b.count == m.count, "BitArray matches the model")
        try Stitch.check(b == BitArray(m) && b.hashValue == BitArray(m).hashValue, "equality and hashing")
        try Stitch.check(b.firstIndex(of: true) == m.firstIndex(of: true) && b.lastIndex(of: true) == m.lastIndex(of: true), "firstIndex / lastIndex")
    }
}
final class TS { var t = TreeSet<Int>(); var m = Set<Int>()
    func check() throws {
        try Stitch.check(Set(t) == m && t.count == m.count, "TreeSet matches the model")
        try Stitch.check(t == TreeSet(m) && t.hashValue == TreeSet(m).hashValue, "equality and hashing")
        for x in m { try Stitch.check(t.contains(x), "contains"); try Stitch.check(t.firstIndex(of: x).map { t[$0] } == x, "firstIndex(of:)") }
        var n = 0; var i = t.startIndex
        while i != t.endIndex { n += 1; i = t.index(after: i); if n > m.count + 1 { break } }
        try Stitch.check(n == m.count, "index(after:) walks every element")
    }
}
final class TSS { var t = TreeSet<String>(); var m = Set<String>()
    func check() throws {
        try Stitch.check(Set(t) == m && t.count == m.count, "TreeSet<String> matches the model")
        try Stitch.check(t == TreeSet(m), "equality")
    }
}
final class TD { var t = TreeDictionary<Int, Int>(); var m: [Int: Int] = [:]
    func check() throws {
        try Stitch.check(t.count == m.count, "count")
        for (k, val) in m { try Stitch.check(t[k] == val, "lookups match the model") }
        try Stitch.check(Dictionary(uniqueKeysWithValues: t.map { ($0.key, $0.value) }) == m, "iteration matches the model")
        try Stitch.check(t == TreeDictionary(uniqueKeysWithValues: m.map { ($0.key, $0.value) }), "equality")
        try Stitch.check(Set(t.keys) == Set(m.keys) && t.values.count == m.count, "keys / values views")
    }
}
final class TDS { var t = TreeDictionary<String, Int>(); var m: [String: Int] = [:]
    func check() throws {
        try Stitch.check(t.count == m.count, "count")
        for (k, val) in m { try Stitch.check(t[k] == val, "lookups match the model") }
    }
}
enum DQo: StitchObject { typealias Handle = DQ }
enum OSo: StitchObject { typealias Handle = OS }
enum OSSo: StitchObject { typealias Handle = OSS }
enum ODo: StitchObject { typealias Handle = OD }
enum HPo: StitchObject { typealias Handle = HP }
enum BSo: StitchObject { typealias Handle = BS }
enum BAo: StitchObject { typealias Handle = BA }
enum TSo: StitchObject { typealias Handle = TS }
enum TSSo: StitchObject { typealias Handle = TSS }
enum TDo: StitchObject { typealias Handle = TD }
enum TDSo: StitchObject { typealias Handle = TDS }

func clone(_ v: DQ) -> DQ { let c = DQ(); let b = (value: v, 0); _ = b.1; c.d = b.value.d; c.m = b.value.m; return c }
func clone(_ v: OS) -> OS { let c = OS(); let b = (value: v, 0); _ = b.1; c.s = b.value.s; c.m = b.value.m; return c }
func clone(_ v: OSS) -> OSS { let c = OSS(); let b = (value: v, 0); _ = b.1; c.s = b.value.s; c.m = b.value.m; return c }
func clone(_ v: OD) -> OD { let c = OD(); let b = (value: v, 0); _ = b.1; c.d = b.value.d; c.k = b.value.k; c.vals = b.value.vals; return c }
func clone(_ v: HP) -> HP { let c = HP(); let b = (value: v, 0); _ = b.1; c.h = b.value.h; c.m = b.value.m; return c }
func clone(_ v: BS) -> BS { let c = BS(); let b = (value: v, 0); _ = b.1; c.b = b.value.b; c.m = b.value.m; return c }
func clone(_ v: BA) -> BA { let c = BA(); let b = (value: v, 0); _ = b.1; c.b = b.value.b; c.m = b.value.m; return c }
func clone(_ v: TS) -> TS { let c = TS(); let b = (value: v, 0); _ = b.1; c.t = b.value.t; c.m = b.value.m; return c }
func clone(_ v: TSS) -> TSS { let c = TSS(); let b = (value: v, 0); _ = b.1; c.t = b.value.t; c.m = b.value.m; return c }
func clone(_ v: TD) -> TD { let c = TD(); let b = (value: v, 0); _ = b.1; c.t = b.value.t; c.m = b.value.m; return c }
func clone(_ v: TDS) -> TDS { let c = TDS(); let b = (value: v, 0); _ = b.1; c.t = b.value.t; c.m = b.value.m; return c }

let dq_new = endpoint("dq_new") { () throws -> New<DQo> in
    New(DQ())
}

let dq_copy = endpoint("dq_copy") { (b: Borrow<DQo>) throws -> New<DQo> in
    let c = DQ(); c.d = b.value.d; c.m = b.value.m
    return New(c)
}

let dq_append = endpoint("dq_append") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); x.d.append(a); x.m.append(a)
    try x.check()
}

let dq_prepend = endpoint("dq_prepend") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); x.d.prepend(a); x.m.insert(a, at: 0)
    try x.check()
}

let dq_insert = endpoint("dq_insert") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let i = s.range(0, x.m.count); let a = v(s); x.d.insert(a, at: i); x.m.insert(a, at: i)
    try x.check()
}

let dq_remove_at = endpoint("dq_remove_at") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.d.append(7); x.m.append(7) }; let i = s.range(0, x.m.count - 1); try check(x.d.remove(at: i) == x.m.remove(at: i), "remove(at:) returns the element")
    try x.check()
}

let dq_remove_first = endpoint("dq_remove_first") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.d.append(7); x.m.append(7) }; try check(x.d.removeFirst() == x.m.removeFirst(), "removeFirst")
    try x.check()
}

let dq_remove_last = endpoint("dq_remove_last") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.d.append(7); x.m.append(7) }; try check(x.d.removeLast() == x.m.removeLast(), "removeLast")
    try x.check()
}

let dq_pop_first = endpoint("dq_pop_first") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    try check(x.d.popFirst() == (x.m.isEmpty ? nil : x.m.removeFirst()), "popFirst")
    try x.check()
}

let dq_pop_last = endpoint("dq_pop_last") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    try check(x.d.popLast() == x.m.popLast(), "popLast")
    try x.check()
}

let dq_append_contents = endpoint("dq_append_contents") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 6)).map { _ in v(s) }; x.d.append(contentsOf: a); x.m.append(contentsOf: a)
    try x.check()
}

let dq_prepend_contents = endpoint("dq_prepend_contents") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 6)).map { _ in v(s) }; x.d.prepend(contentsOf: a); x.m.insert(contentsOf: a, at: 0)
    try x.check()
}

let dq_insert_contents = endpoint("dq_insert_contents") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let i = s.range(0, x.m.count); let a = (0..<s.range(0, 6)).map { _ in v(s) }; x.d.insert(contentsOf: a, at: i); x.m.insert(contentsOf: a, at: i)
    try x.check()
}

let dq_replace_subrange = endpoint("dq_replace_subrange") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); let a = (0..<s.range(0, 6)).map { _ in v(s) }; x.d.replaceSubrange(lo..<hi, with: a); x.m.replaceSubrange(lo..<hi, with: a)
    try x.check()
}

let dq_remove_subrange = endpoint("dq_remove_subrange") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); x.d.removeSubrange(lo..<hi); x.m.removeSubrange(lo..<hi)
    try x.check()
}

let dq_remove_first_k = endpoint("dq_remove_first_k") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = s.range(0, x.m.count); x.d.removeFirst(k); x.m.removeFirst(k)
    try x.check()
}

let dq_remove_last_k = endpoint("dq_remove_last_k") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = s.range(0, x.m.count); x.d.removeLast(k); x.m.removeLast(k)
    try x.check()
}

let dq_remove_all_where = endpoint("dq_remove_all_where") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.d.removeAll { $0 < t }; x.m.removeAll { $0 < t }
    try x.check()
}

let dq_remove_all_keep = endpoint("dq_remove_all_keep") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.removeAll(keepingCapacity: s.coin()); x.m.removeAll()
    try x.check()
}

let dq_subscript_set = endpoint("dq_subscript_set") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.d.append(7); x.m.append(7) }; let i = s.range(0, x.m.count - 1); let a = v(s); x.d[i] = a; x.m[i] = a
    try x.check()
}

let dq_swap_at = endpoint("dq_swap_at") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.d.append(7); x.m.append(7) }; let i = s.range(0, x.m.count - 1), j = s.range(0, x.m.count - 1); x.d.swapAt(i, j); x.m.swapAt(i, j)
    try x.check()
}

let dq_sort = endpoint("dq_sort") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.sort(); x.m.sort()
    try x.check()
}

let dq_reverse = endpoint("dq_reverse") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.reverse(); x.m.reverse()
    try x.check()
}

let dq_reserve = endpoint("dq_reserve") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.reserveCapacity(s.range(0, 100))
    try x.check()
}

let dq_slice = endpoint("dq_slice") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); try check(Array(x.d[lo..<hi]) == Array(x.m[lo..<hi]), "slices match")
    try x.check()
}

let dq_copy_on_write = endpoint("dq_copy_on_write") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = x.d; c.append(99); c.prepend(-99); try check(Array(x.d) == x.m, "a copy's mutation does not change the original"); try check(c.count == x.m.count + 2, "the copy changed")
    try x.check()
}

let dq_index_math = endpoint("dq_index_math") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = x.m.count; let i = s.range(0, n); try check(x.d.index(x.d.startIndex, offsetBy: i) == i && x.d.distance(from: 0, to: i) == i, "index arithmetic")
    try x.check()
}

let dq_with_unsafe = endpoint("dq_with_unsafe") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.d.withContiguousMutableStorageIfAvailable({ (buf: inout UnsafeMutableBufferPointer<Int>) -> Bool in for k in buf.indices { buf[k] = buf[k] &+ 1 }; return true }) == nil { for k in x.d.indices { x.d[k] = x.d[k] &+ 1 } }; x.m = x.m.map { $0 &+ 1 }
    try x.check()
}

let dq_rotate_by_append = endpoint("dq_rotate_by_append") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    for _ in 0..<s.range(0, 10) { if let f = x.d.popFirst() { x.d.append(f); x.m.append(x.m.removeFirst()) } }
    try x.check()
}

let dq_init_repeating = endpoint("dq_init_repeating") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = s.range(0, 20); let a = v(s); x.d = Deque(repeating: a, count: n); x.m = Array(repeating: a, count: n)
    try x.check()
}

let dq_init_unsafe_uninit = endpoint("dq_init_unsafe_uninit") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = s.range(0, 20); x.d = Deque(unsafeUninitializedCapacity: n) { buf, c in for k in 0..<n { buf.initializeElement(at: k, to: k) }; c = n }; x.m = Array(0..<n)
    try x.check()
}

let dq_description = endpoint("dq_description") { (b: Borrow<DQo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    _ = x.d.description; _ = x.d.debugDescription
    try x.check()
}

let os_new = endpoint("os_new") { () throws -> New<OSo> in
    New(OS())
}

let os_copy = endpoint("os_copy") { (b: Borrow<OSo>) throws -> New<OSo> in
    let c = OS(); c.s = b.value.s; c.m = b.value.m
    return New(c)
}

let os_append = endpoint("os_append") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); let r = x.s.append(a); try check(r.inserted == x.add(a), "append reports insertion")
    try x.check()
}

let os_insert = endpoint("os_insert") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); let i = s.range(0, x.m.count); let r = x.s.insert(a, at: i); if !x.m.contains(a) { x.m.insert(a, at: i); try check(r.inserted && r.index == i, "insert(at:)") } else { try check(!r.inserted, "insert of an existing element") }
    try x.check()
}

let os_remove = endpoint("os_remove") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); let r = x.s.remove(a); if let i = x.m.firstIndex(of: a) { x.m.remove(at: i); try check(r == a, "remove returns the element") } else { try check(r == nil, "remove of a missing element") }
    try x.check()
}

let os_remove_at = endpoint("os_remove_at") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { _ = x.s.append(7); _ = x.add(7) }; let i = s.range(0, x.m.count - 1); try check(x.s.remove(at: i) == x.m.remove(at: i), "remove(at:)")
    try x.check()
}

let os_remove_first = endpoint("os_remove_first") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { _ = x.s.append(7); _ = x.add(7) }; try check(x.s.removeFirst() == x.m.removeFirst(), "removeFirst")
    try x.check()
}

let os_remove_last = endpoint("os_remove_last") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { _ = x.s.append(7); _ = x.add(7) }; try check(x.s.removeLast() == x.m.removeLast(), "removeLast")
    try x.check()
}

let os_update_at = endpoint("os_update_at") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { _ = x.s.append(7); _ = x.add(7) }; let i = s.range(0, x.m.count - 1); let old = x.s.update(x.m[i], at: i); try check(old == x.m[i], "update(_:at:) with the same element")
    try x.check()
}

let os_update_or_append = endpoint("os_update_or_append") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); let old = x.s.updateOrAppend(a); try check((old != nil) == x.m.contains(a), "updateOrAppend"); _ = x.add(a)
    try x.check()
}

let os_union = endpoint("os_union") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 6)).map { _ in v(s) }; x.s = x.s.union(o); for a in o { _ = x.add(a) }
    try x.check()
}

let os_form_union = endpoint("os_form_union") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 6)).map { _ in v(s) }; x.s.formUnion(o); for a in o { _ = x.add(a) }
    try x.check()
}

let os_intersection = endpoint("os_intersection") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in v(s) }); x.s = x.s.intersection(o); x.m = x.m.filter { o.contains($0) }
    try x.check()
}

let os_form_intersection = endpoint("os_form_intersection") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in v(s) }); x.s.formIntersection(o); x.m = x.m.filter { o.contains($0) }
    try x.check()
}

let os_subtracting = endpoint("os_subtracting") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in v(s) }); x.s = x.s.subtracting(o); x.m = x.m.filter { !o.contains($0) }
    try x.check()
}

let os_subtract = endpoint("os_subtract") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in v(s) }); x.s.subtract(o); x.m = x.m.filter { !o.contains($0) }
    try x.check()
}

let os_symmetric_difference = endpoint("os_symmetric_difference") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = OrderedSet((0..<s.range(0, 6)).map { _ in v(s) }); x.s = x.s.symmetricDifference(o); let keep = x.m.filter { !o.contains($0) }; let add = o.filter { !x.m.contains($0) }; x.m = keep + add
    try x.check()
}

let os_form_symmetric_difference = endpoint("os_form_symmetric_difference") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = OrderedSet((0..<s.range(0, 6)).map { _ in v(s) }); x.s.formSymmetricDifference(o); let keep = x.m.filter { !o.contains($0) }; let add = o.filter { !x.m.contains($0) }; x.m = keep + add
    try x.check()
}

let os_relations = endpoint("os_relations") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = OrderedSet((0..<s.range(0, 6)).map { _ in v(s) }); let a = Set(x.m), b = Set(o); try check(x.s.isSubset(of: o) == a.isSubset(of: b) && x.s.isSuperset(of: o) == a.isSuperset(of: b) && x.s.isDisjoint(with: o) == a.isDisjoint(with: b) && x.s.isStrictSubset(of: o) == a.isStrictSubset(of: b) && x.s.isStrictSuperset(of: o) == a.isStrictSuperset(of: b) && x.s.isEqualSet(to: o) == (a == b), "set relations match Set")
    try x.check()
}

let os_filter = endpoint("os_filter") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.s = x.s.filter { $0 < t }; x.m = x.m.filter { $0 < t }
    try x.check()
}

let os_remove_all_where = endpoint("os_remove_all_where") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.s.removeAll { $0 % 2 == 0 && $0 > t }; x.m.removeAll { $0 % 2 == 0 && $0 > t }
    try x.check()
}

let os_sort = endpoint("os_sort") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.sort(); x.m.sort()
    try x.check()
}

let os_sort_by = endpoint("os_sort_by") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.sort(by: >); x.m.sort(by: >)
    try x.check()
}

let os_reverse = endpoint("os_reverse") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.reverse(); x.m.reverse()
    try x.check()
}

let os_swap_at = endpoint("os_swap_at") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { _ = x.s.append(7); _ = x.add(7) }; let i = s.range(0, x.m.count - 1), j = s.range(0, x.m.count - 1); x.s.swapAt(i, j); x.m.swapAt(i, j)
    try x.check()
}

let os_partition = endpoint("os_partition") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); let p = x.s.partition { $0 > t }; try check(x.s[..<p].allSatisfy { $0 <= t } && x.s[p...].allSatisfy { $0 > t }, "partition"); x.m = Array(x.s)
    try x.check()
}

let os_remove_subrange = endpoint("os_remove_subrange") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); x.s.removeSubrange(lo..<hi); x.m.removeSubrange(lo..<hi)
    try x.check()
}

let os_elements_view = endpoint("os_elements_view") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    try check(x.s.elements == x.m, "elements"); try check(x.s.unordered == OrderedSet(x.m.sorted()).unordered, "unordered")
    try x.check()
}

let os_unordered_ops = endpoint("os_unordered_ops") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in v(s) }); try check(x.s.unordered.isSubset(of: o) == Set(x.m).isSubset(of: o), "unordered isSubset"); var u = x.s.unordered; u.insert(v(s)); _ = u.remove(v(s))
    try x.check()
}

let os_init_unchecked = endpoint("os_init_unchecked") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = Array(Set((0..<s.range(0, 8)).map { _ in v(s) })); x.s = OrderedSet(uncheckedUniqueElements: a); x.m = a
    try x.check()
}

let os_copy_on_write = endpoint("os_copy_on_write") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = x.s; c.append(1000); c.removeAll(); try check(Array(x.s) == x.m, "a copy's mutation does not change the original")
    try x.check()
}

let os_first_index_last = endpoint("os_first_index_last") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if let a = x.m.last { try check(x.s.lastIndex(of: a) == x.m.count - 1, "lastIndex(of:)") }
    try x.check()
}

let os_remove_all_keep = endpoint("os_remove_all_keep") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.removeAll(keepingCapacity: s.coin()); x.m.removeAll()
    try x.check()
}

let os_reserve = endpoint("os_reserve") { (b: Borrow<OSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.reserveCapacity(s.range(0, 200))
    try x.check()
}

let oss_new = endpoint("oss_new") { () throws -> New<OSSo> in
    New(OSS())
}

let oss_copy = endpoint("oss_copy") { (b: Borrow<OSSo>) throws -> New<OSSo> in
    let c = OSS(); c.s = b.value.s; c.m = b.value.m
    return New(c)
}

let oss_append = endpoint("oss_append") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = w(s); let r = x.s.append(a); if !x.m.contains(a) { x.m.append(a) }; try check(r.inserted == (x.m.last == a && x.m.firstIndex(of: a) == x.m.count - 1 && r.index == x.m.count - 1) || !r.inserted, "append")
    try x.check()
}

let oss_remove = endpoint("oss_remove") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = w(s); let r = x.s.remove(a); if let i = x.m.firstIndex(of: a) { x.m.remove(at: i) }; _ = r
    try x.check()
}

let oss_union = endpoint("oss_union") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 6)).map { _ in w(s) }; x.s.formUnion(o); for a in o where !x.m.contains(a) { x.m.append(a) }
    try x.check()
}

let oss_subtract = endpoint("oss_subtract") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in w(s) }); x.s.subtract(o); x.m = x.m.filter { !o.contains($0) }
    try x.check()
}

let oss_sort = endpoint("oss_sort") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.s.sort(); x.m.sort()
    try x.check()
}

let oss_intersect = endpoint("oss_intersect") { (b: Borrow<OSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in w(s) }); x.s.formIntersection(o); x.m = x.m.filter { o.contains($0) }
    try x.check()
}

let od_new = endpoint("od_new") { () throws -> New<ODo> in
    New(OD())
}

let od_copy = endpoint("od_copy") { (b: Borrow<ODo>) throws -> New<ODo> in
    let c = OD(); c.d = b.value.d; c.k = b.value.k; c.vals = b.value.vals
    return New(c)
}

let od_update_value = endpoint("od_update_value") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s), val = v(s); let old = x.d.updateValue(val, forKey: k); try check(old == x.vals[k], "updateValue returns the old value"); x.set(k, val)
    try x.check()
}

let od_update_value_inserting = endpoint("od_update_value_inserting") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s), val = v(s); let i = s.range(0, x.k.count); let r = x.d.updateValue(val, forKey: k, insertingAt: i); if x.vals[k] == nil { x.k.insert(k, at: i); x.vals[k] = val; try check(r.originalMember == nil && r.index == i, "inserted at the index") } else { try check(r.originalMember == x.vals[k], "existing key"); x.vals[k] = val }
    try x.check()
}

let od_remove_value = endpoint("od_remove_value") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); let old = x.d.removeValue(forKey: k); try check(old == x.vals[k], "removeValue"); x.set(k, nil)
    try x.check()
}

let od_subscript_set = endpoint("od_subscript_set") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s), val = v(s); x.d[k] = val; x.set(k, val)
    try x.check()
}

let od_subscript_nil = endpoint("od_subscript_nil") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); x.d[k] = nil; x.set(k, nil)
    try x.check()
}

let od_subscript_default = endpoint("od_subscript_default") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); x.d[k, default: 0] += 1; x.set(k, (x.vals[k] ?? 0) + 1)
    try x.check()
}

let od_merge_keep_old = endpoint("od_merge_keep_old") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = (0..<s.range(0, 5)).map { _ in (v(s), v(s)) }; x.d.merge(pairs) { a, _ in a }; for (k, val) in pairs where x.vals[k] == nil { x.set(k, val) }
    try x.check()
}

let od_merge_keep_new = endpoint("od_merge_keep_new") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = (0..<s.range(0, 5)).map { _ in (v(s), v(s)) }; x.d.merge(pairs) { _, b in b }; for (k, val) in pairs { x.set(k, val) }
    try x.check()
}

let od_filter = endpoint("od_filter") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.d = x.d.filter { $0.value < t }; for k in x.k where x.vals[k]! >= t { x.set(k, nil) }
    try x.check()
}

let od_map_values = endpoint("od_map_values") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d = x.d.mapValues { $0 &* 2 }; for k in x.k { x.vals[k] = x.vals[k]! &* 2 }
    try x.check()
}

let od_compact_map_values = endpoint("od_compact_map_values") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d = x.d.compactMapValues { $0 % 3 == 0 ? nil : $0 }; for k in x.k where x.vals[k]! % 3 == 0 { x.set(k, nil) }
    try x.check()
}

let od_sort_keys = endpoint("od_sort_keys") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.sort { $0.key < $1.key }; x.k.sort()
    try x.check()
}

let od_sort_values = endpoint("od_sort_values") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.sort { ($0.value, $0.key) < ($1.value, $1.key) }; x.k.sort { (x.vals[$0]!, $0) < (x.vals[$1]!, $1) }
    try x.check()
}

let od_reverse = endpoint("od_reverse") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.d.reverse(); x.k.reverse()
    try x.check()
}

let od_swap_at = endpoint("od_swap_at") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.k.isEmpty { x.d[7] = 7; x.set(7, 7) }; let i = s.range(0, x.k.count - 1), j = s.range(0, x.k.count - 1); x.d.swapAt(i, j); x.k.swapAt(i, j)
    try x.check()
}

let od_remove_at = endpoint("od_remove_at") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.k.isEmpty { x.d[7] = 7; x.set(7, 7) }; let i = s.range(0, x.k.count - 1); let e = x.d.remove(at: i); try check(e.key == x.k[i], "remove(at:)"); x.set(x.k[i], nil)
    try x.check()
}

let od_remove_first = endpoint("od_remove_first") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.k.isEmpty { x.d[7] = 7; x.set(7, 7) }; let e = x.d.removeFirst(); try check(e.key == x.k[0], "removeFirst"); x.set(e.key, nil)
    try x.check()
}

let od_remove_last = endpoint("od_remove_last") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.k.isEmpty { x.d[7] = 7; x.set(7, 7) }; let e = x.d.removeLast(); try check(e.key == x.k.last!, "removeLast"); x.set(e.key, nil)
    try x.check()
}

let od_remove_all_where = endpoint("od_remove_all_where") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.d.removeAll { $0.value > t }; for k in x.k where x.vals[k]! > t { x.set(k, nil) }
    try x.check()
}

let od_remove_subrange = endpoint("od_remove_subrange") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.k.count); let hi = s.range(lo, x.k.count); x.d.removeSubrange(lo..<hi); for key in x.k[lo..<hi] { x.vals[key] = nil }; x.k.removeSubrange(lo..<hi)
    try x.check()
}

let od_values_mutate = endpoint("od_values_mutate") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.k.isEmpty { x.d[7] = 7; x.set(7, 7) }; let i = s.range(0, x.k.count - 1); x.d.values[i] = 7; x.vals[x.k[i]] = 7
    try x.check()
}

let od_elements_slice = endpoint("od_elements_slice") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.k.count); let hi = s.range(lo, x.k.count); try check(x.d.elements[lo..<hi].map { $0.key } == Array(x.k[lo..<hi]), "elements slice")
    try x.check()
}

let od_grouping = endpoint("od_grouping") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 8)).map { _ in v(s) }; let g = OrderedDictionary(grouping: a) { $0 % 3 }; var n = 0; for (_, vs) in g { n += vs.count }; try check(n == a.count, "grouping keeps every element")
    try x.check()
}

let od_unique_keys_init = endpoint("od_unique_keys_init") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let ks = Array(Set((0..<s.range(0, 8)).map { _ in v(s) })); x.d = OrderedDictionary(uniqueKeysWithValues: ks.map { ($0, $0) }); x.k = ks; x.vals = Dictionary(uniqueKeysWithValues: ks.map { ($0, $0) })
    try x.check()
}

let od_copy_on_write = endpoint("od_copy_on_write") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = x.d; c[12345] = 1; c.removeAll(); try check(Array(x.d.keys) == x.k, "a copy's mutation does not change the original")
    try x.check()
}

let od_keys_view = endpoint("od_keys_view") { (b: Borrow<ODo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    for (i, k) in x.k.enumerated() { try check(x.d.keys.firstIndex(of: k) == i, "keys.firstIndex(of:)") }; try check(x.d.keys.elements == x.k, "keys.elements")
    try x.check()
}

let hp_new = endpoint("hp_new") { () throws -> New<HPo> in
    New(HP())
}

let hp_copy = endpoint("hp_copy") { (b: Borrow<HPo>) throws -> New<HPo> in
    let c = HP(); c.h = b.value.h; c.m = b.value.m
    return New(c)
}

let hp_insert = endpoint("hp_insert") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); x.h.insert(a); x.m.append(a)
    try x.check()
}

let hp_insert_contents = endpoint("hp_insert_contents") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 8)).map { _ in v(s) }; x.h.insert(contentsOf: a); x.m += a
    try x.check()
}

let hp_pop_min = endpoint("hp_pop_min") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let r = x.h.popMin(); try check(r == x.m.min(), "popMin returns the minimum"); if let r = r { x.m.remove(at: x.m.firstIndex(of: r)!) }
    try x.check()
}

let hp_pop_max = endpoint("hp_pop_max") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let r = x.h.popMax(); try check(r == x.m.max(), "popMax returns the maximum"); if let r = r { x.m.remove(at: x.m.firstIndex(of: r)!) }
    try x.check()
}

let hp_remove_min = endpoint("hp_remove_min") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.h.insert(7); x.m.append(7) }; let r = x.h.removeMin(); try check(r == x.m.min()!, "removeMin"); x.m.remove(at: x.m.firstIndex(of: r)!)
    try x.check()
}

let hp_remove_max = endpoint("hp_remove_max") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.h.insert(7); x.m.append(7) }; let r = x.h.removeMax(); try check(r == x.m.max()!, "removeMax"); x.m.remove(at: x.m.firstIndex(of: r)!)
    try x.check()
}

let hp_replace_min = endpoint("hp_replace_min") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.h.insert(7); x.m.append(7) }; let a = v(s); let r = x.h.replaceMin(with: a); try check(r == x.m.min()!, "replaceMin returns the old minimum"); x.m.remove(at: x.m.firstIndex(of: r)!); x.m.append(a)
    try x.check()
}

let hp_replace_max = endpoint("hp_replace_max") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.h.insert(7); x.m.append(7) }; let a = v(s); let r = x.h.replaceMax(with: a); try check(r == x.m.max()!, "replaceMax returns the old maximum"); x.m.remove(at: x.m.firstIndex(of: r)!); x.m.append(a)
    try x.check()
}

let hp_init_sequence = endpoint("hp_init_sequence") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 20)).map { _ in v(s) }; x.h = Heap(a); x.m = a
    try x.check()
}

let hp_drain_min = endpoint("hp_drain_min") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var out: [Int] = []; var c = x.h; while let m = c.popMin() { out.append(m) }; try check(out == x.m.sorted(), "popping every minimum sorts")
    try x.check()
}

let hp_drain_max = endpoint("hp_drain_max") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var out: [Int] = []; var c = x.h; while let m = c.popMax() { out.append(m) }; try check(out == x.m.sorted(by: >), "popping every maximum sorts descending")
    try x.check()
}

let hp_array_literal = endpoint("hp_array_literal") { (b: Borrow<HPo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let h: Heap<Int> = [3, 1, 2]; try check(h.min == 1 && h.max == 3, "literal")
    try x.check()
}

let bs_new = endpoint("bs_new") { () throws -> New<BSo> in
    New(BS())
}

let bs_copy = endpoint("bs_copy") { (b: Borrow<BSo>) throws -> New<BSo> in
    let c = BS(); c.b = b.value.b; c.m = b.value.m
    return New(c)
}

let bs_insert = endpoint("bs_insert") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = s.range(0, 300); let r = x.b.insert(a); try check(r.inserted == x.m.insert(a).inserted, "insert reports insertion")
    try x.check()
}

let bs_remove = endpoint("bs_remove") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = s.range(0, 300); try check(x.b.remove(a) == x.m.remove(a), "remove")
    try x.check()
}

let bs_update = endpoint("bs_update") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = s.range(0, 300); _ = x.b.update(with: a); x.m.insert(a)
    try x.check()
}

let bs_insert_range = endpoint("bs_insert_range") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, 200); let hi = s.range(lo, lo + 100); x.b.formUnion(BitSet(lo..<hi)); for i in lo..<hi { x.m.insert(i) }
    try x.check()
}

let bs_remove_range = endpoint("bs_remove_range") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, 200); let hi = s.range(lo, lo + 100); x.b.subtract(BitSet(lo..<hi)); for i in lo..<hi { x.m.remove(i) }
    try x.check()
}

let bs_union = endpoint("bs_union") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitSet((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); x.b = x.b.union(o); x.m.formUnion(o)
    try x.check()
}

let bs_intersection = endpoint("bs_intersection") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitSet((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); x.b = x.b.intersection(o); x.m.formIntersection(o)
    try x.check()
}

let bs_subtracting = endpoint("bs_subtracting") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitSet((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); x.b = x.b.subtracting(o); x.m.subtract(o)
    try x.check()
}

let bs_symmetric_difference = endpoint("bs_symmetric_difference") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitSet((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); x.b = x.b.symmetricDifference(o); x.m.formSymmetricDifference(o)
    try x.check()
}

let bs_form_union_seq = endpoint("bs_form_union_seq") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 8)).map { _ in s.range(0, 300) }; x.b.formUnion(o); x.m.formUnion(o)
    try x.check()
}

let bs_form_intersection_seq = endpoint("bs_form_intersection_seq") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 8)).map { _ in s.range(0, 300) }; x.b.formIntersection(o); x.m.formIntersection(o)
    try x.check()
}

let bs_form_symdiff_seq = endpoint("bs_form_symdiff_seq") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); x.b.formSymmetricDifference(o); x.m.formSymmetricDifference(o)
    try x.check()
}

let bs_relations = endpoint("bs_relations") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitSet((0..<s.range(0, 8)).map { _ in s.range(0, 300) }); let b = Set(o); try check(x.b.isSubset(of: o) == x.m.isSubset(of: b) && x.b.isSuperset(of: o) == x.m.isSuperset(of: b) && x.b.isDisjoint(with: o) == x.m.isDisjoint(with: b) && x.b.isStrictSubset(of: o) == x.m.isStrictSubset(of: b) && x.b.isEqualSet(to: o) == (x.m == b), "set relations match Set")
    try x.check()
}

let bs_relations_range = endpoint("bs_relations_range") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, 200); let r = lo..<s.range(lo, 300); let b = Set(r); try check(x.b.isSubset(of: r) == x.m.isSubset(of: b), "isSubset(of: Range)"); try check(x.b.isSuperset(of: r) == x.m.isSuperset(of: b), "isSuperset(of: Range)"); try check(x.b.isDisjoint(with: r) == x.m.isDisjoint(with: b), "isDisjoint(with: Range)"); try check(x.b.isStrictSubset(of: r) == x.m.isStrictSubset(of: b), "isStrictSubset(of: Range)"); try check(x.b.isStrictSuperset(of: r) == x.m.isStrictSuperset(of: b), "isStrictSuperset(of: Range)")
    try x.check()
}

let bs_filter = endpoint("bs_filter") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = s.range(0, 300); x.b = x.b.filter { $0 < t }; x.m = x.m.filter { $0 < t }
    try x.check()
}

let bs_remove_all = endpoint("bs_remove_all") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.b = BitSet(); x.m.removeAll()
    try x.check()
}

let bs_remove_min = endpoint("bs_remove_min") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let r = x.b.min(); try check(r == x.m.min(), "min"); if let r = r { try check(x.b.remove(r) == r, "remove min"); x.m.remove(r) }
    try x.check()
}

let bs_remove_max = endpoint("bs_remove_max") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let r = x.b.max(); try check(r == x.m.max(), "max"); if let r = r { try check(x.b.remove(r) == r, "remove max"); x.m.remove(r) }
    try x.check()
}

let bs_remove_at_index = endpoint("bs_remove_at_index") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.b.insert(7); x.m.insert(7) }; let i = x.b.index(x.b.startIndex, offsetBy: s.range(0, x.m.count - 1)); let e = x.b.remove(at: i); try check(x.m.remove(e) != nil, "remove(at:) removes a member")
    try x.check()
}

let bs_subscript_range = endpoint("bs_subscript_range") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, 300); let hi = s.range(lo, 400); try check(Array(x.b[members: lo..<hi]) == x.m.filter { $0 >= lo && $0 < hi }.sorted(), "range slice")
    try x.check()
}

let bs_counted = endpoint("bs_counted") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = BitSet.Counted(x.b); try check(c.count == x.m.count, "Counted count"); c.insert(500); try check(c.count == x.m.count + (x.m.contains(500) ? 0 : 1), "Counted insert")
    try x.check()
}

let bs_bitarray_conversion = endpoint("bs_bitarray_conversion") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = BitArray(x.b); try check(BitSet(a) == x.b, "BitSet(BitArray(b)) == b")
    try x.check()
}

let bs_index_walk = endpoint("bs_index_walk") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var i = x.b.startIndex; var n = 0; while i != x.b.endIndex { n += 1; i = x.b.index(after: i); if n > x.m.count + 1 { break } }; try check(n == x.m.count, "index(after:) walks every member")
    try x.check()
}

let bs_index_before = endpoint("bs_index_before") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var i = x.b.endIndex; var n = 0; while i != x.b.startIndex { i = x.b.index(before: i); n += 1; if n > x.m.count + 1 { break } }; try check(n == x.m.count, "index(before:) walks every member")
    try x.check()
}

let bs_description = endpoint("bs_description") { (b: Borrow<BSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    _ = x.b.description; _ = x.b.debugDescription
    try x.check()
}

let ba_new = endpoint("ba_new") { () throws -> New<BAo> in
    New(BA())
}

let ba_copy = endpoint("ba_copy") { (b: Borrow<BAo>) throws -> New<BAo> in
    let c = BA(); c.b = b.value.b; c.m = b.value.m
    return New(c)
}

let ba_append = endpoint("ba_append") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = s.coin(); x.b.append(a); x.m.append(a)
    try x.check()
}

let ba_append_contents = endpoint("ba_append_contents") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 70)).map { _ in s.coin() }; x.b.append(contentsOf: a); x.m += a
    try x.check()
}

let ba_insert = endpoint("ba_insert") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let i = s.range(0, x.m.count); let a = s.coin(); x.b.insert(a, at: i); x.m.insert(a, at: i)
    try x.check()
}

let ba_remove_at = endpoint("ba_remove_at") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.b.append(true); x.m.append(true) }; let i = s.range(0, x.m.count - 1); try check(x.b.remove(at: i) == x.m.remove(at: i), "remove(at:)")
    try x.check()
}

let ba_remove_last = endpoint("ba_remove_last") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.b.append(true); x.m.append(true) }; try check(x.b.removeLast() == x.m.removeLast(), "removeLast")
    try x.check()
}

let ba_remove_first_k = endpoint("ba_remove_first_k") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = s.range(0, x.m.count); x.b.removeFirst(k); x.m.removeFirst(k)
    try x.check()
}

let ba_remove_subrange = endpoint("ba_remove_subrange") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); x.b.removeSubrange(lo..<hi); x.m.removeSubrange(lo..<hi)
    try x.check()
}

let ba_replace_subrange = endpoint("ba_replace_subrange") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); let a = (0..<s.range(0, 70)).map { _ in s.coin() }; x.b.replaceSubrange(lo..<hi, with: a); x.m.replaceSubrange(lo..<hi, with: a)
    try x.check()
}

let ba_subscript_set = endpoint("ba_subscript_set") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.b.append(true); x.m.append(true) }; let i = s.range(0, x.m.count - 1); let a = s.coin(); x.b[i] = a; x.m[i] = a
    try x.check()
}

let ba_toggle_all = endpoint("ba_toggle_all") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.b.toggleAll(); x.m = x.m.map { !$0 }
    try x.check()
}

let ba_toggle_range = endpoint("ba_toggle_range") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); x.b.toggleAll(in: lo..<hi); for i in lo..<hi { x.m[i].toggle() }
    try x.check()
}

let ba_fill = endpoint("ba_fill") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); let a = s.coin(); x.b.fill(in: lo..<hi, with: a); for i in lo..<hi { x.m[i] = a }
    try x.check()
}

let ba_bitwise_via_bitset = endpoint("ba_bitwise_via_bitset") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = BitArray((0..<x.m.count).map { _ in s.coin() }); let ob = Array(o); let op = s.range(0, 2); let bs = BitSet(x.b), os2 = BitSet(o); if op == 0 { x.b = BitArray(bs.intersection(os2)); x.b.truncateOrExtend(toCount: x.m.count); x.m = zip(x.m, ob).map { $0 && $1 } } else if op == 1 { x.b = BitArray(bs.union(os2)); x.b.truncateOrExtend(toCount: x.m.count); x.m = zip(x.m, ob).map { $0 || $1 } } else { x.b = BitArray(bs.symmetricDifference(os2)); x.b.truncateOrExtend(toCount: x.m.count); x.m = zip(x.m, ob).map { $0 != $1 } }
    try x.check()
}

let ba_complement = endpoint("ba_complement") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = BitArray(repeating: true, count: x.m.count); for i in BitSet(x.b) { c[i] = false }; x.b = c; x.m = x.m.map { !$0 }
    try x.check()
}

let ba_shift = endpoint("ba_shift") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = s.range(0, x.m.count); let f = x.m.count; if s.coin() { x.b.truncateOrExtend(toCount: f + k); x.m += Array(repeating: false, count: k) } else { x.b.truncateOrExtend(toCount: max(0, f - k)); x.m = Array(x.m.prefix(max(0, f - k))) }
    try x.check()
}

let ba_init_integer = endpoint("ba_init_integer") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = UInt64(bitPattern: Int64(v(s))); x.b = BitArray(bitPattern: n); x.m = (0..<64).map { (n >> UInt64($0)) & 1 == 1 }
    try x.check()
}

let ba_init_repeating = endpoint("ba_init_repeating") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = s.range(0, 200); let a = s.coin(); x.b = BitArray(repeating: a, count: n); x.m = Array(repeating: a, count: n)
    try x.check()
}

let ba_random_seeded = endpoint("ba_random_seeded") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = s.range(0, 200); var g = SystemRandomNumberGenerator(); let r = BitArray.randomBits(count: n, using: &g); try check(r.count == n, "randomBits count")
    try x.check()
}

let ba_description_roundtrip = endpoint("ba_description_roundtrip") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let d = x.b.description; if let back = BitArray(d) { try check(back == x.b, "BitArray(description) == b") }
    try x.check()
}

let ba_slice = endpoint("ba_slice") { (b: Borrow<BAo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let lo = s.range(0, x.m.count); let hi = s.range(lo, x.m.count); try check(Array(x.b[lo..<hi]) == Array(x.m[lo..<hi]), "slice")
    try x.check()
}

let ts_new = endpoint("ts_new") { () throws -> New<TSo> in
    New(TS())
}

let ts_copy = endpoint("ts_copy") { (b: Borrow<TSo>) throws -> New<TSo> in
    let c = TS(); c.t = b.value.t; c.m = b.value.m
    return New(c)
}

let ts_insert = endpoint("ts_insert") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); try check(x.t.insert(a).inserted == x.m.insert(a).inserted, "insert")
    try x.check()
}

let ts_remove = endpoint("ts_remove") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); try check(x.t.remove(a) == x.m.remove(a), "remove")
    try x.check()
}

let ts_update = endpoint("ts_update") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = v(s); try check(x.t.update(with: a) == x.m.update(with: a), "update")
    try x.check()
}

let ts_union = endpoint("ts_union") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 8)).map { _ in v(s) }); x.t = x.t.union(o); x.m.formUnion(o)
    try x.check()
}

let ts_intersection = endpoint("ts_intersection") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 8)).map { _ in v(s) }); x.t = x.t.intersection(o); x.m.formIntersection(o)
    try x.check()
}

let ts_subtracting = endpoint("ts_subtracting") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 8)).map { _ in v(s) }); x.t = x.t.subtracting(o); x.m.subtract(o)
    try x.check()
}

let ts_symmetric_difference = endpoint("ts_symmetric_difference") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 8)).map { _ in v(s) }); x.t = x.t.symmetricDifference(o); x.m.formSymmetricDifference(o)
    try x.check()
}

let ts_form_union = endpoint("ts_form_union") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 8)).map { _ in v(s) }; x.t.formUnion(o); x.m.formUnion(o)
    try x.check()
}

let ts_form_intersection = endpoint("ts_form_intersection") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 8)).map { _ in v(s) }); x.t.formIntersection(o); x.m.formIntersection(o)
    try x.check()
}

let ts_subtract = endpoint("ts_subtract") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 8)).map { _ in v(s) }; x.t.subtract(o); x.m.subtract(o)
    try x.check()
}

let ts_form_symdiff = endpoint("ts_form_symdiff") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 8)).map { _ in v(s) }); x.t.formSymmetricDifference(o); x.m.formSymmetricDifference(o)
    try x.check()
}

let ts_relations = endpoint("ts_relations") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 8)).map { _ in v(s) }); let b = Set(o); try check(x.t.isSubset(of: o) == x.m.isSubset(of: b) && x.t.isSuperset(of: o) == x.m.isSuperset(of: b) && x.t.isDisjoint(with: o) == x.m.isDisjoint(with: b) && x.t.isStrictSubset(of: o) == x.m.isStrictSubset(of: b) && x.t.isStrictSuperset(of: o) == x.m.isStrictSuperset(of: b) && x.t.isEqualSet(to: o) == (x.m == b), "set relations match Set")
    try x.check()
}

let ts_relations_seq = endpoint("ts_relations_seq") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = (0..<s.range(0, 8)).map { _ in v(s) }; let b = Set(o); try check(x.t.isSubset(of: o) == x.m.isSubset(of: b) && x.t.isSuperset(of: o) == x.m.isSuperset(of: b) && x.t.isDisjoint(with: o) == x.m.isDisjoint(with: b), "relations with a sequence")
    try x.check()
}

let ts_filter = endpoint("ts_filter") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.t = x.t.filter { $0 < t }; x.m = x.m.filter { $0 < t }
    try x.check()
}

let ts_remove_at_index = endpoint("ts_remove_at_index") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.t.insert(7); x.m.insert(7) }; let k = s.range(0, x.m.count - 1); let i = x.t.index(x.t.startIndex, offsetBy: k); let e = x.t.remove(at: i); try check(x.m.remove(e) != nil, "remove(at:) removes a member")
    try x.check()
}

let ts_copy_on_write = endpoint("ts_copy_on_write") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = x.t; c.insert(12345); c.remove(v(s)); try check(Set(x.t) == x.m, "a copy's mutation does not change the original")
    try x.check()
}

let ts_dictionary_keys = endpoint("ts_dictionary_keys") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let d = TreeDictionary(uniqueKeysWithValues: x.t.map { ($0, 1) }); try check(TreeSet(d.keys) == x.t, "TreeSet(dictionary.keys)")
    try x.check()
}

let ts_index_math = endpoint("ts_index_math") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let n = x.m.count; let k = s.range(0, n); let i = x.t.index(x.t.startIndex, offsetBy: k); try check(x.t.distance(from: x.t.startIndex, to: i) == k, "distance(index(offsetBy:))")
    try x.check()
}

let ts_remove_all = endpoint("ts_remove_all") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.t = []; x.m.removeAll()
    try x.check()
}

let ts_description = endpoint("ts_description") { (b: Borrow<TSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    _ = x.t.description; _ = x.t.debugDescription
    try x.check()
}

let tss_new = endpoint("tss_new") { () throws -> New<TSSo> in
    New(TSS())
}

let tss_copy = endpoint("tss_copy") { (b: Borrow<TSSo>) throws -> New<TSSo> in
    let c = TSS(); c.t = b.value.t; c.m = b.value.m
    return New(c)
}

let tss_insert = endpoint("tss_insert") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = w(s); try check(x.t.insert(a).inserted == x.m.insert(a).inserted, "insert")
    try x.check()
}

let tss_remove = endpoint("tss_remove") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = w(s); try check(x.t.remove(a) == x.m.remove(a), "remove")
    try x.check()
}

let tss_union = endpoint("tss_union") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 6)).map { _ in w(s) }); x.t.formUnion(o); x.m.formUnion(o)
    try x.check()
}

let tss_intersect = endpoint("tss_intersect") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in w(s) }); x.t.formIntersection(o); x.m.formIntersection(o)
    try x.check()
}

let tss_subtract = endpoint("tss_subtract") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = Set((0..<s.range(0, 6)).map { _ in w(s) }); x.t.subtract(o); x.m.subtract(o)
    try x.check()
}

let tss_symdiff = endpoint("tss_symdiff") { (b: Borrow<TSSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 6)).map { _ in w(s) }); x.t.formSymmetricDifference(o); x.m.formSymmetricDifference(o)
    try x.check()
}

let td_new = endpoint("td_new") { () throws -> New<TDo> in
    New(TD())
}

let td_copy = endpoint("td_copy") { (b: Borrow<TDo>) throws -> New<TDo> in
    let c = TD(); c.t = b.value.t; c.m = b.value.m
    return New(c)
}

let td_update_value = endpoint("td_update_value") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s), val = v(s); try check(x.t.updateValue(val, forKey: k) == x.m.updateValue(val, forKey: k), "updateValue")
    try x.check()
}

let td_remove_value = endpoint("td_remove_value") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); try check(x.t.removeValue(forKey: k) == x.m.removeValue(forKey: k), "removeValue")
    try x.check()
}

let td_subscript_set = endpoint("td_subscript_set") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s), val = v(s); x.t[k] = val; x.m[k] = val
    try x.check()
}

let td_subscript_nil = endpoint("td_subscript_nil") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); x.t[k] = nil; x.m[k] = nil
    try x.check()
}

let td_subscript_default = endpoint("td_subscript_default") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = v(s); x.t[k, default: 0] &+= 1; x.m[k, default: 0] &+= 1
    try x.check()
}

let td_merge_old = endpoint("td_merge_old") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = (0..<s.range(0, 5)).map { _ in (v(s), v(s)) }; x.t.merge(pairs) { a, _ in a }; x.m.merge(pairs) { a, _ in a }
    try x.check()
}

let td_merge_new = endpoint("td_merge_new") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = (0..<s.range(0, 5)).map { _ in (v(s), v(s)) }; x.t.merge(pairs) { _, b in b }; x.m.merge(pairs) { _, b in b }
    try x.check()
}

let td_merge_tree = endpoint("td_merge_tree") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeDictionary((0..<s.range(0, 5)).map { _ in (v(s), v(s)) }, uniquingKeysWith: { a, _ in a }); x.t.merge(o) { a, b in a &+ b }; x.m.merge(o.map { ($0.key, $0.value) }) { a, b in a &+ b }
    try x.check()
}

let td_filter = endpoint("td_filter") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.t = x.t.filter { $0.value < t }; x.m = x.m.filter { $0.value < t }
    try x.check()
}

let td_map_values = endpoint("td_map_values") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.t = x.t.mapValues { $0 &* 3 }; x.m = x.m.mapValues { $0 &* 3 }
    try x.check()
}

let td_compact_map_values = endpoint("td_compact_map_values") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.t = x.t.compactMapValues { $0 % 2 == 0 ? $0 : nil }; x.m = x.m.compactMapValues { $0 % 2 == 0 ? $0 : nil }
    try x.check()
}

let td_remove_at_index = endpoint("td_remove_at_index") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.t[7] = 7; x.m[7] = 7 }; let k = s.range(0, x.m.count - 1); let i = x.t.index(x.t.startIndex, offsetBy: k); let e = x.t.remove(at: i); try check(x.m.removeValue(forKey: e.key) == e.value, "remove(at:) removes an entry")
    try x.check()
}

let td_index_for_key = endpoint("td_index_for_key") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    for k in x.m.keys { if let i = x.t.index(forKey: k) { try check(x.t[i].key == k, "index(forKey:)") } else { try check(false, "index(forKey:) finds every key") } }
    try x.check()
}

let td_keys_set_ops = endpoint("td_keys_set_ops") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let o = TreeSet((0..<s.range(0, 6)).map { _ in v(s) }); try check(Set(x.t.keys.intersection(o)) == Set(x.m.keys).intersection(o), "keys.intersection"); try check(Set(x.t.keys.subtracting(o)) == Set(x.m.keys).subtracting(o), "keys.subtracting")
    try x.check()
}

let td_copy_on_write = endpoint("td_copy_on_write") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    var c = x.t; c[999] = 1; c = [:]; try check(x.t.count == x.m.count, "a copy's mutation does not change the original")
    try x.check()
}

let td_grouping = endpoint("td_grouping") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let a = (0..<s.range(0, 10)).map { _ in v(s) }; let g = TreeDictionary(grouping: a) { $0 % 4 }; try check(g.values.reduce(0) { $0 + $1.count } == a.count, "grouping keeps every element")
    try x.check()
}

let td_unique_keys_init = endpoint("td_unique_keys_init") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = Array(Set((0..<s.range(0, 8)).map { _ in v(s) })).map { ($0, $0 &* 2) }; x.t = TreeDictionary(uniqueKeysWithValues: pairs); x.m = Dictionary(uniqueKeysWithValues: pairs)
    try x.check()
}

let td_values_mutate = endpoint("td_values_mutate") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    if x.m.isEmpty { x.t[7] = 7; x.m[7] = 7 }; let i = x.t.index(x.t.startIndex, offsetBy: s.range(0, x.m.count - 1)); let k = x.t[i].key; x.t[k] = 5; x.m[k] = 5
    try x.check()
}

let td_remove_all = endpoint("td_remove_all") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    x.t = [:]; x.m.removeAll()
    try x.check()
}

let td_description = endpoint("td_description") { (b: Borrow<TDo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    _ = x.t.description; _ = x.t.debugDescription
    try x.check()
}

let tds_new = endpoint("tds_new") { () throws -> New<TDSo> in
    New(TDS())
}

let tds_copy = endpoint("tds_copy") { (b: Borrow<TDSo>) throws -> New<TDSo> in
    let c = TDS(); c.t = b.value.t; c.m = b.value.m
    return New(c)
}

let tds_update_value = endpoint("tds_update_value") { (b: Borrow<TDSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = w(s), val = v(s); try check(x.t.updateValue(val, forKey: k) == x.m.updateValue(val, forKey: k), "updateValue")
    try x.check()
}

let tds_remove_value = endpoint("tds_remove_value") { (b: Borrow<TDSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let k = w(s); try check(x.t.removeValue(forKey: k) == x.m.removeValue(forKey: k), "removeValue")
    try x.check()
}

let tds_merge = endpoint("tds_merge") { (b: Borrow<TDSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let pairs = (0..<s.range(0, 5)).map { _ in (w(s), v(s)) }; x.t.merge(pairs) { _, b in b }; x.m.merge(pairs) { _, b in b }
    try x.check()
}

let tds_filter = endpoint("tds_filter") { (b: Borrow<TDSo>, c: Choices<32>) throws in
    let x = b.value
    let s = c.source
    let t = v(s); x.t = x.t.filter { $0.value < t }; x.m = x.m.filter { $0.value < t }
    try x.check()
}

let dq_append_other_with = endpoint("dq_append_other_with") { (a: Borrow<DQo>, b: Borrow<DQo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.d.append(contentsOf: y.d); x.m += y.m
    try x.check()
    try y.check()
}

let dq_prepend_other_with = endpoint("dq_prepend_other_with") { (a: Borrow<DQo>, b: Borrow<DQo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.d.prepend(contentsOf: y.d); x.m.insert(contentsOf: y.m, at: 0)
    try x.check()
    try y.check()
}

let dq_insert_other_slice_with = endpoint("dq_insert_other_slice_with") { (a: Borrow<DQo>, b: Borrow<DQo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let lo = s.range(0, y.m.count); let hi = s.range(lo, y.m.count); let i = s.range(0, x.m.count); x.d.insert(contentsOf: y.d[lo..<hi], at: i); x.m.insert(contentsOf: y.m[lo..<hi], at: i)
    try x.check()
    try y.check()
}

let dq_equal_with = endpoint("dq_equal_with") { (a: Borrow<DQo>, b: Borrow<DQo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.d == y.d) == (x.m == y.m), "== matches the model"); try check(x.d.elementsEqual(y.d) == (x.m == y.m), "elementsEqual")
    try x.check()
    try y.check()
}

let dq_assign_with = endpoint("dq_assign_with") { (a: Borrow<DQo>, b: Borrow<DQo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.d = y.d; x.m = y.m; x.d.append(1); x.m.append(1); try check(Array(y.d) == y.m, "assignment copies")
    try x.check()
    try y.check()
}

let os_union_with = endpoint("os_union_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.formUnion(y.s); for a in y.m { _ = x.add(a) }
    try x.check()
    try y.check()
}

let os_intersection_with = endpoint("os_intersection_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.formIntersection(y.s); x.m = x.m.filter { y.m.contains($0) }
    try x.check()
    try y.check()
}

let os_subtract_with = endpoint("os_subtract_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.subtract(y.s); x.m = x.m.filter { !y.m.contains($0) }
    try x.check()
    try y.check()
}

let os_symdiff_with = endpoint("os_symdiff_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.formSymmetricDifference(y.s); let keep = x.m.filter { !y.m.contains($0) }; let add = y.m.filter { !x.m.contains($0) }; x.m = keep + add
    try x.check()
    try y.check()
}

let os_union_unordered_with = endpoint("os_union_unordered_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.formUnion(y.s.unordered); for a in y.m { _ = x.add(a) }
    try x.check()
    try y.check()
}

let os_relations_with = endpoint("os_relations_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let a = Set(x.m), b = Set(y.m); try check(x.s.isSubset(of: y.s) == a.isSubset(of: b) && x.s.isSuperset(of: y.s) == a.isSuperset(of: b) && x.s.isDisjoint(with: y.s) == a.isDisjoint(with: b) && x.s.isStrictSubset(of: y.s) == a.isStrictSubset(of: b) && x.s.isEqualSet(to: y.s) == (a == b), "set relations")
    try x.check()
    try y.check()
}

let os_equal_with = endpoint("os_equal_with") { (a: Borrow<OSo>, b: Borrow<OSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.s == y.s) == (x.m == y.m), "== is order-sensitive"); try check((x.s.unordered == y.s.unordered) == (Set(x.m) == Set(y.m)), "unordered == is not")
    try x.check()
    try y.check()
}

let od_merge_with = endpoint("od_merge_with") { (a: Borrow<ODo>, b: Borrow<ODo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.d.merge(y.d) { a, b in a &+ b }; for k in y.k { x.set(k, (x.vals[k] ?? 0) &+ y.vals[k]!) }
    try x.check()
    try y.check()
}

let od_merge_keep_with = endpoint("od_merge_keep_with") { (a: Borrow<ODo>, b: Borrow<ODo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.d.merge(y.d) { a, _ in a }; for k in y.k where x.vals[k] == nil { x.set(k, y.vals[k]!) }
    try x.check()
    try y.check()
}

let od_equal_with = endpoint("od_equal_with") { (a: Borrow<ODo>, b: Borrow<ODo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let same = x.k == y.k && x.k.allSatisfy { x.vals[$0] == y.vals[$0] }; try check((x.d == y.d) == same, "== matches the model")
    try x.check()
    try y.check()
}

let hp_insert_other_with = endpoint("hp_insert_other_with") { (a: Borrow<HPo>, b: Borrow<HPo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.h.insert(contentsOf: y.h.unordered); x.m += y.m
    try x.check()
    try y.check()
}

let bs_union_with = endpoint("bs_union_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.b.formUnion(y.b); x.m.formUnion(y.m)
    try x.check()
    try y.check()
}

let bs_intersection_with = endpoint("bs_intersection_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.b.formIntersection(y.b); x.m.formIntersection(y.m)
    try x.check()
    try y.check()
}

let bs_subtract_with = endpoint("bs_subtract_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.b.subtract(y.b); x.m.subtract(y.m)
    try x.check()
    try y.check()
}

let bs_symdiff_with = endpoint("bs_symdiff_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.b.formSymmetricDifference(y.b); x.m.formSymmetricDifference(y.m)
    try x.check()
    try y.check()
}

let bs_relations_with = endpoint("bs_relations_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check(x.b.isSubset(of: y.b) == x.m.isSubset(of: y.m) && x.b.isSuperset(of: y.b) == x.m.isSuperset(of: y.m) && x.b.isDisjoint(with: y.b) == x.m.isDisjoint(with: y.m) && x.b.isStrictSubset(of: y.b) == x.m.isStrictSubset(of: y.m) && x.b.isStrictSuperset(of: y.b) == x.m.isStrictSuperset(of: y.m), "set relations")
    try x.check()
    try y.check()
}

let bs_equal_with = endpoint("bs_equal_with") { (a: Borrow<BSo>, b: Borrow<BSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.b == y.b) == (x.m == y.m), "==")
    try x.check()
    try y.check()
}

let ba_append_other_with = endpoint("ba_append_other_with") { (a: Borrow<BAo>, b: Borrow<BAo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.b.append(contentsOf: y.b); x.m += y.m
    try x.check()
    try y.check()
}

let ba_insert_other_slice_with = endpoint("ba_insert_other_slice_with") { (a: Borrow<BAo>, b: Borrow<BAo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let lo = s.range(0, y.m.count); let hi = s.range(lo, y.m.count); let i = s.range(0, x.m.count); x.b.insert(contentsOf: y.b[lo..<hi], at: i); x.m.insert(contentsOf: y.m[lo..<hi], at: i)
    try x.check()
    try y.check()
}

let ba_equal_with = endpoint("ba_equal_with") { (a: Borrow<BAo>, b: Borrow<BAo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.b == y.b) == (x.m == y.m), "==")
    try x.check()
    try y.check()
}

let ts_union_with = endpoint("ts_union_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formUnion(y.t); x.m.formUnion(y.m)
    try x.check()
    try y.check()
}

let ts_intersection_with = endpoint("ts_intersection_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formIntersection(y.t); x.m.formIntersection(y.m)
    try x.check()
    try y.check()
}

let ts_subtract_with = endpoint("ts_subtract_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.subtract(y.t); x.m.subtract(y.m)
    try x.check()
    try y.check()
}

let ts_symdiff_with = endpoint("ts_symdiff_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formSymmetricDifference(y.t); x.m.formSymmetricDifference(y.m)
    try x.check()
    try y.check()
}

let ts_relations_with = endpoint("ts_relations_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check(x.t.isSubset(of: y.t) == x.m.isSubset(of: y.m) && x.t.isSuperset(of: y.t) == x.m.isSuperset(of: y.m) && x.t.isDisjoint(with: y.t) == x.m.isDisjoint(with: y.m) && x.t.isStrictSubset(of: y.t) == x.m.isStrictSubset(of: y.m) && x.t.isStrictSuperset(of: y.t) == x.m.isStrictSuperset(of: y.m) && x.t.isEqualSet(to: y.t) == (x.m == y.m), "set relations")
    try x.check()
    try y.check()
}

let ts_equal_with = endpoint("ts_equal_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.t == y.t) == (x.m == y.m), "=="); if x.m == y.m { try check(x.t.hashValue == y.t.hashValue, "equal sets hash equally") }
    try x.check()
    try y.check()
}

let ts_union_keys_with = endpoint("ts_union_keys_with") { (a: Borrow<TSo>, b: Borrow<TSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let d = TreeDictionary(uniqueKeysWithValues: y.m.map { ($0, 0) }); x.t.formUnion(d.keys); x.m.formUnion(y.m)
    try x.check()
    try y.check()
}

let td_merge_with = endpoint("td_merge_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.merge(y.t) { a, b in a &+ b }; x.m.merge(y.m) { a, b in a &+ b }
    try x.check()
    try y.check()
}

let td_merge_keep_with = endpoint("td_merge_keep_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.merge(y.t) { a, _ in a }; x.m.merge(y.m) { a, _ in a }
    try x.check()
    try y.check()
}

let td_equal_with = endpoint("td_equal_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.t == y.t) == (x.m == y.m), "=="); if x.m == y.m { try check(x.t.hashValue == y.t.hashValue, "equal dictionaries hash equally") }
    try x.check()
    try y.check()
}

let td_keys_intersection_with = endpoint("td_keys_intersection_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let r = x.t.keys.intersection(y.t.keys); try check(Set(r) == Set(x.m.keys).intersection(y.m.keys), "keys intersection")
    try x.check()
    try y.check()
}

let td_keys_subtracting_with = endpoint("td_keys_subtracting_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    let r = x.t.keys.subtracting(y.t.keys); try check(Set(r) == Set(x.m.keys).subtracting(y.m.keys), "keys subtracting")
    try x.check()
    try y.check()
}

let td_keys_equal_with = endpoint("td_keys_equal_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.t.keys == y.t.keys) == (Set(x.m.keys) == Set(y.m.keys)), "keys ==")
    try x.check()
    try y.check()
}

let td_filter_by_other_with = endpoint("td_filter_by_other_with") { (a: Borrow<TDo>, b: Borrow<TDo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t = x.t.filter { y.t[$0.key] == nil }; x.m = x.m.filter { y.m[$0.key] == nil }
    try x.check()
    try y.check()
}

let tds_merge_with = endpoint("tds_merge_with") { (a: Borrow<TDSo>, b: Borrow<TDSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.merge(y.t) { _, b in b }; x.m.merge(y.m) { _, b in b }
    try x.check()
    try y.check()
}

let tds_equal_with = endpoint("tds_equal_with") { (a: Borrow<TDSo>, b: Borrow<TDSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check((x.t == y.t) == (x.m == y.m), "==")
    try x.check()
    try y.check()
}

let tss_union_with = endpoint("tss_union_with") { (a: Borrow<TSSo>, b: Borrow<TSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formUnion(y.t); x.m.formUnion(y.m)
    try x.check()
    try y.check()
}

let tss_intersection_with = endpoint("tss_intersection_with") { (a: Borrow<TSSo>, b: Borrow<TSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formIntersection(y.t); x.m.formIntersection(y.m)
    try x.check()
    try y.check()
}

let tss_symdiff_with = endpoint("tss_symdiff_with") { (a: Borrow<TSSo>, b: Borrow<TSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.t.formSymmetricDifference(y.t); x.m.formSymmetricDifference(y.m)
    try x.check()
    try y.check()
}

let tss_relations_with = endpoint("tss_relations_with") { (a: Borrow<TSSo>, b: Borrow<TSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    try check(x.t.isSubset(of: y.t) == x.m.isSubset(of: y.m) && x.t.isDisjoint(with: y.t) == x.m.isDisjoint(with: y.m), "set relations")
    try x.check()
    try y.check()
}

let oss_union_with = endpoint("oss_union_with") { (a: Borrow<OSSo>, b: Borrow<OSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.formUnion(y.s); for a in y.m where !x.m.contains(a) { x.m.append(a) }
    try x.check()
    try y.check()
}

let oss_subtract_with = endpoint("oss_subtract_with") { (a: Borrow<OSSo>, b: Borrow<OSSo>, c: Choices<16>) throws in
    let x = a.value, y = a.value === b.value ? clone(b.value) : b.value
    let s = c.source
    _ = s
    x.s.subtract(y.s); x.m = x.m.filter { !y.m.contains($0) }
    try x.check()
    try y.check()
}

let ts_vs_os = endpoint("ts_vs_os") { (a: Borrow<TSo>, b: Borrow<OSo>) throws in
    try check(Set(a.value.t).isSubset(of: Set(b.value.s)) == a.value.m.isSubset(of: Set(b.value.m)), "TreeSet and OrderedSet agree on isSubset")
}

let bs_vs_ts = endpoint("bs_vs_ts") { (a: Borrow<BSo>, b: Borrow<TSo>) throws in
    try check(a.value.b.isSubset(of: b.value.t.filter { $0 >= 0 }) == a.value.m.isSubset(of: b.value.m.filter { $0 >= 0 }), "BitSet and TreeSet agree on isSubset")
}
