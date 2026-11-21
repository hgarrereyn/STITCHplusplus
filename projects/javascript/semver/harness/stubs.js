// semver: versions, ranges and comparisons (npm's semver).
// Oracles: a version that satisfies a range is >= minVersion(range); subset(a, b) means every
// version in a is in b; parse(format(v)) == v. Slow parsing shows up as timeouts.
const stitch = require("stitch");
const { Borrow, New, Str, OneOf, Bool, Src, Range } = stitch;
const semver = require("semver");

const Ver = stitch.object("Ver");
const Rng = stitch.object("Rng");
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
const invalid = (e) => e instanceof TypeError && /^Invalid |Version is longer|is not a valid/.test(e.message);

stitch.endpoint("parse", { text: Str(128, VersionText), o: O }, New(Ver), ({ text, o }) => {
  const v = new semver.SemVer(text, optsOf(o));
  const again = new semver.SemVer(v.format(), optsOf(o));
  stitch.check(semver.eq(v, again) && again.format() === v.format(), "parse(format(v)) == v");
  return v;
}, { bailsOn: [invalid] });

stitch.endpoint("coerce", { text: Str(64, VersionText), rtl: Bool, o: O }, New(Ver), ({ text, rtl, o }) => {
  const v = semver.coerce(text, { rtl, ...optsOf(o) });
  stitch.require(v, "nothing to coerce");
  stitch.check(semver.valid(v.version) === v.version, "coerce() returns a valid version");
  return v;
});

stitch.endpoint("inc", { v: Borrow(Ver), release: OneOf("major", "minor", "patch", "premajor", "preminor", "prepatch", "prerelease"), id: OneOf(undefined, "alpha", "beta", "0") },
  New(Ver), ({ v, release, id }) => {
    const r = semver.inc(v.version, release, id);
    stitch.require(r, "inc failed");
    // switching the prerelease identifier may go down (1.2.3-beta -> 1.2.3-alpha.0): by design
    const switched = release.startsWith("pre") && id !== undefined && v.prerelease.length > 0 && v.prerelease[0] !== id;
    if (!switched) stitch.check(semver.gt(r, v.version), `inc(${release}) is greater`);
    return new semver.SemVer(r);
  });

stitch.endpoint("compare", { a: Borrow(Ver), b: Borrow(Ver) }, null, ({ a, b }) => {
  const c = semver.compare(a, b);
  stitch.check(c === -semver.compare(b, a), "compare is antisymmetric");
  stitch.check((c === 0) === semver.eq(a, b) && (c > 0) === semver.gt(a, b), "compare agrees with eq/gt");
  const d = semver.diff(a, b);
  stitch.check((d === null) === semver.eq(a, b), "diff is null iff equal");
});

stitch.endpoint("range", { text: Str(65536, RangeText), o: O }, New(Rng), ({ text, o }) => new semver.Range(text, optsOf(o)),
  { bailsOn: [invalid] });

stitch.endpoint("valid_range", { text: Str(65536, RangeText), o: O }, null, ({ text, o }) => {
  semver.validRange(text, optsOf(o));
});

// A Range keeps the options it was parsed with; functions given a Range and other options parse
// its text again (and a loose range may not parse strictly). So every call passes r.options.
stitch.endpoint("satisfies", { v: Borrow(Ver), r: Borrow(Rng) }, null, ({ v, r }) => {
  if (semver.satisfies(v, r, r.options)) {
    const min = semver.minVersion(r, r.options);
    stitch.check(min !== null && semver.lte(min, v), "satisfying version >= minVersion(range)");
  }
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
  if (!semver.intersects(a, b, o)) stitch.check(!(inA && inB), "intersects() false but v in both");
});

stitch.endpoint("max_satisfying", { r: Borrow(Rng), a: Borrow(Ver), b: Borrow(Ver) }, null, ({ r, a, b }) => {
  const m = semver.maxSatisfying([a, b], r, r.options);
  if (m) stitch.check(semver.satisfies(m, r, r.options), "maxSatisfying() satisfies");
  semver.minSatisfying([a, b], r, r.options);
});

stitch.endpoint("simplify", { r: Borrow(Rng), a: Borrow(Ver), b: Borrow(Ver) }, null, ({ r, a, b }) => {
  const s = semver.simplifyRange([a.version, b.version], r.raw, r.options);
  for (const v of [a, b]) {
    stitch.check(semver.satisfies(v, s, r.options) === semver.satisfies(v, r, r.options), "simplifyRange keeps membership of the listed versions");
  }
}, { bailsOn: [invalid] });
