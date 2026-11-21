package harness

import (
	"math"

	"golang.org/x/exp/rand"
	"gonum.org/v1/gonum/mathext"
	"gonum.org/v1/gonum/stat/combin"
	"gonum.org/v1/gonum/stat/distuv"
	"stitch"
)

type dist interface {
	CDF(float64) float64
	Prob(float64) float64
	LogProb(float64) float64
	Survival(float64) float64
	Mean() float64
	Variance() float64
	StdDev() float64
}

// par: a valid (positive, moderate) distribution parameter; gonum does not validate parameters, so
// results for invalid ones are not checked
func par(s *stitch.Source) float64 {
	return stitch.Pick(s, 0.5, 1, 2, 3.5, 10, 0.2, 5, 1.5)
}

var dists = map[string]func(s *stitch.Source) dist{
	"normal":        func(s *stitch.Source) dist { return distuv.Normal{Mu: par(s) - 2, Sigma: par(s)} },
	"lognormal":     func(s *stitch.Source) dist { return distuv.LogNormal{Mu: par(s) - 2, Sigma: par(s)} },
	"uniform":       func(s *stitch.Source) dist { a := par(s) - 5; return distuv.Uniform{Min: a, Max: a + par(s)} },
	"exponential":   func(s *stitch.Source) dist { return distuv.Exponential{Rate: par(s)} },
	"gamma":         func(s *stitch.Source) dist { return distuv.Gamma{Alpha: par(s), Beta: par(s)} },
	"beta":          func(s *stitch.Source) dist { return distuv.Beta{Alpha: par(s), Beta: par(s)} },
	"chisquared":    func(s *stitch.Source) dist { return distuv.ChiSquared{K: par(s)} },
	"chi":           func(s *stitch.Source) dist { return distuv.Chi{K: par(s)} },
	"studentst":     func(s *stitch.Source) dist { return distuv.StudentsT{Mu: 0, Sigma: par(s), Nu: par(s)} },
	"f":             func(s *stitch.Source) dist { return distuv.F{D1: par(s), D2: par(s)} },
	"laplace":       func(s *stitch.Source) dist { return distuv.Laplace{Mu: par(s), Scale: par(s)} },
	"logistic":      func(s *stitch.Source) dist { return distuv.Logistic{Mu: par(s), S: par(s)} },
	"weibull":       func(s *stitch.Source) dist { return distuv.Weibull{K: par(s), Lambda: par(s)} },
	"pareto":        func(s *stitch.Source) dist { return distuv.Pareto{Xm: par(s), Alpha: par(s)} },
	"gumbel":        func(s *stitch.Source) dist { return distuv.GumbelRight{Mu: par(s), Beta: par(s)} },
	"inversegamma":  func(s *stitch.Source) dist { return distuv.InverseGamma{Alpha: par(s), Beta: par(s)} },
	"triangle":      func(s *stitch.Source) dist { a := par(s) - 5; b := a + par(s); return distuv.NewTriangle(a, b, (a+b)/2, nil) },
	"poisson":       func(s *stitch.Source) dist { return distuv.Poisson{Lambda: par(s)} },
	"binomial":      func(s *stitch.Source) dist { return distuv.Binomial{N: math.Floor(par(s) * 3), P: stitch.Pick(s, 0.0, 0.3, 0.5, 1.0)} },
	"bernoulli":     func(s *stitch.Source) dist { return distuv.Bernoulli{P: stitch.Pick(s, 0.0, 0.3, 0.5, 1.0)} },
}

var discrete = map[string]bool{"poisson": true, "binomial": true, "bernoulli": true}

