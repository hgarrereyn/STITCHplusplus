// Linear algebra identities on small integer matrices (exact enough in floating point).
const { stitch, math, V, BAILS, Borrow, Src } = require("./common");

// (compared as dense arrays: math.size of an Array and of a Matrix have different types)
const dense = (x) => (math.isMatrix(x) ? x.toArray() : x);
const approx = (a, b, tol = 1e-6) => {
  const A = dense(a), B = dense(b);
  return JSON.stringify(math.size(A)) === JSON.stringify(math.size(B)) && math.max(math.abs(math.subtract(A, B))) <= tol * Math.max(1, math.max(math.abs(A)));
};
function square(s, n) {
  return Array.from({ length: n }, () => Array.from({ length: n }, () => s.range(-5, 5)));
}
const sparseOr = (s, d) => (s.coin() ? math.sparse(d) : math.matrix(d));

stitch.endpoint("inverse_identity", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 4);
  const A = sparseOr(s, square(s, n));
  const d = math.det(A);
  stitch.require(Math.abs(d) >= 0.5, "singular");  // (integer matrices: |det| >= 1 when invertible)
  const I = math.multiply(math.inv(A), A);
  stitch.log(`A ${math.format(A)} det ${d} inv(A) A ${math.format(I, { precision: 6 })}`);
  stitch.check(approx(math.matrix(I), math.identity(n)), "inv(A) * A == I");
}, { bailsOn: BAILS });

stitch.endpoint("det_multiplicative", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 4);
  const A = sparseOr(s, square(s, n)), B = sparseOr(s, square(s, n));
  const lhs = math.det(math.multiply(A, B)), rhs = math.det(A) * math.det(B);
  stitch.log(`det(AB) ${lhs} det(A)det(B) ${rhs}`);
  stitch.check(Math.abs(lhs - rhs) <= 1e-6 * Math.max(1, Math.abs(rhs)), "det(AB) == det(A) det(B)");
  stitch.check(Math.abs(math.det(math.transpose(A)) - math.det(A)) <= 1e-6 * Math.max(1, Math.abs(math.det(A))), "det(A') == det(A)");
}, { bailsOn: BAILS });

stitch.endpoint("solvers_agree", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 4);
  const A = square(s, n);
  const b = Array.from({ length: n }, () => [s.range(-5, 5)]);
  stitch.require(Math.abs(math.det(A)) >= 0.5, "singular");
  const x1 = math.lusolve(A, b);
  stitch.check(approx(math.multiply(A, x1), b), "lusolve: A x == b");
  const lup = math.lup(A);
  const PA = lup.p.map((r) => A[r]);  // (row i of P A is row p[i] of A)
  stitch.check(approx(math.multiply(lup.L, lup.U), PA), "lup: L U == P A");
  const x2 = math.lusolve(math.sparse(A), b);
  stitch.check(approx(math.matrix(x2), math.matrix(x1)), "sparse and dense lusolve agree");
  const qr = math.qr(A);
  stitch.check(approx(math.multiply(qr.Q, qr.R), A), "qr: Q R == A");
  const slu = math.slu(math.sparse(A), s.range(0, 3), 1);
  const x3 = math.lusolve(slu, b);
  stitch.check(approx(math.matrix(x3), math.matrix(x1)), "slu + lusolve agrees with lusolve");
}, { bailsOn: BAILS });

stitch.endpoint("triangular_solvers", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 4);
  const L = Array.from({ length: n }, (_, i) => Array.from({ length: n }, (_, j) => (j < i ? s.range(-5, 5) : j === i ? s.pick([1, 2, -3]) : 0)));
  const b = Array.from({ length: n }, () => [s.range(-5, 5)]);
  stitch.check(approx(math.multiply(L, math.lsolve(L, b)), b), "lsolve: L x == b");
  const U = math.transpose(L);
  stitch.check(approx(math.multiply(U, math.usolve(U, b)), b), "usolve: U x == b");
  for (const x of math.lsolveAll(L, b)) stitch.check(approx(math.multiply(L, x), b), "lsolveAll solutions solve the system");
}, { bailsOn: BAILS });

