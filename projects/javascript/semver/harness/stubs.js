// semver: versions, comparators, ranges and comparisons (npm's semver).
// Oracles: parse(format(v)) == v and the parse/valid/clean/accessor functions agree; the compare
// family (compare, rcompare, compareBuild, gt/gte/lt/lte/eq/neq, cmp, compareIdentifiers) is
// antisymmetric and consistent; sort/rsort order by compareBuild; a version that satisfies a range
// is >= minVersion(range); re-parsing a range's normalized text keeps its membership; subset(a, b)
// means every version in a is in b; intersects() is symmetric and true when a version is in both;
// gtr/ltr exclude satisfies. Slow parsing shows up as timeouts.
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Bool, Range } = stitch;
const semver = require("semver");

const Ver = stitch.object("Ver");
const Rng = stitch.object("Rng");
const Cmp = stitch.object("Cmp");
const VersionText = stitch.datatype("VersionText");
const RangeText = stitch.datatype("RangeText");

const num = (s) => String(s.pick([0, 1, 2, 9, 10, 99, s.range(0, 100000)]));
const version = (s) => {
  let v = `${num(s)}.${num(s)}.${num(s)}`;
  if (s.coin(0.3)) v += "-" + s.pick(["alpha", "beta.1", "0", "rc.2", "x-y", s.token()]);
  if (s.coin(0.1)) v += "+" + s.pick(["build", "001", "sha.5114f85"]);
  return (s.coin(0.1) ? s.pick(["v", "=", " "]) : "") + v;
};
stitch.decoder(VersionText, (s) => (s.coin(0.8) ? version(s) : s.token() + s.str(6)));

stitch.decoder(RangeText, (s) => {
  // rarely, a long run of separators (stress for the range regexes)
  const space = () => (s.coin(0.004) ? " ".repeat(s.range(1, 20000)) : s.pick([" ", "", "  ", "\t"]));
  const comparator = () => {
    const k = s.choice(5);
    if (k === 0) return s.pick(["^", "~", ">=", "<", "<=", ">", "=", "", "~>"]) + space() + version(s);
    if (k === 1) return `${version(s)}${space()}-${space()}${version(s)}`;
    if (k === 2) return s.pick(["*", "x", "1.x", "1.2.x", "^1", "~0.2", ">=1.0.0 <2.0.0-0"]);
    if (k === 3) return s.token();
    return s.str(6);
  };
  const sets = [];
  do {
    const cs = [comparator()];
    while (s.coin(0.4)) cs.push(comparator());
    sets.push(cs.join(space()));
  } while (s.more() && sets.length < 6);
  return sets.join(`${space()}||${space()}`);
});

const OPTS = [{}, { loose: true }, { includePrerelease: true }, { loose: true, includePrerelease: true }];
const optsOf = (i) => OPTS[i];
const O = OneOf(0, 1, 2, 3);
const invalid = (e) => e instanceof TypeError && /^Invalid |Version is longer|is not a valid/i.test(e.message);
// SemVer#inc rejects bad identifiers and impossible increments with plain Errors (semver.inc()
// turns them into null)
const badIncrement = (e) => e instanceof Error && /^invalid increment argument|^invalid identifier|is not a prerelease/.test(e.message);
const sign = (x) => (x > 0 ? 1 : x < 0 ? -1 : 0);
const withBuild = (v) => v.version + (v.build.length ? "+" + v.build.join(".") : "");
const safe = (v) => [v.major, v.minor, v.patch].every((n) => n < Number.MAX_SAFE_INTEGER);

