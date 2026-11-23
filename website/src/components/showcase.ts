// The home page example: the endpoints of the key-value store harness (src/showcase/<lang>/harness.txt),
// their shapes, what to say about each part, and where each part is in each language's file.
import type { ObjType, Shape } from "./glyph";

const S = (name = "st", take = false) => ({ type: "Store" as ObjType, name, take });
const N = (name = "snap", take = false) => ({ type: "Snapshot" as ObjType, name, take });

export type Endpoint = Shape & { id: string; note: string };

export const ENDPOINTS: Endpoint[] = [
  { id: "new_store", in: [], out: [S()], fuzz: [],
    note: "Makes an empty Store. It needs no inputs, so a program can start here." },
  { id: "load", in: [], out: [S()], fuzz: [{ name: "text", decoder: "KvText" }],
    note: "Parses text into a new Store. The text (orange) is fuzzed; the KvText decoder makes it look like key=value lines." },
  { id: "put", in: [S()], out: [S()], fuzz: [{ name: "key" }, { name: "value" }],
    note: "Puts a fuzzed key and value into a Store. It borrows the Store: the same Store comes out and later calls can use it." },
  { id: "remove", in: [S()], out: [S()], fuzz: [{ name: "key" }],
    note: "Removes a fuzzed key from a Store it borrows." },
  { id: "snapshot", in: [S()], out: [S(), N()], fuzz: [],
    note: "Copies a Store into a new Snapshot. One input, two outputs: the Store, still usable, and the Snapshot." },
  { id: "merge", in: [S(), N("snap", true)], out: [S()], fuzz: [],
    note: "Puts a Snapshot's entries back into a Store. It takes two inputs of different types. The Snapshot is taken (hollow): it is used up, so only the Store comes out." },
  { id: "roundtrip", in: [S()], out: [S()], fuzz: [], check: true,
    note: "A check: dumping the Store to text and loading it back must give the same entries. If not, STITCH++ reports a bug." },
];

export const TYPE_NOTES: Record<ObjType, string> = {
  Store: "An object type. A Store is made by one call and passed to later ones; the fuzzer only wires a Store into an input that takes a Store.",
  Snapshot: "A second object type, a copy of a Store. Different types are different colors; wires only join ports of the same color.",
};

export const PART_NOTES = {
  decoder: "A decoder: it turns the fuzzer's random choices into text that looks like key=value lines, so mutated inputs keep that shape.",
  bail: "Invalid input is not a bug. This marks the call as rejected (a bail): the program goes on without its result. Any other error is a bug.",
  check: "The property being tested. When it is false, STITCH++ reports a bug.",
};

type Lang = {
  /** stub names that differ from the endpoint ids */
  names?: Record<string, string>;
  /** text on the signature line of each endpoint (its shape sits beside that line) */
  at: Record<string, string>;
  /** text of each object type's declaration (a type without one is declared by its first use) */
  types: Partial<Record<ObjType, string>>;
  decoder: string;
  bail: string[];
  check: string;
};

