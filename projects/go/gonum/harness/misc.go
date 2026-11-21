package harness

import (
	"fmt"
	"math"
	"sort"

	"gonum.org/v1/gonum/integrate"
	"gonum.org/v1/gonum/integrate/quad"
	"gonum.org/v1/gonum/interp"
	"gonum.org/v1/gonum/num/dual"
	"gonum.org/v1/gonum/num/hyperdual"
	"gonum.org/v1/gonum/num/quat"
	"gonum.org/v1/gonum/spatial/kdtree"
	"gonum.org/v1/gonum/spatial/r2"
	"gonum.org/v1/gonum/spatial/r3"
	"gonum.org/v1/gonum/spatial/vptree"
	"stitch"
)

func q(s *stitch.Source) quat.Number {
	return quat.Number{Real: value(s), Imag: value(s), Jmag: value(s), Kmag: value(s)}
}

// qnear: |a - b| small relative to the larger of |a|, |b| (componentwise comparison fails for
// components much smaller than the others)
func qnear(a, b quat.Number, tol float64) bool {
	return quat.Abs(quat.Sub(a, b)) <= tol*math.Max(1, math.Max(quat.Abs(a), quat.Abs(b)))
}

func qfinite(a quat.Number) bool { return !quat.IsNaN(a) && !quat.IsInf(a) && quat.Abs(a) < 1e8 && quat.Abs(a) > 1e-8 }