stitch.endpoint("parse", { text: Str(128, VersionText), o: O }, New(Ver), ({ text, o }) => {
  const opts = optsOf(o);
  const v = new semver.SemVer(text, opts);
  const again = new semver.SemVer(v.format(), opts);
  stitch.check(semver.eq(v, again) && again.format() === v.format(), "parse(format(v)) == v");
  stitch.check(String(v) === v.version, "toString() == version");
  const p = semver.parse(text, opts);
  stitch.check(p !== null && p.version === v.version, "parse() agrees with new SemVer()");
  stitch.check(semver.valid(text, opts) === v.version, "valid() returns the parsed version");
  stitch.check(semver.major(text, opts) === v.major && semver.minor(text, opts) === v.minor && semver.patch(text, opts) === v.patch,
    "major/minor/patch() agree");
  const pre = semver.prerelease(text, opts);
  stitch.check(v.prerelease.length ? JSON.stringify(pre) === JSON.stringify(v.prerelease) : pre === null, "prerelease() agrees");
  return v;
}, { bailsOn: [invalid] });

stitch.endpoint("parse_lenient", { text: Str(128, VersionText), o: O }, New(Ver).optional(), ({ text, o }) => {
  // the functions that return null instead of throwing
  const opts = optsOf(o);
  const p = semver.parse(text, opts);
  stitch.check((p === null) === (semver.valid(text, opts) === null), "parse() and valid() agree on validity");
  const c = semver.clean(text, opts);
  if (c !== null) stitch.check(semver.valid(c, opts) === c, "clean() returns a valid version");
  return p;
});

stitch.endpoint("reparse", { v: Borrow(Ver), o: O }, New(Ver), ({ v, o }) => {
  const w = new semver.SemVer(v, optsOf(o));
  stitch.check(semver.eq(v, w), "new SemVer(v, other options) == v");
  return w;
}, { bailsOn: [invalid] });

stitch.endpoint("coerce", { text: Str(64, VersionText), rtl: Bool, o: O }, New(Ver), ({ text, rtl, o }) => {
  const v = semver.coerce(text, { rtl, ...optsOf(o) });
  stitch.require(v, "nothing to coerce");
  stitch.check(semver.valid(v.version) === v.version, "coerce() returns a valid version");
  return v;
});

const RELEASE = OneOf("major", "minor", "patch", "premajor", "preminor", "prepatch", "prerelease", "release");
const ID = OneOf(undefined, "alpha", "beta", "0", "alpha.1", "");
const BASE = OneOf(undefined, "0", "1", false);

stitch.endpoint("inc", { v: Borrow(Ver), release: RELEASE, id: ID, base: BASE }, New(Ver), ({ v, release, id, base }) => {
  stitch.require(safe(v), "component at Number.MAX_SAFE_INTEGER");
  // inc(version, release, options, identifier, identifierBase): options shifts out only when it is a string
  const r = semver.inc(v.version, release, {}, id, base);
  stitch.require(r, "inc failed");
  // switching the prerelease identifier may go down (1.2.3-beta -> 1.2.3-alpha.0): by design
  const switched = release.startsWith("pre") && id && v.prerelease.length > 0 && v.prerelease[0] !== id;
  if (!switched) stitch.check(semver.gt(r, v.version), `inc(${release}) is greater`);
  return new semver.SemVer(r);
});

stitch.endpoint("inc_in_place", { v: Borrow(Ver), release: RELEASE, id: ID, base: BASE }, null, ({ v, release, id, base }) => {
  stitch.require(safe(v), "component at Number.MAX_SAFE_INTEGER");
  const before = v.version;
  v.inc(release, id, base);
  stitch.check(v.version === v.format(), "inc() leaves version == format()");
  const again = new semver.SemVer(v.version, v.options);
  stitch.check(again.version === v.version, "the incremented version parses back");
  const switched = release.startsWith("pre") && id && semver.prerelease(before) && semver.prerelease(before)[0] !== id;
  if (!switched) stitch.check(semver.gt(v.version, before, v.options), `SemVer#inc(${release}) is greater`);
}, { bailsOn: [badIncrement] });