export const LANGS: Record<string, Lang> = {
  python: {
    at: { new_store: "def new_store", load: "def load(", put: "def put(", remove: "def remove(",
      snapshot: "def snapshot(", merge: "def merge(", roundtrip: "def roundtrip(" },
    types: { Store: 'Store = stitch.object("Store")', Snapshot: 'Snapshot = stitch.object("Snapshot")' },
    decoder: "@stitch.decoder(KvText)", bail: ["bails_on=(ValueError,)"], check: "stitch.check(",
  },
  cpp: {
    names: { remove: "remove_key" },
    at: { new_store: "New<Store> new_store()", load: "New<Store> load(", put: "void put(", remove: "void remove_key(",
      snapshot: "New<Snapshot> snapshot(", merge: "void merge(", roundtrip: "void roundtrip(" },
    types: { Store: "STITCH_OBJECT(Store, kv_store*);", Snapshot: "STITCH_OBJECT(Snapshot, kv_store*);" },
    decoder: "STITCH_DECODER(KvText,", bail: ["stitch::require("], check: "stitch::check(",
  },
  javascript: {
    at: { new_store: 'stitch.endpoint("new_store"', load: 'stitch.endpoint("load"', put: 'stitch.endpoint("put"',
      remove: 'stitch.endpoint("remove"', snapshot: 'stitch.endpoint("snapshot"', merge: 'stitch.endpoint("merge"',
      roundtrip: 'stitch.endpoint("roundtrip"' },
    types: { Store: 'const Store = stitch.object("Store");', Snapshot: 'const Snapshot = stitch.object("Snapshot");' },
    decoder: "stitch.decoder(KvText,", bail: ["bailsOn: [RangeError]"], check: "stitch.check(",
  },
  java: {
    names: { new_store: "newStore" },
    at: { new_store: "public static Store newStore()", load: "public static Store load(", put: "public static void put(",
      remove: "public static void remove(", snapshot: "public static Store snapshot(", merge: "public static void merge(",
      roundtrip: "public static void roundtrip(" },
    types: { Store: "import demo.kv.Store;", Snapshot: '@Obj("Snapshot")' },
    decoder: '@Decoder("KvText")', bail: ["bailsOn = {IllegalArgumentException.class}"], check: "Stitch.check(",
  },
  go: {
    at: { new_store: "func newStore()", load: "func load(", put: "func put(", remove: "func remove(",
      snapshot: "func snapshot(", merge: "func merge(", roundtrip: "func roundtrip(" },
    types: { Store: 'stitch.Object[*kvgo.Store]("Store", nil)', Snapshot: 'stitch.Object[*Snapshot]("Snapshot", nil)' },
    decoder: 'stitch.Decoder("KvText", kvText)', bail: ["string) error", "(*Snapshot, error)", "stitch.Require("], check: "stitch.Check(",
  },
  rust: {
    at: { new_store: "fn new_store()", load: "fn load(", put: "fn put(", remove: "fn remove(",
      snapshot: "fn snapshot(", merge: "fn merge(", roundtrip: "fn roundtrip(" },
    types: { Store: "object!(Store, kvrs::Store);", Snapshot: "object!(Snapshot, kvrs::Store);" },
    decoder: "decoder!(KvText,", bail: ["Result<New<Store>, String>", "Result<New<Snapshot>, String>", "Result<(), String>", "require(again"], check: "check(again",
  },
  ruby: {
    at: { new_store: "Stitch.endpoint :new_store", load: "Stitch.endpoint :load", put: "Stitch.endpoint :put",
      remove: "Stitch.endpoint :remove", snapshot: "Stitch.endpoint :snapshot", merge: "Stitch.endpoint :merge",
      roundtrip: "Stitch.endpoint :roundtrip" },
    types: { Store: 'Store = Stitch.object("Store")', Snapshot: 'Snapshot = Stitch.object("Snapshot")' },
    decoder: "Stitch.decoder(KvText)", bail: ["bails_on: [ArgumentError]"], check: "Stitch.check(",
  },
  swift: {
    at: { new_store: "let newStore = endpoint", load: "let load = endpoint", put: "let put = endpoint",
      remove: "let remove = endpoint", snapshot: "let snapshot = endpoint", merge: "let merge = endpoint",
      roundtrip: "let roundtrip = endpoint" },
    types: { Store: "enum Store: StitchObject", Snapshot: "enum Snapshot: StitchObject" },
    decoder: "enum KvText: Decoded", bail: ["throws"], check: "try check(",
  },
  objc: {
    at: { new_store: "STITCH_ENDPOINT(New(Store), new_store)", load: "STITCH_ENDPOINT(New(Store), load",
      put: "STITCH_ENDPOINT(void, put", remove: "STITCH_ENDPOINT(void, remove", snapshot: "STITCH_ENDPOINT(New(Snapshot), snapshot",
      merge: "STITCH_ENDPOINT(void, merge", roundtrip: "STITCH_ENDPOINT(void, roundtrip" },
    types: { Store: "STITCH_OBJECT(Store, KVStore *);", Snapshot: "STITCH_OBJECT(Snapshot, KVStore *);" },
    decoder: "STITCH_DECODER(KvText)", bail: ["STRequire("], check: "STCheck(",
  },
};

export const nameOf = (lang: string, id: string) => LANGS[lang]?.names?.[id] ?? id;
export const endpointByName = (lang: string, name: string) =>
  ENDPOINTS.find((e) => nameOf(lang, e.id) === name);