stitch.endpoint("sparse_dense_agree", { s: Src(64), op: stitch.OneOf("add", "subtract", "multiply", "dotMultiply", "transpose", "abs", "unaryMinus", "trace", "det", "kron", "max", "sum") }, null,
  ({ s, op }) => {
    const n = s.range(1, 4), m = s.range(1, 4);
    const A = Array.from({ length: n }, () => Array.from({ length: op === "trace" || op === "det" ? n : m }, () => (s.coin(0.5) ? 0 : s.range(-5, 5))));
    const B = Array.from({ length: op === "multiply" ? A[0].length : n }, () => Array.from({ length: m }, () => (s.coin(0.5) ? 0 : s.range(-5, 5))));
    const unary = ["transpose", "abs", "unaryMinus", "trace", "det", "max", "sum"].includes(op);
    const dense = unary ? math[op](math.matrix(A)) : math[op](math.matrix(A), math.matrix(B));
    const sparse = unary ? math[op](math.sparse(A)) : math[op](math.sparse(A), math.sparse(B));
    const mixed = unary ? dense : math[op](math.sparse(A), math.matrix(B));
    const norm = (x) => (math.isMatrix(x) ? math.matrix(x.toArray()).toArray() : x);
    stitch.log(`${op}: dense ${JSON.stringify(norm(dense))} sparse ${JSON.stringify(norm(sparse))}`);
    stitch.check(math.deepEqual(norm(dense), norm(sparse)), `${op}: sparse and dense matrices agree`);
    stitch.check(math.deepEqual(norm(dense), norm(mixed)), `${op}: sparse with dense agrees`);
  }, { bailsOn: BAILS });

stitch.endpoint("eigs_check", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 3);
  const A = square(s, n);
  const S = math.add(A, math.transpose(A));
  const { values, vectors } = math.eigs(S);
  stitch.log(`S ${JSON.stringify(S)} values ${math.format(values, { precision: 8 })} vectors ${math.format(vectors, { precision: 8 })}`);
  const V2 = math.matrix(vectors);
  for (let i = 0; i < n; i++) {
    const v = math.column(V2, i);
    const lam = math.subset(math.matrix(values), math.index(i));
    stitch.log(`S v ${JSON.stringify(dense(math.multiply(S, v)))} lambda v ${JSON.stringify(dense(math.multiply(lam, v)))}`);
    // (compared flattened: math.column of a 1x1 matrix returns a scalar)
    const flat = (x) => [].concat(...[dense(x)].flat(3));
    stitch.check(approx(flat(math.multiply(S, v)), flat(math.multiply(lam, v)), 1e-4), "symmetric eigs: S v == lambda v");
  }
}, { bailsOn: BAILS });

stitch.endpoint("expm_sqrtm", { s: Src(64) }, null, ({ s }) => {
  const n = s.range(1, 3);
  const A = math.multiply(square(s, n), 0.1);
  const S = math.multiply(A, math.transpose(A));
  const E = math.expm(math.zeros(n, n));
  stitch.check(approx(E, math.identity(n)), "expm(0) == I");
  const R = math.sqrtm(math.add(S, math.identity(n)));
  stitch.check(approx(math.multiply(R, R), math.add(S, math.identity(n)), 1e-5), "sqrtm(M)^2 == M");
}, { bailsOn: BAILS });

stitch.endpoint("set_functions_agree", { s: Src(64) }, null, ({ s }) => {
  const a = Array.from({ length: s.range(0, 6) }, () => s.range(0, 5)), b = Array.from({ length: s.range(0, 6) }, () => s.range(0, 5));
  const sort = (x) => [...x].sort((p, q) => p - q);
  const uniq = (x) => sort([...new Set(x)]);
  stitch.check(JSON.stringify(sort(math.setDistinct(a))) === JSON.stringify(uniq(a)) || a.length === 0, "setDistinct");
  if (a.length && b.length) {
    stitch.check(JSON.stringify(uniq(math.setUnion(a, b))) === JSON.stringify(uniq([...a, ...b])), "setUnion");
    stitch.check(JSON.stringify(uniq(math.setIntersect(a, b))) === JSON.stringify(uniq(a.filter((x) => b.includes(x)))), "setIntersect");
    // (multisets: setDifference removes as many copies as b has)
    const left = [...a];
    for (const x of b) { const i = left.indexOf(x); if (i >= 0) left.splice(i, 1); }
    stitch.check(JSON.stringify(sort(math.setDifference(a, b))) === JSON.stringify(sort(left)), "setDifference (multisets)");
  }
}, { bailsOn: BAILS });
