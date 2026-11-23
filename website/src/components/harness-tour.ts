// A guided tour of the home page harness (src/tutorial-output/<lang>/harness.txt), for readers who
// have never seen one. Each step highlights line ranges and/or phrases:
//   range [from, before, to, after]: from the first line containing `from` (plus `before` lines above)
//     to the first line at or after it matching `to` (plus `after` lines below). A `to` that starts
//     with "=" must equal the whole line.
//   marks: phrases; every occurrence is highlighted and can be clicked to open this step.
// HarnessTour.astro fails the build if an anchor is missing.

export type Range = [from: string, before: number, to: string, after?: number];
export type Step = { title: string; text: string; ranges?: Range[]; marks?: string[] };
type Spec = {
  name: string;
  endpoints: Range[];
  object: string[];
  datatype: Range[];
  producers: string[];
  params: string[];
  borrow: string[];
  bails: string[];
  bailHow: string;
  check: string[];
  objectText?: string;
  borrowText?: string;
};

const SPECS: Record<string, Spec> = {
  python: {
    name: "Python",
    endpoints: [["def new_store", 1, "return kvlib.Store()"], ["def load(", 1, "return kvlib.load(text)"],
      ["def put(", 1, "st.put(key, value)"], ["def remove(", 1, "st.remove(key)"], ["def roundtrip(", 1, "stitch.check("]],
    object: ['Store = stitch.object("Store")'],
    datatype: [['KvText = stitch.datatype("KvText")', 0, "KvText = stitch.datatype"], ["@stitch.decoder(KvText)", 0, ".join(lines)"]],
    producers: ["-> New[Store]"],
    params: ["text: Str[256, KvText]", "key: Str[16]", "value: Str[32]"],
    borrow: ["st: Borrow[Store]"],
    bails: ["bails_on=(ValueError,)"],
    bailHow: "raising a ValueError",
    check: ["stitch.check("],
  },
  cpp: {
    name: "C++",
    endpoints: [["New<Store> new_store()", 0, "STITCH_ENDPOINT(new_store);"], ["New<Store> load(", 0, "STITCH_ENDPOINT(load);"],
      ["void put(", 0, "STITCH_ENDPOINT(put);"], ["void remove_key(", 0, "STITCH_ENDPOINT(remove_key);"],
      ["void roundtrip(", 0, "STITCH_ENDPOINT(roundtrip);"]],
    object: ["STITCH_OBJECT(Store, kv_store*);", "STITCH_DROP("],
    objectText: "Store is a kind of value that endpoints pass to each other, here a kv_store* pointer. An endpoint that returns a Store makes one; an endpoint that takes a Store uses one made earlier. STITCH_DROP frees the stores that are left when a program ends.",
    datatype: [["STITCH_DATATYPE(KvText);", 0, "STITCH_DATATYPE(KvText);"], ["STITCH_DECODER(KvText,", 0, "});"]],
    producers: ["New<Store> new_store()", "New<Store> load"],
    params: ["Str<256, KvText> text", "Str<16> key", "Str<32> value"],
    borrow: ["Borrow<Store> st"],
    bails: ["stitch::require("],
    bailHow: "a failed stitch::require",
    check: ["stitch::check("],
  },
  javascript: {
    name: "JavaScript",
    endpoints: [['stitch.endpoint("new_store"', 0, 'stitch.endpoint("new_store"'], ['stitch.endpoint("load"', 0, "bailsOn: [RangeError] });"],
      ['stitch.endpoint("put"', 0, "}, { bailsOn"], ['stitch.endpoint("remove"', 0, 'stitch.endpoint("remove"'],
      ['stitch.endpoint("roundtrip"', 0, "=});"]],
    object: ['const Store = stitch.object("Store");'],
    datatype: [['const KvText = stitch.datatype("KvText");', 0, "const KvText"], ["stitch.decoder(KvText,", 0, "=});"]],
    producers: ["New(Store)"],
    params: ["text: Str(256, KvText)", "key: Str(16)", "value: Str(32)"],
    borrow: ["st: Borrow(Store)"],
    bails: ["bailsOn: [RangeError]"],
    bailHow: "throwing a RangeError",
    check: ["stitch.check("],
  },
  java: {
    name: "Java",
    endpoints: [["public static Store newStore()", 1, "=    }"], ["public static Store load(", 1, "=    }"],
      ["public static void put(", 1, "=    }"], ["public static void remove(", 1, "=    }"], ["public static void roundtrip(", 1, "=    }"]],
    object: ["import demo.kv.Store;"],
    objectText: "Store, the library's class, is the kind of value that endpoints pass to each other. In Java, a class used as a parameter or return type is an object type: an endpoint that returns a Store makes one, and an endpoint that takes a Store uses one made earlier.",
    datatype: [['@Decoder("KvText")', 0, "=    }"]],
    producers: ["public static Store newStore()", "public static Store load"],
    params: ['@Str(max = 256, type = "KvText") String text', "@Str(max = 16) String key", "@Str(max = 32) String value"],
    borrow: ["Store st"],
    borrowText: "put, remove and roundtrip take a Store made by an earlier call. Taking it this way borrows it: it stays available, so later calls can use the same store.",
    bails: ["bailsOn = {IllegalArgumentException.class}"],
    bailHow: "throwing an IllegalArgumentException",
    check: ["Stitch.check("],
  },
  go: {
    name: "Go",
    endpoints: [['stitch.Endpoint("new_store"', 0, 'stitch.Endpoint("roundtrip"'], ["func put(", 0, "func put("],
      ["func remove(", 0, "func remove("], ["func roundtrip(", 0, "=}"]],
    object: ['stitch.Object[*kvgo.Store]("Store", nil)'],
    datatype: [['stitch.Decoder("KvText"', 0, 'stitch.Decoder("KvText"'], ["func kvText(", 0, "=}"]],
    producers: ['stitch.Endpoint("new_store", kvgo.New)', 'stitch.Endpoint("load", kvgo.Load'],
    params: ['"text:max=256,type=KvText"', '"key:max=16"', '"value:max=32"'],
    borrow: ["st *kvgo.Store"],
    bails: ["string) error", "stitch.Require("],
    bailHow: "returning a non-nil error (or a failed stitch.Require)",
    check: ["stitch.Check("],
  },
  rust: {
    name: "Rust",
    endpoints: [["fn new_store()", 1, "=    }", 1], ["fn load(", 1, "=    }", 1], ["fn put(", 1, "=    }", 1],
      ["fn remove(", 1, "=    }", 1], ["fn roundtrip(", 1, "=    }", 1]],
    object: ["object!(Store, kvrs::Store);"],
    datatype: [["datatype!(KvText);", 0, "datatype!(KvText);"], ["decoder!(KvText,", 0, "=});"]],
    producers: ["-> New<Store>", "New::new("],
    params: ["text: Str<256, KvText>", "key: Str<16>", "value: Str<32>"],
    borrow: ["st: Borrow<Store>"],
    bails: ["Result<New<Store>, String>", "Result<(), String>", "require(again"],
    bailHow: "returning an Err (or a failed require)",
    check: ["check(again"],
  },
  ruby: {
    name: "Ruby",
    endpoints: [["Stitch.endpoint :new_store", 0, "=end"], ["Stitch.endpoint :load", 0, "=end"], ["Stitch.endpoint :put", 0, "=end"],
      ["Stitch.endpoint :remove", 0, "=end"], ["Stitch.endpoint :roundtrip", 0, "=end"]],
    object: ['Store = Stitch.object("Store")'],
    datatype: [['KvText = Stitch.datatype("KvText")', 0, "KvText = Stitch.datatype"], ["Stitch.decoder(KvText)", 0, "=end"]],
    producers: ["New[Store]"],
    params: ["text: Str[256, KvText]", "key: Str[16]", "value: Str[32]"],
    borrow: ["st: Borrow[Store]"],
    bails: ["bails_on: [ArgumentError]"],
    bailHow: "raising an ArgumentError",
    check: ["Stitch.check("],
  },
  swift: {
    name: "Swift",
    endpoints: [["let newStore = endpoint", 0, "=}"], ["let load = endpoint", 0, "=}"], ["let put = endpoint", 0, "=}"],
      ["let remove = endpoint", 0, "=}"], ["let roundtrip = endpoint", 0, "=}"]],
    object: ["enum Store: StitchObject { typealias Handle = KVSwift.Store }"],
    datatype: [["enum KvText: Decoded", 0, "=}"]],
    producers: ["-> New<Store>"],
    params: ["text: Str<256, KvText>", "key: Str<16, Plain>", "value: Str<32, Plain>"],
    borrow: ["st: Borrow<Store>"],
    bails: ["throws"],
    bailHow: "throwing an error",
    check: ["try check("],
  },
  objc: {
    name: "Objective-C",
    endpoints: [["STITCH_ENDPOINT(New(Store), new_store)", 0, "=}"], ["STITCH_ENDPOINT(New(Store), load", 0, "=}"],
      ["STITCH_ENDPOINT(void, put", 0, "=}"], ["STITCH_ENDPOINT(void, remove", 0, "=}"], ["STITCH_ENDPOINT(void, roundtrip", 0, "=}"]],
    object: ["STITCH_OBJECT(Store, KVStore *);"],
    datatype: [["STITCH_DECODER(KvText)", 0, "=}"]],
    producers: ["New(Store)"],
    params: ["Str(256, KvText) text", "Str(16) key", "Str(32) value"],
    borrow: ["Borrow(Store) st"],
    bails: ["STRequire("],
    bailHow: "a failed STRequire",
    check: ["STCheck("],
  },
};