func init() {
	for _, name := range sortedKeys(dists) {
		name, mk := name, dists[name]
		stitch.Endpoint("dist_"+name+"_quantile_cdf", func(s *stitch.Source) {
			guard(func() {
				d := mk(s)
				qd, ok := d.(interface{ Quantile(float64) float64 })
				stitch.Require(ok, "no Quantile")
				p := stitch.Pick(s, 0.001, 0.1, 0.25, 0.5, 0.75, 0.9, 0.999)
				x := qd.Quantile(p)
				c := d.CDF(x)
				stitch.Log("%#v: Quantile(%v) = %v, CDF = %v", d, p, x, c)
				if !math.IsNaN(x) && !math.IsInf(x, 0) {
					if discrete[name] {
						stitch.Check(c >= p-1e-9, "discrete: CDF(Quantile(p)) >= p")
					} else {
						stitch.Check(math.Abs(c-p) <= 1e-6, "CDF(Quantile(p)) == p")
					}
				}
			})
		}, "s:max=16")
		stitch.Endpoint("dist_"+name+"_density", func(s *stitch.Source) {
			guard(func() {
				d := mk(s)
				x := value(s)
				p, lp := d.Prob(x), d.LogProb(x)
				stitch.Check(math.IsNaN(p) || p >= 0, "Prob >= 0")
				if p > 1e-300 && !math.IsInf(lp, 0) && !math.IsNaN(lp) {
					stitch.Log("%#v x=%v Prob %v LogProb %v", d, x, p, lp)
					stitch.Check(math.Abs(math.Exp(lp)-p) <= 1e-6*math.Max(1, p), "exp(LogProb) == Prob")
				}
				c, sv := d.CDF(x), d.Survival(x)
				if !math.IsNaN(c) && !math.IsNaN(sv) {
					stitch.Check(c >= -1e-12 && c <= 1+1e-12, "CDF in [0, 1]")
					stitch.Check(math.Abs(c+sv-1) <= 1e-9, "CDF + Survival == 1")
				}
			})
		}, "s:max=16")
		stitch.Endpoint("dist_"+name+"_moments", func(s *stitch.Source) {
			guard(func() {
				d := mk(s)
				m, v, sd := d.Mean(), d.Variance(), d.StdDev()
				if !math.IsNaN(v) && !math.IsInf(v, 0) {
					stitch.Check(v >= 0 && math.Abs(sd*sd-v) <= 1e-9*math.Max(1, v), "StdDev^2 == Variance")
				}
				_ = m
				if r, ok := d.(interface{ Entropy() float64 }); ok {
					_ = r.Entropy()
				}
				if r, ok := d.(interface{ ExKurtosis() float64 }); ok {
					_ = r.ExKurtosis()
				}
				if r, ok := d.(interface{ Skewness() float64 }); ok {
					_ = r.Skewness()
				}
				if r, ok := d.(interface{ Median() float64 }); ok {
					med := r.Median()
					if !discrete[name] && !math.IsNaN(med) && !math.IsInf(med, 0) {
						stitch.Check(math.Abs(d.CDF(med)-0.5) <= 1e-6, "CDF(Median) == 1/2")
					}
				}
				if r, ok := d.(interface{ Mode() float64 }); ok {
					_ = r.Mode()
				}
			})
		}, "s:max=16")
	}

	// mathext special functions, with their inverses and identities
	special := map[string]func(s *stitch.Source){
		"mathext_beta": func(s *stitch.Source) {
			a, b := par(s), par(s)
			if a > 0 && b > 0 && a < 50 && b < 50 {
				stitch.Check(near(math.Log(mathext.Beta(a, b)), mathext.Lbeta(a, b), 1e-9), "log Beta == Lbeta")
				stitch.Check(near(mathext.Beta(a, b), mathext.Beta(b, a), 1e-12), "Beta is symmetric")
			}
		},
		"mathext_digamma": func(s *stitch.Source) {
			x := value(s)
			if x > 0.1 && x < 1e6 {
				stitch.Check(near(mathext.Digamma(x+1), mathext.Digamma(x)+1/x, 1e-9), "digamma(x + 1) == digamma(x) + 1/x")
			}
		},
		"mathext_gamma_inc_reg": func(s *stitch.Source) {
			a, x := par(s), par(s)
			p, q := mathext.GammaIncReg(a, x), mathext.GammaIncRegComp(a, x)
			if !math.IsNaN(p) && !math.IsNaN(q) {
				stitch.Check(math.Abs(p+q-1) <= 1e-9, "P(a, x) + Q(a, x) == 1")
			}
		},
		"mathext_gamma_inc_reg_inv": func(s *stitch.Source) {
			a, y := par(s), stitch.Pick(s, 0.01, 0.3, 0.5, 0.9, 0.999)
			if a > 0 && a < 50 {
				x := mathext.GammaIncRegInv(a, y)
				stitch.Check(math.IsNaN(x) || math.Abs(mathext.GammaIncReg(a, x)-y) <= 1e-6, "GammaIncReg(a, GammaIncRegInv(a, y)) == y")
				xc := mathext.GammaIncRegCompInv(a, y)
				stitch.Check(math.IsNaN(xc) || math.Abs(mathext.GammaIncRegComp(a, xc)-y) <= 1e-6, "GammaIncRegComp(a, GammaIncRegCompInv(a, y)) == y")
			}
		},
		"mathext_reg_inc_beta": func(s *stitch.Source) {
			a, b, y := par(s), par(s), stitch.Pick(s, 0.01, 0.3, 0.5, 0.9, 0.999)
			if a > 0 && b > 0 && a < 50 && b < 50 {
				x := mathext.InvRegIncBeta(a, b, y)
				stitch.Check(math.IsNaN(x) || math.Abs(mathext.RegIncBeta(a, b, x)-y) <= 1e-6, "RegIncBeta(a, b, InvRegIncBeta(a, b, y)) == y")
			}
		},
		"mathext_zeta":    func(s *stitch.Source) { _ = mathext.Zeta(par(s)+1, par(s)) },
		"mathext_mvlgamma": func(s *stitch.Source) { _ = mathext.MvLgamma(par(s)+5, int(s.Range(1, 4))) },
		"mathext_elliptic": func(s *stitch.Source) {
			x, y, z := par(s), par(s), par(s)
			_ = mathext.EllipticRF(x, y, z)
			_ = mathext.EllipticRD(x, y, z)
			phi, m := value(s), stitch.Pick(s, 0.0, 0.5, 0.99, 1.0)
			_ = mathext.EllipticF(phi, m)
			_ = mathext.EllipticE(phi, m)
		},
		"mathext_airy": func(s *stitch.Source) { _ = mathext.AiryAi(complex(value(s), value(s))) },
		"mathext_lgamma": func(s *stitch.Source) { _, _ = math.Lgamma(value(s)) },
	}
	for _, name := range sortedKeys(special) {
		fn := special[name]
		stitch.Endpoint(name, func(s *stitch.Source) { guard(func() { fn(s) }) }, "s:max=16")
	}

	// combin: counting, indexing and enumeration agree
	stitch.Endpoint("combin_binomial", func(n, k int) {
		guard(func() {
			b := combin.Binomial(n, k)
			if k >= 0 && n >= k && n < 30 {
				stitch.Check(b == len(combin.Combinations(n, k)), "Binomial(n, k) == len(Combinations(n, k))")
				stitch.Check(near(float64(b), combin.GeneralizedBinomial(float64(n), float64(k)), 1e-9), "Binomial == GeneralizedBinomial")
				stitch.Check(near(math.Log(float64(b)), combin.LogGeneralizedBinomial(float64(n), float64(k)), 1e-9), "LogGeneralizedBinomial")
			}
		})
	}, "n:range=-2..20", "k:range=-2..20")
	stitch.Endpoint("combin_combination_index", func(n, k, i int) {
		guard(func() {
			c := combin.IndexToCombination(nil, i, n, k)
			stitch.Check(combin.CombinationIndex(c, n, k) == i, "CombinationIndex(IndexToCombination(i)) == i")
		})
	}, "n:range=0..15", "k:range=0..15", "i:range=0..300")
	stitch.Endpoint("combin_permutation_index", func(n, k, i int) {
		guard(func() {
			p := combin.IndexToPermutation(nil, i, n, k)
			stitch.Check(combin.PermutationIndex(p, n, k) == i, "PermutationIndex(IndexToPermutation(i)) == i")
			stitch.Check(len(combin.Permutations(n, k)) == combin.NumPermutations(n, k), "len(Permutations) == NumPermutations")
		})
	}, "n:range=0..7", "k:range=0..7", "i:range=0..300")
	stitch.Endpoint("combin_combination_generator", func(n, k int) {
		guard(func() {
			g := combin.NewCombinationGenerator(n, k)
			cnt := 0
			for g.Next() && cnt < 5000 {
				_ = g.Combination(nil)
				cnt++
			}
			stitch.Check(cnt == combin.Binomial(n, k), "the generator yields Binomial(n, k) combinations")
		})
	}, "n:range=0..14", "k:range=0..14")
	stitch.Endpoint("combin_permutation_generator", func(n, k int) {
		guard(func() {
			g := combin.NewPermutationGenerator(n, k)
			cnt := 0
			for g.Next() && cnt < 6000 {
				_ = g.Permutation(nil)
				cnt++
			}
			stitch.Check(cnt == combin.NumPermutations(n, k), "the generator yields NumPermutations(n, k) permutations")
		})
	}, "n:range=0..7", "k:range=0..7")
	stitch.Endpoint("combin_cartesian", func(a, b, c int) {
		guard(func() {
			dims := []int{a, b, c}
			prods := combin.Cartesian(dims)
			stitch.Check(len(prods) == a*b*c || a*b*c == 0, "len(Cartesian) == product of the dimensions")
			for i, p := range prods {
				stitch.Check(combin.IdxFor(p, dims) == i, "IdxFor(Cartesian[i]) == i")
				stitch.Check(intsEq(combin.SubFor(nil, i, dims), p), "SubFor(i) == Cartesian[i]")
			}
		})
	}, "a:range=0..4", "b:range=0..4", "c:range=0..4")
	stitch.Endpoint("combin_cartesian_generator", func(a, b int) {
		guard(func() {
			g := combin.NewCartesianGenerator([]int{a, b})
			n := 0
			for g.Next() {
				_ = g.Product(nil)
				n++
			}
			stitch.Check(n == a*b, "the generator yields every product")
		})
	}, "a:range=0..6", "b:range=0..6")
	stitch.Endpoint("dist_rand_seeded", func(seed int) {
		guard(func() {
			a := distuv.Normal{Mu: 0, Sigma: 1, Src: rand.NewSource(uint64(seed))}
			b := distuv.Normal{Mu: 0, Sigma: 1, Src: rand.NewSource(uint64(seed))}
			stitch.Check(a.Rand() == b.Rand(), "same seed, same draws")
			c := distuv.NewCategorical([]float64{1, 2, 0, 3}, rand.NewSource(uint64(seed)))
			x := c.Rand()
			stitch.Check(x != 2, "a zero-weight category is never drawn")
			c.Reweight(0, 0)
			_ = c.Rand()
		})
	}, "seed:range=0..1000")
}

func intsEq(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}