stitch.endpoint("truncate", { v: Borrow(Ver), type: OneOf("major", "minor", "patch", "premajor", "preminor", "prepatch", "prerelease", "build", "") },
  null, ({ v, type }) => {
    stitch.require(typeof semver.truncate === "function", "no truncate() in this version");
    const r = semver.truncate(v, type, v.options);
    if (!semver.RELEASE_TYPES.includes(type)) return stitch.check(r === null, "truncate() to an unknown type is null");
    stitch.check(r !== null && semver.valid(r) === r, "truncate() returns a valid version");
    if (type.startsWith("pre")) return stitch.check(r === v.version, "truncate(pre*) keeps the version");
    const t = semver.parse(r);
    stitch.check(t.prerelease.length === 0 && t.major === v.major, "truncate() drops the prerelease and keeps major");
    if (type !== "major") stitch.check(t.minor === v.minor, "truncate(minor/patch) keeps minor");
    if (type === "patch") stitch.check(t.patch === v.patch, "truncate(patch) keeps patch");
  });

stitch.endpoint("compare", { a: Borrow(Ver), b: Borrow(Ver) }, null, ({ a, b }) => {
  const c = semver.compare(a, b);
  stitch.check(c === -semver.compare(b, a), "compare is antisymmetric");
  stitch.check(semver.rcompare(a, b) === semver.compare(b, a), "rcompare(a, b) == compare(b, a)");
  stitch.check((c === 0) === semver.eq(a, b) && (c > 0) === semver.gt(a, b), "compare agrees with eq/gt");
  stitch.check(semver.neq(a, b) === !semver.eq(a, b) && semver.lt(a, b) === semver.gt(b, a), "neq/lt agree");
  stitch.check(semver.gte(a, b) === (c >= 0) && semver.lte(a, b) === (c <= 0), "gte/lte agree with compare");
  const ops = { "": c === 0, "=": c === 0, "==": c === 0, "!=": c !== 0, ">": c > 0, ">=": c >= 0, "<": c < 0, "<=": c <= 0 };
  for (const [op, want] of Object.entries(ops)) stitch.check(semver.cmp(a, op, b) === want, `cmp(a, "${op}", b) agrees with compare`);
  stitch.check(semver.cmp(a, "===", b) === (a.version === b.version), "cmp === compares the version strings");
  stitch.check(c === 0 || c === sign(a.compareMain(b) || a.comparePre(b)), "compare == compareMain || comparePre");
  stitch.check(a.compareMain(b) === -b.compareMain(a) && a.comparePre(b) === -b.comparePre(a), "compareMain/comparePre are antisymmetric");
  // (loose: a loosely parsed version may print as a strictly invalid one)
  const cb = semver.compareBuild(withBuild(a), withBuild(b), true);
  stitch.check(cb === -semver.compareBuild(withBuild(b), withBuild(a), true), "compareBuild is antisymmetric");
  stitch.check(c === 0 || cb === c, "compareBuild agrees with compare when the versions differ");
  stitch.check(semver.compareLoose(a.version, b.version) === c, "compareLoose agrees with compare on valid versions");
  const d = semver.diff(a, b);
  stitch.check((d === null) === semver.eq(a, b), "diff is null iff equal");
  stitch.check(d === semver.diff(b, a), "diff is symmetric");
  stitch.check(d === null || semver.RELEASE_TYPES.includes(d), "diff returns a release type");
});

stitch.endpoint("compare_identifiers", { a: Borrow(Ver), b: Borrow(Ver) }, null, ({ a, b }) => {
  const ids = [...a.prerelease, ...b.prerelease, ...a.build, ...b.build, a.major, b.minor];
  for (const x of ids) {
    for (const y of ids) {
      const c = semver.compareIdentifiers(x, y);
      stitch.check(c === -semver.compareIdentifiers(y, x), "compareIdentifiers is antisymmetric");
      stitch.check(semver.rcompareIdentifiers(x, y) === -c, "rcompareIdentifiers reverses");
    }
  }
});

