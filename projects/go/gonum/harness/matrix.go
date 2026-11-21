package harness

import (
	"math"

	"gonum.org/v1/gonum/floats"
	"gonum.org/v1/gonum/mat"
	"stitch"
)

func entries(s *stitch.Source, n int) []float64 {
	d := make([]float64, n)
	for i := range d {
		if s.Choice(8) == 0 {
			d[i] = value(s)
		} else {
			d[i] = float64(s.Range(-5, 5))
		}
	}
	return d
}

func dense(m mat.Matrix) *mat.Dense {
	var d mat.Dense
	d.CloneFrom(m)
	return &d
}

func dims(m *Mat) (int, int) { return m.m.Dims() }

func square(m *Mat) int {
	r, c := dims(m)
	stitch.Require(r == c && r > 0, "not square")
	return r
}

func finiteMat(m mat.Matrix) bool {
	r, c := m.Dims()
	for i := 0; i < r; i++ {
		for j := 0; j < c; j++ {
			v := m.At(i, j)
			if math.IsNaN(v) || math.IsInf(v, 0) {
				return false
			}
		}
	}
	return true
}

// moderate: finite entries of magnitude at most 1e6 (reconstruction checks are meaningless when
// entries span hundreds of orders of magnitude)
func moderate(m mat.Matrix) bool {
	r, c := m.Dims()
	for i := 0; i < r; i++ {
		for j := 0; j < c; j++ {
			if v := m.At(i, j); math.IsNaN(v) || math.Abs(v) > 1e6 {
				return false
			}
		}
	}
	return true
}

func approxEq(a, b mat.Matrix, tol float64) bool {
	ra, ca := a.Dims()
	rb, cb := b.Dims()
	if ra != rb || ca != cb {
		return false
	}
	scale := math.Max(1, mat.Norm(a, math.Inf(1)))
	return mat.EqualApprox(a, b, tol*scale)
}

