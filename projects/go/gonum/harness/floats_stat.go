package harness

import (
	"math"
	"sort"

	"gonum.org/v1/gonum/floats"
	"gonum.org/v1/gonum/floats/scalar"
	"gonum.org/v1/gonum/stat"
	"stitch"
)

func cp(v []float64) []float64 { return append([]float64(nil), v...) }

func init() {
	// floats: unary reductions, checked against straightforward loops where that is exact
	unary := map[string]func(v []float64){
		"floats_sum": func(v []float64) {
			if finite(v) && len(v) < 8 {
				naive := 0.0
				for _, x := range v {
					naive += x
				}
				stitch.Check(near(floats.Sum(v), naive, 1e-12) || math.Abs(naive) > 1e200, "Sum matches a loop")
			}
		},
		"floats_sum_compensated": func(v []float64) { _ = floats.SumCompensated(v) },
		"floats_prod":            func(v []float64) { _ = floats.Prod(v) },
		"floats_max": func(v []float64) {
			if len(v) > 0 {
				stitch.Check(floats.Max(v) == v[floats.MaxIdx(v)] || math.IsNaN(floats.Max(v)), "Max == v[MaxIdx]")
			}
		},
		"floats_min": func(v []float64) {
			if len(v) > 0 {
				stitch.Check(floats.Min(v) == v[floats.MinIdx(v)] || math.IsNaN(floats.Min(v)), "Min == v[MinIdx]")
			}
		},
		"floats_logsumexp": func(v []float64) {
			if finite(v) && len(v) > 0 {
				r := floats.LogSumExp(v)
				stitch.Check(r >= floats.Max(v)-1e-9 || math.IsNaN(r), "LogSumExp >= Max")
			}
		},
		"floats_hasnan": func(v []float64) {
			has := false
			for _, x := range v {
				has = has || math.IsNaN(x)
			}
			stitch.Check(floats.HasNaN(v) == has, "HasNaN")
		},
		"floats_argsort": func(v []float64) {
			d := cp(v)
			idx := make([]int, len(d))
			floats.Argsort(d, idx)
			for i := 1; i < len(d); i++ {
				stitch.Check(!(d[i] < d[i-1]), "Argsort sorts")
			}
			for i, j := range idx {
				stitch.Check(v[j] == d[i] || math.IsNaN(d[i]), "Argsort indices point at the sorted values")
			}
		},
		"floats_argsort_stable": func(v []float64) {
			d := cp(v)
			idx := make([]int, len(d))
			floats.ArgsortStable(d, idx)
			for i := 1; i < len(d); i++ {
				stitch.Check(!(d[i] < d[i-1]), "ArgsortStable sorts")
				if d[i] == d[i-1] {
					stitch.Check(idx[i] > idx[i-1], "ArgsortStable keeps equal elements in order")
				}
			}
		},
		"floats_cumsum": func(v []float64) {
			c := floats.CumSum(make([]float64, len(v)), v)
			if len(v) > 0 && finite(v) {
				stitch.Check(near(c[len(c)-1], floats.Sum(v), 1e-9), "last CumSum == Sum")
			}
		},
		"floats_cumprod": func(v []float64) { _ = floats.CumProd(make([]float64, len(v)), v) },
		"floats_reverse": func(v []float64) {
			d := cp(v)
			floats.Reverse(d)
			floats.Reverse(d)
			stitch.Check(floats.Same(d, v), "Reverse twice is the identity")
		},
		"floats_norm_l1":   func(v []float64) { _ = floats.Norm(v, 1) },
		"floats_norm_l2":   func(v []float64) { _ = floats.Norm(v, 2) },
		"floats_norm_inf":  func(v []float64) { _ = floats.Norm(v, math.Inf(1)) },
		"floats_norm_half": func(v []float64) { _ = floats.Norm(v, 0.5) },
		"floats_count":     func(v []float64) { _ = floats.Count(func(x float64) bool { return x > 0 }, v) },
		"floats_find": func(v []float64) {
			ix, err := floats.Find(nil, func(x float64) bool { return x > 0 }, v, 2)
			for _, i := range ix {
				stitch.Check(v[i] > 0, "Find returns matching indices")
			}
			_ = err
		},
		"floats_scale_addconst": func(v []float64) {
			d := cp(v)
			floats.Scale(2, d)
			floats.AddConst(-1, d)
		},
		"floats_within_sorted": func(v []float64) {
			d := cp(v)
			sort.Float64s(d)
			if len(d) >= 2 && finite(d) {
				i := floats.Within(d, (d[0]+d[len(d)-1])/2)
				stitch.Check(i == -1 || (d[i] <= (d[0]+d[len(d)-1])/2 && (d[0]+d[len(d)-1])/2 < d[i+1]), "Within finds the bracketing interval")
			}
		},
		"floats_nearest_idx": func(v []float64) {
			if len(v) > 0 {
				i := floats.NearestIdx(v, 0.3)
				for _, x := range v {
					stitch.Check(math.IsNaN(x) || math.Abs(v[i]-0.3) <= math.Abs(x-0.3) || math.IsNaN(v[i]), "NearestIdx is nearest")
				}
			}
		},
		"stat_entropy": func(v []float64) { _ = stat.Entropy(v) },
		"stat_mean_variance": func(v []float64) {
			if finite(v) && len(v) > 1 {
				m, va := stat.MeanVariance(v, nil)
				stitch.Check(near(m, stat.Mean(v, nil), 1e-9) && near(va, stat.Variance(v, nil), 1e-9), "MeanVariance == (Mean, Variance)")
				stitch.Check(va >= 0 || math.IsNaN(va), "variance >= 0")
				m2, sd := stat.MeanStdDev(v, nil)
				stitch.Check(near(m2, m, 1e-12) && near(sd*sd, va, 1e-9), "MeanStdDev agrees")
				pm, pv := stat.PopMeanVariance(v, nil)
				stitch.Check(near(pm, m, 1e-12) && near(pv*float64(len(v))/float64(len(v)-1), va, 1e-9), "population variance relation")
				ones := make([]float64, len(v))
				for i := range ones {
					ones[i] = 1
				}
				stitch.Check(near(stat.Variance(v, ones), va, 1e-9) && near(stat.Mean(v, ones), m, 1e-9), "unit weights == no weights")
			}
		},
		"stat_skew":           func(v []float64) { _ = stat.Skew(v, nil) },
		"stat_exkurtosis":     func(v []float64) { _ = stat.ExKurtosis(v, nil) },
		"stat_geometric_mean": func(v []float64) { _ = stat.GeometricMean(v, nil) },
		"stat_harmonic_mean":  func(v []float64) { _ = stat.HarmonicMean(v, nil) },
		"stat_circular_mean":  func(v []float64) { _ = stat.CircularMean(v, nil) },
		"stat_mode": func(v []float64) {
			val, cnt := stat.Mode(v, nil)
			if len(v) > 0 && !floats.HasNaN(v) {
				n := 0.0
				for _, x := range v {
					if x == val {
						n++
					}
				}
				stitch.Check(n == cnt, "Mode's count is the value's count")
			}
		},
		"stat_moment":       func(v []float64) { _ = stat.Moment(3, v, nil) },
		"stat_moment_about": func(v []float64) { _ = stat.MomentAbout(4, v, 0, nil) },
		"stat_quantiles": func(v []float64) {
			d := cp(v)
			sort.Float64s(d)
			stitch.Require(len(d) > 0 && finite(d), "empty or not finite")
			for _, k := range []stat.CumulantKind{stat.Empirical, stat.LinInterp} {
				prev := math.Inf(-1)
				for _, p := range []float64{0, 0.1, 0.25, 0.5, 0.9, 1} {
					q := stat.Quantile(p, k, d, nil)
					stitch.Check(q >= prev-1e-12, "Quantile is monotone in p")
					stitch.Check(q >= d[0]-1e-9 && q <= d[len(d)-1]+1e-9, "Quantile lies within the data")
					prev = q
					c := stat.CDF(q, k, d, nil)
					stitch.Check(c >= 0 && c <= 1, "CDF in [0, 1]")
				}
			}
		},
		"stat_sort_weighted": func(v []float64) {
			d := cp(v)
			w := make([]float64, len(d))
			for i := range w {
				w[i] = float64(i)
			}
			stat.SortWeighted(d, w)
			for i := 1; i < len(d); i++ {
				stitch.Check(!(d[i] < d[i-1]), "SortWeighted sorts")
			}
		},
		"stat_std_err":    func(v []float64) { _ = stat.StdErr(stat.StdDev(v, nil), float64(len(v))) },
		"stat_pop_stddev": func(v []float64) { _ = stat.PopStdDev(v, nil) },
	}
	for _, name := range sortedKeys(unary) {
		fn := unary[name]
		stitch.Endpoint(name, func(v *Vec) { guard(func() { fn(v.v) }) }, "v")
	}

	binary := map[string]func(a, b []float64){
		"floats_dot":       func(a, b []float64) { _ = floats.Dot(a, b) },
		"floats_distance":  func(a, b []float64) { stitch.Check(floats.Distance(a, a, 2) == 0 || floats.HasNaN(a) || !finite(a), "Distance(a, a) == 0"); _ = floats.Distance(a, b, 1) },
		"floats_add_sub": func(a, b []float64) {
			d := cp(a)
			floats.Add(d, b)
			floats.Sub(d, b)
			sum := floats.AddTo(make([]float64, len(a)), a, b)
			if finite(a) && finite(b) && finite(sum) {
				stitch.Check(floats.EqualApprox(d, a, 1e-9*(1+floats.Norm(b, math.Inf(1)))), "(a + b) - b == a")
			}
		},
		"floats_mul_div":    func(a, b []float64) { d := cp(a); floats.Mul(d, b); floats.Div(d, b) },
		"floats_addto":      func(a, b []float64) { _ = floats.AddTo(make([]float64, len(a)), a, b) },
		"floats_subto":      func(a, b []float64) { _ = floats.SubTo(make([]float64, len(a)), a, b) },
		"floats_multo":      func(a, b []float64) { _ = floats.MulTo(make([]float64, len(a)), a, b) },
		"floats_divto":      func(a, b []float64) { _ = floats.DivTo(make([]float64, len(a)), a, b) },
		"floats_add_scaled": func(a, b []float64) { d := cp(a); floats.AddScaled(d, 2.5, b); _ = floats.AddScaledTo(make([]float64, len(a)), a, -1, b) },
		"floats_equal": func(a, b []float64) {
			stitch.Check(floats.Equal(a, b) == floats.EqualFunc(a, b, func(x, y float64) bool { return x == y }), "Equal == EqualFunc(==)")
			stitch.Check(!floats.Equal(a, b) || floats.EqualApprox(a, b, 0), "Equal implies EqualApprox")
		},
		"floats_same":          func(a, b []float64) { stitch.Check(floats.Same(a, a), "Same(a, a)"); _ = floats.Same(a, b) },
		"stat_correlation":     func(a, b []float64) { c := stat.Correlation(a, b, nil); stitch.Check(math.IsNaN(c) || math.Abs(c) <= 1+1e-9, "|correlation| <= 1") },
		"stat_covariance":      func(a, b []float64) { _ = stat.Covariance(a, b, nil) },
		"stat_kendall":         func(a, b []float64) { k := stat.Kendall(a, b, nil); stitch.Check(math.IsNaN(k) || math.Abs(k) <= 1+1e-9, "|Kendall tau| <= 1") },
		"stat_linear_regression": func(a, b []float64) {
			al, be := stat.LinearRegression(a, b, nil, false)
			_ = stat.RSquared(a, b, nil, al, be)
			_ = stat.RNoughtSquared(a, b, nil, be)
		},
		"stat_weighted_mean": func(a, b []float64) { _ = stat.Mean(a, b) },
		"stat_chi_square":    func(a, b []float64) { _ = stat.ChiSquare(a, b) },
		"stat_kl":            func(a, b []float64) { _ = stat.KullbackLeibler(a, b) },
		"stat_cross_entropy": func(a, b []float64) { _ = stat.CrossEntropy(a, b) },
		"stat_hellinger":     func(a, b []float64) { _ = stat.Hellinger(a, b) },
		"stat_bhattacharyya": func(a, b []float64) { _ = stat.Bhattacharyya(a, b) },
		"stat_jensen_shannon": func(a, b []float64) {
			if len(a) > 0 && floats.Min(a) > 1e-100 && floats.Min(b) > 1e-100 && floats.Max(a) < 1e100 && floats.Max(b) < 1e100 {
				pa, pb := cp(a), cp(b)
				floats.Scale(1/floats.Sum(pa), pa)
				floats.Scale(1/floats.Sum(pb), pb)
				js := stat.JensenShannon(pa, pb)
				stitch.Check(js >= -1e-12 && js <= math.Ln2+1e-9, "Jensen-Shannon in [0, ln 2]")
				stitch.Check(near(js, stat.JensenShannon(pb, pa), 1e-9), "Jensen-Shannon is symmetric")
			}
		},
		"stat_kolmogorov_smirnov": func(a, b []float64) {
			stitch.Require(finite(a) && finite(b), "not finite")
			x, y := cp(a), cp(b)
			sort.Float64s(x)
			sort.Float64s(y)
			ks := stat.KolmogorovSmirnov(x, nil, y, nil)
			stitch.Check(math.IsNaN(ks) || (ks >= -1e-12 && ks <= 1+1e-12), "KS statistic in [0, 1]")
		},
		"stat_bivariate_moment": func(a, b []float64) { _ = stat.BivariateMoment(1, 2, a, b, nil) },
		"stat_histogram": func(a, b []float64) {
			x := cp(a)
			sort.Float64s(x)
			div := cp(b)
			sort.Float64s(div)
			if len(div) >= 2 && len(x) > 0 && finite(x) && finite(div) && div[0] <= x[0] && div[len(div)-1] > x[len(x)-1] {
				h := stat.Histogram(nil, div, x, nil)
				stitch.Check(near(floats.Sum(h), float64(len(x)), 0), "the histogram counts every value")
			}
		},
		"stat_roc": func(a, b []float64) {
			y := cp(a)
			sort.Float64s(y)
			classes := make([]bool, len(y))
			for i := range classes {
				classes[i] = i < len(b) && b[i] > 0
			}
			tpr, fpr, _ := stat.ROC(nil, y, classes, nil)
			for i := range tpr {
				stitch.Check((tpr[i] >= 0 && tpr[i] <= 1 || math.IsNaN(tpr[i])) && (fpr[i] >= 0 && fpr[i] <= 1 || math.IsNaN(fpr[i])), "ROC rates in [0, 1]")
			}
		},
		"scalar_equal_within": func(a, b []float64) {
			if len(a) > 0 && len(b) > 0 {
				_ = scalar.EqualWithinAbsOrRel(a[0], b[0], 1e-9, 1e-9)
				_ = scalar.EqualWithinULP(a[0], b[0], 4)
				_ = scalar.Round(a[0], int(b[0])%20)
				_ = scalar.RoundEven(a[0], int(b[0])%20)
			}
		},
	}
	for _, name := range sortedKeys(binary) {
		fn := binary[name]
		stitch.Endpoint(name, func(a, b *Vec) { guard(func() { fn(a.v, b.v) }) }, "a", "b")
	}

	stitch.Endpoint("floats_span", func(n int, l, u int) *Vec {
		var out []float64
		guard(func() {
			out = floats.Span(make([]float64, n), float64(l), float64(u))
			stitch.Check(n < 2 || (out[0] == float64(l) && out[n-1] == float64(u)), "Span hits both ends")
			_ = floats.NearestIdxForSpan(n, float64(l), float64(u), 0.5)
		})
		return &Vec{out}
	}, "n:range=0..12", "l:range=-10..10", "u:range=-10..10")
	stitch.Endpoint("floats_logspan", func(n int, l, u int) *Vec {
		var out []float64
		guard(func() { out = floats.LogSpan(make([]float64, n), float64(l), float64(u)) })
		return &Vec{out}
	}, "n:range=0..12", "l:range=-10..10", "u:range=-10..10")
	stitch.Endpoint("stat_std_score", func(x, m, sd int) { guard(func() { _ = stat.StdScore(float64(x), float64(m), float64(sd)) }) },
		"x:range=-10..10", "m:range=-10..10", "sd:range=-3..3")
}