stitch.endpoint("sort", { a: Borrow(Ver), b: Borrow(Ver), c: Borrow(Ver) }, null, ({ a, b, c }) => {
  const list = [a, b, c, a].map(withBuild);
  const up = semver.sort(list.slice(), true);
  const down = semver.rsort(list.slice(), true);
  for (let i = 0; i + 1 < up.length; i++) {
    stitch.check(semver.compareBuild(up[i], up[i + 1], true) <= 0, "sort() is ascending by compareBuild");
    stitch.check(semver.compareBuild(down[i], down[i + 1], true) >= 0, "rsort() is descending by compareBuild");
  }
});

stitch.endpoint("range", { text: Str(65536, RangeText), o: O }, New(Rng), ({ text, o }) => {
  const r = new semver.Range(text, optsOf(o));
  stitch.check(r.format() === r.range && String(r) === r.range, "format() and toString() return range");
  return r;
}, { bailsOn: [invalid] });

stitch.endpoint("valid_range", { text: Str(65536, RangeText), o: O }, null, ({ text, o }) => {
  const v = semver.validRange(text, optsOf(o));
  if (v !== null) semver.toComparators(text, optsOf(o));
});

// A Range keeps the options it was parsed with; functions given a Range and other options parse
// its text again (and a loose range may not parse strictly). So every call passes r.options.
stitch.endpoint("satisfies", { v: Borrow(Ver), r: Borrow(Rng) }, null, ({ v, r }) => {
  const sat = semver.satisfies(v, r, r.options);
  stitch.check(r.test(v) === sat, "Range#test agrees with satisfies()");
  if (sat) {
    const min = semver.minVersion(r, r.options);
    stitch.check(min !== null && semver.lte(min, v), "satisfying version >= minVersion(range)");
  }
});

stitch.endpoint("range_reparse", { r: Borrow(Rng), v: Borrow(Ver) }, null, ({ r, v }) => {
  // the normalized text, and the comparators, describe the same set of versions
  const again = new semver.Range(r.range || "*", r.options);
  stitch.check(again.test(v) === r.test(v), "new Range(range.range) has the same members");
  const comps = semver.toComparators(r, r.options);
  const fromComps = new semver.Range(comps.map((c) => c.join(" ")).join(" || ") || "*", r.options);
  stitch.check(fromComps.test(v) === r.test(v), "toComparators(range) has the same members");
});

stitch.endpoint("min_version", { r: Borrow(Rng) }, New(Ver), ({ r }) => {
  const m = semver.minVersion(r, r.options);
  stitch.require(m, "no minimum");
  stitch.check(semver.satisfies(m, r, { ...r.options, includePrerelease: true }), "minVersion(range) satisfies range");
  return m;
});

stitch.endpoint("subset", { a: Borrow(Rng), b: Borrow(Rng), v: Borrow(Ver) }, null, ({ a, b, v }) => {
  stitch.require(a.loose === b.loose && a.includePrerelease === b.includePrerelease, "ranges parsed with different options");
  const o = a.options;
  const inA = semver.satisfies(v, a, o), inB = semver.satisfies(v, b, o);
  if (semver.subset(a, b, o) && inA) stitch.check(inB, "subset(a, b): v in a is in b");
  const x = semver.intersects(a, b, o);
  if (!x) stitch.check(!(inA && inB), "intersects() false but v in both");
  stitch.check(a.intersects(b, o) === x, "Range#intersects agrees with intersects()");
  stitch.check(semver.intersects(b, a, o) === x, "intersects() is symmetric");
});