func init() {
	// constructors for each matrix type
	stitch.Endpoint("mat_dense", func(r, c int, s *stitch.Source) *Mat {
		return &Mat{mat.NewDense(r, c, entries(s, r*c))}
	}, "r:range=1..5", "c:range=1..5", "s:max=64")
	stitch.Endpoint("mat_sym", func(n int, s *stitch.Source) *Mat {
		d := entries(s, n*n)
		m := mat.NewSymDense(n, nil)
		for i := 0; i < n; i++ {
			for j := i; j < n; j++ {
				m.SetSym(i, j, d[i*n+j])
			}
		}
		return &Mat{m}
	}, "n:range=1..5", "s:max=64")
	stitch.Endpoint("mat_spd", func(n int, s *stitch.Source) *Mat {
		a := mat.NewDense(n, n, entries(s, n*n))
		var m mat.SymDense
		m.SymOuterK(1, a)
		for i := 0; i < n; i++ {
			m.SetSym(i, i, m.At(i, i)+1)
		}
		return &Mat{&m}
	}, "n:range=1..5", "s:max=64")
	stitch.Endpoint("mat_tri", func(n int, upper bool, s *stitch.Source) *Mat {
		k := mat.Lower
		if upper {
			k = mat.Upper
		}
		m := mat.NewTriDense(n, k, nil)
		d := entries(s, n*n)
		for i := 0; i < n; i++ {
			for j := 0; j < n; j++ {
				if (upper && j >= i) || (!upper && j <= i) {
					m.SetTri(i, j, d[i*n+j])
				}
			}
		}
		return &Mat{m}
	}, "n:range=1..5", "upper", "s:max=64")
	stitch.Endpoint("mat_band", func(r, c, kl, ku int, s *stitch.Source) *Mat {
		var m *mat.BandDense
		guard(func() { m = mat.NewBandDense(r, c, kl, ku, entries(s, r*(kl+ku+1))) })
		return &Mat{m}
	}, "r:range=1..5", "c:range=1..5", "kl:range=0..3", "ku:range=0..3", "s:max=64")
	stitch.Endpoint("mat_diag", func(n int, s *stitch.Source) *Mat { return &Mat{mat.NewDiagDense(n, entries(s, n))} }, "n:range=1..5", "s:max=16")
	stitch.Endpoint("mat_vec", func(v *Vec) *Mat {
		stitch.Require(len(v.v) > 0, "empty")
		return &Mat{mat.NewVecDense(len(v.v), append([]float64(nil), v.v...))}
	}, "v")
	stitch.Endpoint("mat_identity", func(n int) *Mat {
		d := mat.NewDiagDense(n, nil)
		for i := 0; i < n; i++ {
			d.SetDiag(i, 1)
		}
		return &Mat{d}
	}, "n:range=1..5")

	// Dense operations into a new matrix
	ops2 := map[string]func(d *mat.Dense, a, b mat.Matrix){
		"mat_add":       func(d *mat.Dense, a, b mat.Matrix) { d.Add(a, b) },
		"mat_sub":       func(d *mat.Dense, a, b mat.Matrix) { d.Sub(a, b) },
		"mat_mul_elem":  func(d *mat.Dense, a, b mat.Matrix) { d.MulElem(a, b) },
		"mat_div_elem":  func(d *mat.Dense, a, b mat.Matrix) { d.DivElem(a, b) },
		"mat_mul":       func(d *mat.Dense, a, b mat.Matrix) { d.Mul(a, b) },
		"mat_kronecker": func(d *mat.Dense, a, b mat.Matrix) { d.Kronecker(a, b) },
		"mat_stack":     func(d *mat.Dense, a, b mat.Matrix) { d.Stack(a, b) },
		"mat_augment":   func(d *mat.Dense, a, b mat.Matrix) { d.Augment(a, b) },
		"mat_solve": func(d *mat.Dense, a, b mat.Matrix) {
			if err := d.Solve(a, b); err == nil && finiteMat(d) {
				var chk mat.Dense
				chk.Mul(a, d)
				r, c := a.Dims()
				if r == c && mat.Cond(a, 1) < 1e8 {
					stitch.Check(approxEq(&chk, b, 1e-6), "Solve: A X == B")
				}
			}
		},
	}
	for _, name := range sortedKeys(ops2) {
		op := ops2[name]
		stitch.Endpoint(name, func(a, b *Mat) *Mat {
			var d mat.Dense
			guard(func() { op(&d, a.m, b.m) })
			stitch.Require(!d.IsEmpty(), "empty result")
			return &Mat{&d}
		}, "a", "b")
	}

	ops1 := map[string]func(d *mat.Dense, a mat.Matrix, s *stitch.Source){
		"mat_transpose": func(d *mat.Dense, a mat.Matrix, _ *stitch.Source) {
			d.CloneFrom(a.T())
			if finiteMat(a) {
				stitch.Check(mat.Equal(d.T(), a), "transpose twice is the identity")
			}
		},
		"mat_scale": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) { d.Scale(value(s), a) },
		"mat_exp":   func(d *mat.Dense, a mat.Matrix, _ *stitch.Source) { d.Exp(a) },
		"mat_pow": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			n := int(s.Range(0, 5))
			d.Pow(a, n)
			var m mat.Dense
			if r, c := a.Dims(); r == c && finiteMat(a) {
				m.CloneFrom(eye(r))
				for i := 0; i < n; i++ {
					m.Mul(&m, a)
				}
				stitch.Check(!moderate(a) || approxEq(d, &m, 1e-9), "Pow(a, n) == a * ... * a")
			}
		},
		"mat_inverse": func(d *mat.Dense, a mat.Matrix, _ *stitch.Source) {
			if err := d.Inverse(a); err == nil && finiteMat(a) && mat.Cond(a, 1) < 1e8 {
				var m mat.Dense
				m.Mul(a, d)
				r, _ := a.Dims()
				stitch.Check(approxEq(&m, eye(r), 1e-6), "A * Inverse(A) == I")
			}
		},
		"mat_apply":  func(d *mat.Dense, a mat.Matrix, _ *stitch.Source) { d.Apply(func(i, j int, v float64) float64 { return v*float64(i) - float64(j) }, a) },
		"mat_slice": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			r, c := a.Dims()
			i, j := int(s.Range(0, int64(r))), int(s.Range(0, int64(c)))
			d.CloneFrom(dense(a).Slice(i, r, j, c))
		},
		"mat_grow": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) { d.CloneFrom(dense(a).Grow(int(s.Range(0, 3)), int(s.Range(0, 3)))) },
		"mat_outer": func(d *mat.Dense, a mat.Matrix, _ *stitch.Source) {
			r, c := a.Dims()
			x := mat.NewVecDense(r, nil)
			y := mat.NewVecDense(c, nil)
			x.CopyVec(mat.NewVecDense(r, colOf(a, 0)))
			y.CopyVec(mat.NewVecDense(c, rowOf(a, 0)))
			d.Outer(2, x, y)
			d.RankOne(a, -1, x, y)
		},
		"mat_copy_set": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			d.CloneFrom(a)
			r, c := a.Dims()
			d.SetRow(int(s.Range(0, int64(r-1))), make([]float64, c))
			d.SetCol(int(s.Range(0, int64(c-1))), make([]float64, r))
			_ = d.DiagView()
			_ = d.ColView(0)
			_ = d.RowView(0)
		},
		"mat_sym_ops": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			sm, ok := a.(*mat.SymDense)
			stitch.Require(ok, "not symmetric")
			var r mat.SymDense
			r.AddSym(sm, sm)
			r.ScaleSym(0.5, &r)
			if finiteMat(sm) && mat.Norm(sm, math.Inf(1)) < 1e300 {
				stitch.Check(mat.EqualApprox(&r, sm, 1e-12), "(S + S) / 2 == S")
			}
			r.SymRankOne(sm, 1, mat.NewVecDense(sm.SymmetricDim(), nil))
			r.SubsetSym(sm, []int{0})
			d.CloneFrom(&r)
		},
		"mat_tri_ops": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			t, ok := a.(*mat.TriDense)
			stitch.Require(ok, "not triangular")
			var r mat.TriDense
			if err := r.InverseTri(t); err == nil && finiteMat(t) {
				var m mat.Dense
				m.Mul(t, &r)
				n, _ := t.Dims()
				if mat.Cond(t, 1) < 1e8 {
					stitch.Check(approxEq(&m, eye(n), 1e-6), "T * InverseTri(T) == I")
				}
			}
			r.MulTri(t, t)
			d.CloneFrom(&r)
		},
		"mat_band_ops": func(d *mat.Dense, a mat.Matrix, s *stitch.Source) {
			b, ok := a.(*mat.BandDense)
			stitch.Require(ok, "not banded")
			var x mat.VecDense
			_, c := b.Dims()
			x.MulVec(b, mat.NewVecDense(c, entries(s, c)))
			var dd mat.Dense
			dd.CloneFrom(b)
			var y mat.VecDense
			y.MulVec(&dd, mat.NewVecDense(c, rawVec(&x, c)))
			_ = b.DiagView()
			_ = b.TBand()
			d.CloneFrom(b)
		},
	}
	for _, name := range sortedKeys(ops1) {
		op := ops1[name]
		stitch.Endpoint(name, func(a *Mat, s *stitch.Source) *Mat {
			var d mat.Dense
			guard(func() { op(&d, a.m, s) })
			stitch.Require(!d.IsEmpty(), "empty result")
			return &Mat{&d}
		}, "a", "s:max=8")
	}

	// scalar functions of a matrix
	scalars := map[string]func(a mat.Matrix){
		"mat_det": func(a mat.Matrix) {
			d := mat.Det(a)
			ld, sign := mat.LogDet(a)
			if !math.IsNaN(d) && d != 0 && !math.IsInf(d, 0) && finiteMat(a) {
				stitch.Check(near(math.Log(math.Abs(d)), ld, 1e-6) && sign == math.Copysign(1, d), "LogDet agrees with Det")
				stitch.Check(near(mat.Det(a.T()), d, 1e-6), "det(A') == det(A)")
			}
		},
		"mat_trace": func(a mat.Matrix) { _ = mat.Trace(a) },
		"mat_norms": func(a mat.Matrix) {
			n1, ni, nf := mat.Norm(a, 1), mat.Norm(a, math.Inf(1)), mat.Norm(a, 2)
			if finiteMat(a) {
				stitch.Check(n1 >= 0 && ni >= 0 && nf >= 0, "norms are non-negative")
				stitch.Check(near(mat.Norm(a.T(), 1), ni, 1e-12), "||A'||_1 == ||A||_inf")
			}
		},
		"mat_cond":   func(a mat.Matrix) { _ = mat.Cond(a, 2); _ = mat.Cond(a, 1) },
		"mat_sum_max": func(a mat.Matrix) {
			if finiteMat(a) {
				stitch.Check(mat.Max(a) >= mat.Min(a), "Max >= Min")
				r, c := a.Dims()
				stitch.Check(near(mat.Sum(a), sumAll(a, r, c), 1e-9), "Sum")
			}
		},
		"mat_formatted": func(a mat.Matrix) { _ = mat.Formatted(a, mat.Squeeze(), mat.Excerpt(2)) },
		"mat_dot":       func(a mat.Matrix) { r, c := a.Dims(); v := mat.NewVecDense(r*c, flat(a)); _ = mat.Dot(v, v) },
		"mat_col_row":   func(a mat.Matrix) { _ = mat.Col(nil, 0, a); _ = mat.Row(nil, 0, a) },
		"mat_marshal": func(a mat.Matrix) {
			d := dense(a)
			b, err := d.MarshalBinary()
			if err == nil {
				var e mat.Dense
				stitch.Check(e.UnmarshalBinary(b) == nil && mat.Equal(&e, d) || !finiteMat(d), "MarshalBinary round trip")
			}
		},
	}
	for _, name := range sortedKeys(scalars) {
		fn := scalars[name]
		stitch.Endpoint(name, func(a *Mat) { guard(func() { fn(a.m) }) }, "a")
	}

	// decompositions: each reconstructs its input
	decomps := map[string]func(a *Mat){
		"lu": func(a *Mat) {
			n := square(a)
			var lu mat.LU
			lu.Factorize(a.m)
			var l, u mat.TriDense
			lu.LTo(&l)
			lu.UTo(&u)
			var p mat.Dense
			var pm mat.Dense
			pv := lu.Pivot(nil)
			p.CloneFrom(perm(pv))
			pm.Mul(&p, a.m)
			var lu2 mat.Dense
			lu2.Mul(&l, &u)
			if finiteMat(a.m) {
				stitch.Check(approxEq(&lu2, &pm, 1e-9) || approxEq(&lu2, mulT(&p, a.m), 1e-9), "LU: P A == L U")
				stitch.Check(near(lu.Det(), mat.Det(a.m), 1e-6), "LU.Det == Det")
			}
			var x mat.VecDense
			b := mat.NewVecDense(n, nil)
			for i := 0; i < n; i++ {
				b.SetVec(i, float64(i+1))
			}
			if err := lu.SolveVecTo(&x, false, b); err == nil && lu.Cond() < 1e8 {
				var chk mat.VecDense
				chk.MulVec(a.m, &x)
				stitch.Check(mat.EqualApprox(&chk, b, 1e-6*math.Max(1, mat.Norm(a.m, math.Inf(1)))), "LU solve: A x == b")
			}
		},
		"cholesky": func(a *Mat) {
			sm, ok := a.m.(*mat.SymDense)
			stitch.Require(ok, "not symmetric")
			var ch mat.Cholesky
			if !ch.Factorize(sm) {
				stitch.Bail("not positive definite")
			}
			var l mat.TriDense
			ch.LTo(&l)
			var r mat.Dense
			r.Mul(&l, l.T())
			stitch.Check(!moderate(sm) || approxEq(&r, sm, 1e-9), "Cholesky: L L' == A")
			var inv mat.SymDense
			if err := ch.InverseTo(&inv); err == nil && ch.Cond() < 1e8 {
				var m mat.Dense
				m.Mul(sm, &inv)
				stitch.Check(approxEq(&m, eye(sm.SymmetricDim()), 1e-6), "Cholesky inverse")
			}
			stitch.Check(!moderate(sm) || near(ch.Det(), mat.Det(sm), 1e-6), "Cholesky.Det == Det")
			x := mat.NewVecDense(sm.SymmetricDim(), nil)
			x.SetVec(0, 1)
			var up mat.Cholesky
			if up.SymRankOne(&ch, 1, x) {
				var s2 mat.SymDense
				up.ToSym(&s2)
				var want mat.SymDense
				want.SymRankOne(sm, 1, x)
				stitch.Check(!moderate(sm) || approxEq(&s2, &want, 1e-9), "Cholesky.SymRankOne == SymDense.SymRankOne")
			}
		},
		"qr": func(a *Mat) {
			var qr mat.QR
			qr.Factorize(a.m)
			var q, r, m mat.Dense
			qr.QTo(&q)
			qr.RTo(&r)
			m.Mul(&q, &r)
			if finiteMat(a.m) {
				stitch.Check(approxEq(&m, a.m, 1e-9), "QR: Q R == A")
			}
		},
		"lq": func(a *Mat) {
			var lq mat.LQ
			lq.Factorize(a.m)
			var l, q, m mat.Dense
			lq.LTo(&l)
			lq.QTo(&q)
			m.Mul(&l, &q)
			if finiteMat(a.m) {
				stitch.Check(!moderate(a.m) || approxEq(&m, a.m, 1e-9), "LQ: L Q == A")
			}
		},
		"svd": func(a *Mat) {
			var svd mat.SVD
			if !svd.Factorize(a.m, mat.SVDFull) {
				stitch.Bail("SVD did not converge")
			}
			var u, v mat.Dense
			svd.UTo(&u)
			svd.VTo(&v)
			vals := svd.Values(nil)
			r, c := a.m.Dims()
			sig := mat.NewDense(r, c, nil)
			for i, x := range vals {
				sig.Set(i, i, x)
			}
			var m, m2 mat.Dense
			m.Mul(&u, sig)
			m2.Mul(&m, v.T())
			if finiteMat(a.m) {
				stitch.Check(approxEq(&m2, a.m, 1e-9), "SVD: U S V' == A")
				for i := 1; i < len(vals); i++ {
					stitch.Check(vals[i] <= vals[i-1]+1e-12, "singular values are sorted")
				}
				stitch.Check(svd.Rank(1e-12) <= len(vals), "rank <= number of singular values")
			}
		},
		"eigen_sym": func(a *Mat) {
			sm, ok := a.m.(*mat.SymDense)
			stitch.Require(ok, "not symmetric")
			var es mat.EigenSym
			if !es.Factorize(sm, true) {
				stitch.Bail("did not converge")
			}
			vals := es.Values(nil)
			var vecs mat.Dense
			es.VectorsTo(&vecs)
			n := sm.SymmetricDim()
			for i := 0; i < n; i++ {
				v := vecs.ColView(i)
				var av, lv mat.VecDense
				av.MulVec(sm, v)
				lv.ScaleVec(vals[i], v)
				if finiteMat(sm) {
					stitch.Check(mat.EqualApprox(&av, &lv, 1e-6*math.Max(1, mat.Norm(sm, 1))), "EigenSym: A v == lambda v")
				}
			}
			stitch.Check(!moderate(sm) || near(floats.Sum(vals), mat.Trace(sm), 1e-6*mat.Norm(sm, 1)), "sum of eigenvalues == trace")
		},
		"eigen": func(a *Mat) {
			square(a)
			var e mat.Eigen
			if !e.Factorize(a.m, mat.EigenRight) {
				stitch.Bail("did not converge")
			}
			vals := e.Values(nil)
			var vecs mat.CDense
			e.VectorsTo(&vecs)
			sum := 0.0
			for _, v := range vals {
				sum += real(v)
			}
			if finiteMat(a.m) {
				stitch.Check(!moderate(a.m) || near(sum, mat.Trace(a.m), 1e-6*mat.Norm(a.m, 1)), "sum of eigenvalues == trace")
			}
		},
		"gsvd": func(a *Mat) {
			var g mat.GSVD
			b := dense(a.m)
			if !g.Factorize(a.m, b, mat.GSVDAll) {
				stitch.Bail("did not converge")
			}
			_ = g.ValuesA(nil)
			_ = g.ValuesB(nil)
			var u mat.Dense
			g.UTo(&u)
		},
		"hogsvd": func(a *Mat) {
			var h mat.HOGSVD
			b := dense(a.m)
			if !h.Factorize(a.m, b) {
				stitch.Bail("did not converge")
			}
			_ = h.Values(nil, 0)
		},
		"band_cholesky": func(a *Mat) {
			sm, ok := a.m.(*mat.SymDense)
			stitch.Require(ok, "not symmetric")
			n := sm.SymmetricDim()
			sb := mat.NewSymBandDense(n, 1, nil)
			for i := 0; i < n; i++ {
				for j := i; j < n && j <= i+1; j++ {
					sb.SetSymBand(i, j, sm.At(i, j))
				}
			}
			var bc mat.BandCholesky
			if bc.Factorize(sb) {
				stitch.Check(!moderate(sb) || approxEq(&bc, sb, 1e-9), "BandCholesky reproduces the factorized matrix")
				stitch.Check(!moderate(sb) || near(bc.Det(), mat.Det(sb), 1e-6), "BandCholesky.Det == Det")
			}
		},
	}
	for _, name := range sortedKeys(decomps) {
		fn := decomps[name]
		stitch.Endpoint("mat_"+name, func(a *Mat) { guard(func() { fn(a) }) }, "a")
	}
}