function tour(s: Spec): Step[] {
  return [
    {
      title: "This file is a STITCH++ spec",
      text: `A harness is ordinary ${s.name} code that describes the API you want to fuzz: which calls exist, what each one needs, and what it returns. STITCH++ reads it and builds test programs from it. Step through to see its parts.`,
    },
    {
      title: "Endpoints",
      text: "Each endpoint (a stub) wraps one call into the library: create a store, load one from text, put a key, remove a key, and check a round trip. STITCH++ calls them in many different orders and combinations.",
      ranges: s.endpoints,
    },
    {
      title: "An object type",
      text: s.objectText ?? "Store is a kind of value that endpoints pass to each other. An endpoint that returns a Store makes one; an endpoint that takes a Store uses one made earlier. STITCH++ only connects endpoints whose types match.",
      marks: s.object,
    },
    {
      title: "A custom data type",
      text: "KvText describes the text that load parses: lines of key=value. Its decoder builds such text from the fuzzer's random choices, so inputs keep that shape while the fuzzer mutates them.",
      ranges: s.datatype,
    },
    {
      title: "Making a store",
      text: "new_store and load return a new Store, so a program can start with either of them. Every Store a later call uses comes from one of these two.",
      marks: s.producers,
    },
    {
      title: "Fuzzed arguments",
      text: "Arguments that are not a Store are chosen by the fuzzer. The signature gives each one's kind and size, such as a string of at most 16 bytes; load's text comes from the KvText decoder.",
      marks: s.params,
    },
    {
      title: "Using a store",
      text: s.borrowText ?? "put, remove and roundtrip borrow a Store made by an earlier call. Borrowing leaves it available, so later calls can use the same store.",
      marks: s.borrow,
    },
    {
      title: "Expected errors",
      text: `Invalid input is not a bug. Here, ${s.bailHow} means the input was rejected: the call stops (a bail) and the rest of the program runs without its result. Any other error or crash is reported as a bug.`,
      marks: s.bails,
    },
    {
      title: "A property to test",
      text: "roundtrip dumps the store as text, loads it back, and checks that nothing changed. A check that fails is reported as a bug. This one found a real problem: a key that contains = changes on the way through.",
      marks: s.check,
    },
  ];
}

export const HARNESS_TOUR: Record<string, Step[]> = Object.fromEntries(
  Object.entries(SPECS).map(([lang, spec]) => [lang, tour(spec)]),
);