stitch.endpoint("outside", { v: Borrow(Ver), r: Borrow(Rng), a: Borrow(Ver), b: Borrow(Ver) }, null, ({ v, r, a, b }) => {
  const o = r.options;
  const g = semver.gtr(v, r, o), l = semver.ltr(v, r, o);
  stitch.check(semver.outside(v, r, ">", o) === g && semver.outside(v, r, "<", o) === l, "outside() agrees with gtr/ltr");
  // (both hold, vacuously, for a range nothing satisfies)
  if (g && l) stitch.check(semver.minVersion(r, o) === null, "not both gtr and ltr");
  if (g || l) stitch.check(!semver.satisfies(v, r, o), "gtr/ltr version does not satisfy");
  for (const w of [a, b]) {
    if (!semver.satisfies(w, r, o)) continue;
    if (g) stitch.check(semver.gt(v, w), "gtr(v, range): v > every version in range");
    if (l) stitch.check(semver.lt(v, w), "ltr(v, range): v < every version in range");
  }
});

stitch.endpoint("max_satisfying", { r: Borrow(Rng), a: Borrow(Ver), b: Borrow(Ver) }, null, ({ r, a, b }) => {
  const m = semver.maxSatisfying([a, b], r, r.options);
  if (m) stitch.check(semver.satisfies(m, r, r.options), "maxSatisfying() satisfies");
  const n = semver.minSatisfying([a, b], r, r.options);
  stitch.check((m === null) === (n === null), "maxSatisfying() and minSatisfying() agree on emptiness");
  if (m && n) stitch.check(semver.lte(n, m), "minSatisfying() <= maxSatisfying()");
});

stitch.endpoint("simplify", { r: Borrow(Rng), a: Borrow(Ver), b: Borrow(Ver) }, null, ({ r, a, b }) => {
  const s = semver.simplifyRange([a.version, b.version], r.raw, r.options);
  for (const v of [a, b]) {
    stitch.check(semver.satisfies(v, s, r.options) === semver.satisfies(v, r, r.options), "simplifyRange keeps membership of the listed versions");
  }
}, { bailsOn: [invalid] });

stitch.endpoint("comparator", { op: OneOf("", "=", ">", ">=", "<", "<=", " >= ", "~"), text: Str(64, VersionText), o: O }, New(Cmp), ({ op, text, o }) => {
  const c = new semver.Comparator(op + text, optsOf(o));
  stitch.check(String(c) === c.value, "toString() == value");
  const again = new semver.Comparator(c.value || "", optsOf(o));
  stitch.check(again.value === c.value, "new Comparator(c.value) == c");
  return c;
}, { bailsOn: [invalid] });

stitch.endpoint("comparator_test", { c: Borrow(Cmp), v: Borrow(Ver) }, null, ({ c, v }) => {
  const t = c.test(v);
  if (semver.valid(v.version, c.options)) stitch.check(c.test(v.version) === t, "Comparator#test agrees on a version string");
  // a Range of one comparator also excludes prereleases from other tuples: compare plain versions
  if (v.prerelease.length === 0) stitch.check(new semver.Range(c.value || "*", c.options).test(v) === t, "Range(c.value) agrees with Comparator#test");
});

stitch.endpoint("comparator_intersects", { a: Borrow(Cmp), b: Borrow(Cmp), v: Borrow(Ver), o: O }, null, ({ a, b, v, o }) => {
  const opts = optsOf(o);
  stitch.require(a.loose === b.loose && a.loose === !!opts.loose, "comparators parsed with other options");
  const x = a.intersects(b, opts);
  stitch.check(b.intersects(a, opts) === x, "Comparator#intersects is symmetric");
  if (v.prerelease.length === 0 && a.test(v) && b.test(v)) stitch.check(x, "Comparator#intersects false but v satisfies both");
});

// new Range(comparator) wraps the comparator without setting the Range's options (so it is not
// handed to the endpoints above, which pass r.options along)
stitch.endpoint("range_of_comparator", { c: Borrow(Cmp), v: Borrow(Ver) }, null, ({ c, v }) => {
  const r = new semver.Range(c, c.options);
  stitch.check(r.range === c.value, "Range(comparator).range == comparator.value");
  const t = r.test(v);
  if (v.prerelease.length === 0) stitch.check(t === c.test(v), "Range(comparator) agrees with Comparator#test");
});