func eye(n int) *mat.Dense {
	d := mat.NewDense(n, n, nil)
	for i := 0; i < n; i++ {
		d.Set(i, i, 1)
	}
	return d
}

func perm(p []int) *mat.Dense {
	n := len(p)
	d := mat.NewDense(n, n, nil)
	for i, j := range p {
		d.Set(i, j, 1)
	}
	return d
}

func mulT(p *mat.Dense, a mat.Matrix) *mat.Dense {
	var m mat.Dense
	m.Mul(p.T(), a)
	return &m
}

func colOf(a mat.Matrix, j int) []float64 { r, _ := a.Dims(); return mat.Col(make([]float64, r), j, a) }
func rowOf(a mat.Matrix, i int) []float64 { _, c := a.Dims(); return mat.Row(make([]float64, c), i, a) }

func rawVec(v *mat.VecDense, n int) []float64 {
	out := make([]float64, n)
	for i := 0; i < n && i < v.Len(); i++ {
		out[i] = v.AtVec(i)
	}
	return out
}

func sumAll(a mat.Matrix, r, c int) float64 {
	s := 0.0
	for i := 0; i < r; i++ {
		for j := 0; j < c; j++ {
			s += a.At(i, j)
		}
	}
	return s
}

func flat(a mat.Matrix) []float64 {
	r, c := a.Dims()
	out := make([]float64, 0, r*c)
	for i := 0; i < r; i++ {
		for j := 0; j < c; j++ {
			out = append(out, a.At(i, j))
		}
	}
	return out
}