func init() {
	quats := map[string]func(a, b quat.Number){
		"quat_inv": func(a, _ quat.Number) {
			if qfinite(a) && quat.Abs(a) > 1e-10 {
				stitch.Check(qnear(quat.Mul(a, quat.Inv(a)), quat.Number{Real: 1}, 1e-9), "q * Inv(q) == 1")
			}
		},
		"quat_exp_log": func(a, _ quat.Number) {
			if qfinite(a) && quat.Abs(a) < 20 && quat.Abs(a) > 1e-6 {
				stitch.Log("q %v Log %v Exp(Log) %v", a, quat.Log(a), quat.Exp(quat.Log(a)))
				stitch.Check(qnear(quat.Exp(quat.Log(a)), a, 1e-6), "Exp(Log(q)) == q")
			}
		},
		"quat_sqrt": func(a, _ quat.Number) {
			if qfinite(a) && quat.Abs(a) < 1e50 {
				r := quat.Sqrt(a)
				stitch.Log("q %v Sqrt %v", a, r)
				stitch.Check(qnear(quat.Mul(r, r), a, 1e-6), "Sqrt(q)^2 == q")
			}
		},
		"quat_arith": func(a, b quat.Number) {
			if qfinite(a) && qfinite(b) {
				stitch.Check(qnear(quat.Sub(quat.Add(a, b), b), a, 1e-9), "(a + b) - b == a")
				stitch.Check(near(quat.Abs(quat.Mul(a, b)), quat.Abs(a)*quat.Abs(b), 1e-9), "|a b| == |a| |b|")
				stitch.Check(qnear(quat.Conj(quat.Conj(a)), a, 0), "Conj twice")
			}
		},
		"quat_pow":   func(a, b quat.Number) { _ = quat.Pow(a, b); _ = quat.PowReal(a, b.Real) },
		"quat_trig":  func(a, _ quat.Number) { _ = quat.Sin(a); _ = quat.Cos(a); _ = quat.Tan(a); _ = quat.Sinh(a); _ = quat.Cosh(a); _ = quat.Tanh(a) },
		"quat_atrig": func(a, _ quat.Number) { _ = quat.Asin(a); _ = quat.Acos(a); _ = quat.Atan(a); _ = quat.Asinh(a); _ = quat.Acosh(a); _ = quat.Atanh(a) },
		"quat_format_parse": func(a, _ quat.Number) {
			if qfinite(a) {
				s := fmt.Sprint(a)
				back, err := quat.Parse(s)
				stitch.Check(err == nil && qnear(back, a, 1e-12), "Parse(Sprint(q)) == q ("+s+")")
			}
		},
	}
	for _, name := range sortedKeys(quats) {
		fn := quats[name]
		stitch.Endpoint(name, func(s *stitch.Source) { guard(func() { fn(q(s), q(s)) }) }, "s:max=64")
	}
	stitch.Endpoint("quat_parse", func(text string) { guard(func() { _, _ = quat.Parse(text) }) }, "text:max=48")

	// dual and hyperdual numbers: derivatives agree with each other
	duals := map[string][3]func(float64) float64{}
	_ = duals
	stitch.Endpoint("dual_derivatives", func(s *stitch.Source) {
		guard(func() {
			x := float64(s.Range(-30, 30)) / 7
			type fpair struct {
				d func(dual.Number) dual.Number
				h func(hyperdual.Number) hyperdual.Number
			}
			fs := []fpair{
				{dual.Sin, hyperdual.Sin}, {dual.Cos, hyperdual.Cos}, {dual.Exp, hyperdual.Exp}, {dual.Tanh, hyperdual.Tanh},
				{dual.Atan, hyperdual.Atan}, {dual.Sinh, hyperdual.Sinh}, {dual.Cosh, hyperdual.Cosh},
			}
			f := fs[s.Choice(len(fs))]
			d := f.d(dual.Number{Real: x, Emag: 1})
			h := f.h(hyperdual.Number{Real: x, E1mag: 1, E2mag: 1})
			stitch.Check(near(d.Real, h.Real, 1e-12) && near(d.Emag, h.E1mag, 1e-9), "dual and hyperdual first derivatives agree")
		})
	}, "s:max=8")
	stitch.Endpoint("dual_ops", func(s *stitch.Source) {
		guard(func() {
			a, b := dual.Number{Real: value(s), Emag: value(s)}, dual.Number{Real: value(s), Emag: value(s)}
			_ = dual.Mul(a, b)
			_ = dual.Inv(a)
			_ = dual.Pow(a, b)
			_ = dual.PowReal(a, b.Real)
			_ = dual.Log(a)
			_ = dual.Sqrt(a)
			_ = dual.Abs(a)
			_ = dual.Asin(a)
			_ = dual.Acos(a)
			_ = dual.Tan(a)
		})
	}, "s:max=32")
	stitch.Endpoint("hyperdual_ops", func(s *stitch.Source) {
		guard(func() {
			a := hyperdual.Number{Real: value(s), E1mag: value(s), E2mag: value(s), E1E2mag: value(s)}
			b := hyperdual.Number{Real: value(s), E1mag: value(s)}
			_ = hyperdual.Mul(a, b)
			_ = hyperdual.Inv(a)
			_ = hyperdual.Pow(a, b)
			_ = hyperdual.Log(a)
			_ = hyperdual.Sqrt(a)
			_ = hyperdual.Asin(a)
			_ = hyperdual.Acosh(a)
		})
	}, "s:max=32")

	// spatial: nearest-neighbour trees agree with brute force
	stitch.Endpoint("kdtree_nearest", func(s *stitch.Source) {
		guard(func() {
			var pts kdtree.Points
			for s.More() && len(pts) < 40 {
				pts = append(pts, kdtree.Point{float64(s.Range(-20, 20)), float64(s.Range(-20, 20))})
			}
			stitch.Require(len(pts) > 0, "no points")
			t := kdtree.New(append(kdtree.Points(nil), pts...), s.Choice(2) == 0)
			qp := kdtree.Point{float64(s.Range(-20, 20)), float64(s.Range(-20, 20))}
			_, d := t.Nearest(qp)
			best := math.Inf(1)
			for _, p := range pts {
				best = math.Min(best, p.Distance(qp))
			}
			stitch.Check(d == best, "kd-tree Nearest == brute force")
			keep := kdtree.NewNKeeper(3)
			t.NearestSet(keep, qp)
			stitch.Check(keep.Len() == min(3, len(pts)), "NearestSet keeps k points")
			dk := kdtree.NewDistKeeper(25)
			t.NearestSet(dk, qp)
			for _, c := range dk.Heap {
				stitch.Check(c.Dist <= 25, "DistKeeper keeps points within the distance")
			}
		})
	}, "s:max=128")
	stitch.Endpoint("vptree_nearest", func(s *stitch.Source) {
		guard(func() {
			var pts []vptree.Comparable
			for s.More() && len(pts) < 40 {
				pts = append(pts, vptree.Point{float64(s.Range(-20, 20)), float64(s.Range(-20, 20))})
			}
			stitch.Require(len(pts) > 0, "no points")
			t, err := vptree.New(pts, 1, nil)
			stitch.Require(err == nil, "build")
			qp := vptree.Point{float64(s.Range(-20, 20)), float64(s.Range(-20, 20))}
			_, d := t.Nearest(qp)
			best := math.Inf(1)
			for _, p := range pts {
				best = math.Min(best, p.Distance(qp))
			}
			stitch.Check(d == best, "vp-tree Nearest == brute force")
		})
	}, "s:max=128")
	stitch.Endpoint("r2_r3_vectors", func(s *stitch.Source) {
		guard(func() {
			a := r3.Vec{X: value(s), Y: value(s), Z: value(s)}
			b := r3.Vec{X: value(s), Y: value(s), Z: value(s)}
			c := r3.Cross(a, b)
			if finite([]float64{a.X, a.Y, a.Z, b.X, b.Y, b.Z}) && r3.Norm(a) < 1e100 && r3.Norm(b) < 1e100 {
				stitch.Check(math.Abs(r3.Dot(c, a)) <= 1e-9*math.Max(1, r3.Norm(a)*r3.Norm(a)*r3.Norm(b)), "a x b is orthogonal to a")
			}
			_ = r3.Unit(a)
			_ = r3.Rotate(a, value(s), b)
			_ = r3.Cos(a, b)
			p := r2.Vec{X: value(s), Y: value(s)}
			_ = r2.Rotate(p, value(s), r2.Vec{})
			_ = r2.Unit(p)
			box := r2.Box{Min: r2.Vec{X: -1, Y: -1}, Max: r2.Vec{X: 1, Y: 1}}
			_ = box.Contains(p)
			_ = box.Union(r2.Box{Min: p, Max: p}).Size()
		})
	}, "s:max=64")

	// interpolation: every interpolator passes through its knots
	fitters := map[string]func() interface {
		Fit(xs, ys []float64) error
		Predict(x float64) float64
	}{
		"piecewise_linear":   func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.PiecewiseLinear{} },
		"piecewise_constant": func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.PiecewiseConstant{} },
		"akima":              func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.AkimaSpline{} },
		"fritsch_butland":    func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.FritschButland{} },
		"natural_cubic":      func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.NaturalCubic{} },
		"clamped_cubic":      func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.ClampedCubic{} },
		"not_a_knot_cubic":   func() interface{ Fit([]float64, []float64) error; Predict(float64) float64 } { return &interp.NotAKnotCubic{} },
	}
	for _, name := range sortedKeys(fitters) {
		mk := fitters[name]
		stitch.Endpoint("interp_"+name, func(s *stitch.Source) {
			guard(func() {
				xsSet := map[int64]bool{}
				var xs []float64
				for s.More() && len(xs) < 12 {
					x := s.Range(-50, 50)
					if !xsSet[x] {
						xsSet[x] = true
						xs = append(xs, float64(x))
					}
				}
				sort.Float64s(xs)
				ys := make([]float64, len(xs))
				for i := range ys {
					ys[i] = float64(s.Range(-20, 20))
				}
				f := mk()
				if err := f.Fit(xs, ys); err != nil {
					stitch.Bail(err.Error())
				}
				for i, x := range xs {
					p := f.Predict(x)
					stitch.Check(near(p, ys[i], 1e-9) || (name == "piecewise_constant"), "the interpolant passes through its knots")
				}
				if len(xs) > 1 {
					_ = f.Predict((xs[0] + xs[1]) / 2)
					_ = f.Predict(xs[0] - 100)
				}
			})
		}, "s:max=64")
	}

	// integration: rules agree on smooth integrands with known integrals
	stitch.Endpoint("integrate_rules", func(n int, a, b int) {
		guard(func() {
			xs := make([]float64, n)
			lo, hi := float64(a), float64(a+b)
			for i := range xs {
				xs[i] = lo + (hi-lo)*float64(i)/float64(n-1)
			}
			ys := make([]float64, n)
			for i, x := range xs {
				ys[i] = 3*x*x + 1
			}
			exact := (hi*hi*hi + hi) - (lo*lo*lo + lo)
			stitch.Check(near(integrate.Simpsons(xs, ys), exact, 1e-9), "Simpson's rule is exact for quadratics")
			t := integrate.Trapezoidal(xs, ys)
			stitch.Check(t >= exact-1e-9, "the trapezoid rule over-estimates a convex integrand")
			if (n-1)&(n-2) == 0 {
				r := integrate.Romberg(ys, (hi-lo)/float64(n-1))
				stitch.Check(near(r, exact, 1e-6), "Romberg")
			}
			qf := quad.Fixed(func(x float64) float64 { return 3*x*x + 1 }, lo, hi, 5, quad.Legendre{}, 0)
			stitch.Check(near(qf, exact, 1e-9), "Gauss-Legendre quadrature")
		})
	}, "n:range=3..17", "a:range=-5..5", "b:range=1..6")
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
