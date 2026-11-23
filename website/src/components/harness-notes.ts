// Notes on the home page harness: each `at` is text on one line of src/tutorial-output/<lang>/harness.txt
// (the first occurrence is marked). HarnessNotes.astro fails the build if one is missing.

const N = {
  object: "Object type. Store names a kind of value that one call produces and later calls use. The fuzzer passes a Store only where a stub asks for one.",
  decoder: "Decoder for the KvText data type. It builds key=value lines from fuzzer choices, so mutated inputs keep their shape.",
  producer: "A producer: it takes no objects and returns a new Store, so a program can start with it.",
  param: "A fuzzed parameter: text of at most 256 bytes, made by the KvText decoder.",
  borrow: "Borrow: uses a Store from an earlier call. The store stays available for later calls.",
  check: "A check: a property that holds for every store. When it is false, STITCH++ reports a bug.",
};
const bail = (how: string) => `A bail: ${how} means the input was invalid. The call stops without counting as a bug; any other failure is a bug.`;

export type Note = { at: string; note: string };

export const HARNESS_NOTES: Record<string, Note[]> = {
  python: [
    { at: 'Store = stitch.object("Store")', note: N.object },
    { at: "@stitch.decoder(KvText)", note: N.decoder },
    { at: "def new_store() -> New[Store]:", note: N.producer },
    { at: "bails_on=(ValueError,)", note: bail("a ValueError") },
    { at: "text: Str[256, KvText]", note: N.param },
    { at: "st: Borrow[Store]", note: N.borrow },
    { at: "stitch.check(", note: N.check },
  ],
  cpp: [
    { at: "STITCH_OBJECT(Store, kv_store*);", note: N.object },
    { at: "STITCH_DROP(Store,", note: "Frees stores that are left over when a program ends." },
    { at: "STITCH_DECODER(KvText,", note: N.decoder },
    { at: "New<Store> new_store()", note: N.producer },
    { at: "Str<256, KvText> text", note: N.param },
    { at: 'stitch::require(st, "invalid text");', note: bail("a null store from kv_load") },
    { at: "Borrow<Store> st", note: N.borrow },
    { at: "stitch::check(", note: N.check },
  ],
  javascript: [
    { at: 'const Store = stitch.object("Store");', note: N.object },
    { at: "stitch.decoder(KvText,", note: N.decoder },
    { at: 'stitch.endpoint("new_store", {}, New(Store)', note: N.producer },
    { at: "text: Str(256, KvText)", note: N.param },
    { at: "bailsOn: [RangeError]", note: bail("a RangeError") },
    { at: "st: Borrow(Store)", note: N.borrow },
    { at: "stitch.check(", note: N.check },
  ],
  java: [
    { at: '@Decoder("KvText")', note: N.decoder },
    { at: "public static Store newStore()", note: `${N.producer} A parameter or return value of a class type is an object.` },
    { at: '@Str(max = 256, type = "KvText") String text', note: N.param },
    { at: "bailsOn = {IllegalArgumentException.class}", note: bail("an IllegalArgumentException") },
    { at: "void put(Store st", note: N.borrow },
    { at: "Stitch.check(", note: N.check },
  ],
  go: [
    { at: 'stitch.Object[*kvgo.Store]("Store", nil)', note: N.object },
    { at: 'stitch.Decoder("KvText", kvText)', note: N.decoder },
    { at: 'stitch.Endpoint("new_store", kvgo.New)', note: `${N.producer} Here it is the library's own constructor.` },
    { at: '"text:max=256,type=KvText"', note: N.param },
    { at: "string) error", note: bail("a non-nil error") },
    { at: "st *kvgo.Store", note: N.borrow },
    { at: "stitch.Check(", note: N.check },
  ],
  rust: [
    { at: "object!(Store, kvrs::Store);", note: N.object },
    { at: "decoder!(KvText,", note: N.decoder },
    { at: "fn new_store() -> New<Store>", note: N.producer },
    { at: "text: Str<256, KvText>", note: N.param },
    { at: "Result<New<Store>, String>", note: bail("an Err") },
    { at: "st: Borrow<Store>", note: N.borrow },
    { at: "check(again", note: N.check },
  ],
  ruby: [
    { at: 'Store = Stitch.object("Store")', note: N.object },
    { at: "Stitch.decoder(KvText)", note: N.decoder },
    { at: "Stitch.endpoint :new_store, {}, New[Store]", note: N.producer },
    { at: "text: Str[256, KvText]", note: N.param },
    { at: "bails_on: [ArgumentError]", note: bail("an ArgumentError") },
    { at: "st: Borrow[Store]", note: N.borrow },
    { at: "Stitch.check(", note: N.check },
  ],
  swift: [
    { at: "enum Store: StitchObject", note: N.object },
    { at: "enum KvText: Decoded", note: N.decoder },
    { at: 'endpoint("new_store") { () -> New<Store>', note: N.producer },
    { at: "text: Str<256, KvText>", note: N.param },
    { at: "throws -> New<Store>", note: bail("a thrown error") },
    { at: "st: Borrow<Store>", note: N.borrow },
    { at: "try check(", note: N.check },
  ],
  objc: [
    { at: "STITCH_OBJECT(Store, KVStore *);", note: N.object },
    { at: "STITCH_DECODER(KvText)", note: N.decoder },
    { at: "STITCH_ENDPOINT(New(Store), new_store)", note: N.producer },
    { at: "Str(256, KvText) text", note: N.param },
    { at: 'STRequire(st != nil, @"invalid text");', note: bail("a nil store") },
    { at: "Borrow(Store) st", note: N.borrow },
    { at: "STCheck(", note: N.check },
  ],
};
