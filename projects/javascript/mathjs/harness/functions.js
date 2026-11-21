// One stub per mathjs function: every typed function of the library, with as many value
// arguments as its shortest signature takes (callbacks and options are supplied where a function
// needs them). Results that are values join the pool.
const { stitch, math, V, BAILS, small, describe, Borrow, New, Src } = require("./common");

// not called through the table: global configuration, factories, expression entry points
// (expressions.js), randomness (seeded separately), and createUnit (changes global state)
const SKIP = new Set(["typed", "chain", "parser", "help", "createUnit", "parse", "evaluate", "compile", "simplify", "rationalize", "derivative",
  "random", "randomInt", "pickRandom", "print", "import", "config", "create", "factory", "reviver", "replacer"]);
const CALLBACK = {
  map: (x) => [x, (v) => (math.isNumeric(v) ? math.multiply(v, 2) : v)],
  forEach: (x) => [x, () => {}],
  filter: (x) => [x, (v) => (typeof v === "number" ? v > 0 : true)],
  apply: (x) => [x, 0, (row) => math.sum(row)],
  matrixFromFunction: (x) => [math.isMatrix(x) ? x.size() : [2, 2], (i) => i[0] + 2 * (i[1] || 0)],
  sort: (x) => [x],
  partitionSelect: (x) => [x, 0],
};

const fns = Object.keys(math).filter((k) => typeof math[k] === "function" && math[k].signatures && !SKIP.has(k)).sort();
for (const f of fns) {
  const sigs = Object.keys(math[f].signatures);
  const arity = Math.min(...sigs.map((x) => (x === "" ? 0 : x.split(",").length)));
  const spec = {};
  for (let i = 0; i < Math.min(arity, 3); i++) spec[`x${i}`] = Borrow(V);
  stitch.endpoint(f, spec, New(V).optional(), (args) => {
    let xs = Object.keys(spec).map((k) => args[k]);
    xs.forEach(small);
    if (CALLBACK[f]) xs = CALLBACK[f](xs[0]);
    const out = math[f](...xs);
    if (out === undefined || out === null || typeof out === "function") return null;
    if (typeof out === "object" && !math.isMatrix(out) && !Array.isArray(out) && !math.isBigNumber(out) && !math.isFraction(out) && !math.isComplex(out) && !math.isUnit(out) && !math.isIndex(out) && !math.isRange(out)) return null;
    return out;
  }, { bailsOn: BAILS });
}

// seeded randomness: same seed, same numbers
stitch.endpoint("random_seeded", { seed: stitch.Range(0, 1000), n: stitch.Range(0, 5) }, null, ({ seed, n }) => {
  const a = math.create(math.all, { randomSeed: String(seed) });
  const b = math.create(math.all, { randomSeed: String(seed) });
  const xa = [a.random(), a.randomInt(0, 100), a.pickRandom([1, 2, 3]), a.random([n, 2])];
  const xb = [b.random(), b.randomInt(0, 100), b.pickRandom([1, 2, 3]), b.random([n, 2])];
  stitch.check(JSON.stringify(xa) === JSON.stringify(xb), "the same randomSeed gives the same numbers");
}, { bailsOn: BAILS });
