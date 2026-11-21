// gonum harness: vectors, matrices and graphs, with stubs for floats, stat, distuv, mathext,
// combin, mat, graph, num (quaternions, dual numbers), spatial, interp and integrate.
//
// Bugs: Go runtime panics (index out of range, nil dereference, integer divide by zero, ...) and
// failed checks. gonum panics with its own messages on documented misuse ("mat: dimension
// mismatch", "floats: length mismatch", ...): those bail.
package harness

import (
	"fmt"
	"math"
	"runtime"
	"sort"
	"strings"

	"gonum.org/v1/gonum/mat"
	"stitch"
)

// Vec is a []float64; Mat any gonum matrix.
type Vec struct{ v []float64 }
type Mat struct{ m mat.Matrix }

func init() {
	stitch.Object[*Vec]("Vec", nil)
	stitch.Object[*Mat]("Mat", nil)
	stitch.Endpoint("vec", newVec, "s:max=128")
	stitch.Endpoint("vec_len", vecLen, "n:range=0..12", "s:max=64")
	stitch.Endpoint("vec_copy", func(v *Vec) *Vec { return &Vec{append([]float64(nil), v.v...)} }, "v")
}

// guard runs f; panics from gonum's documented checks bail, Go runtime errors stay bugs.
func guard(f func()) {
	defer func() {
		r := recover()
		if r == nil {
			return
		}
		if _, ok := r.(runtime.Error); ok {
			panic(r)
		}
		if strings.HasPrefix(fmt.Sprintf("%T", r), "stitch.") {
			panic(r) // a bail or a failed check of the harness itself
		}
		stitch.Bail(fmt.Sprint("gonum panic: ", r))
	}()
	f()
}

func value(s *stitch.Source) float64 {
	switch s.Choice(6) {
	case 0:
		return float64(s.Range(-10, 10))
	case 1:
		return stitch.Pick(s, 0, math.Copysign(0, -1), 0.5, -1.5, 1e-300, 1e300, math.MaxFloat64, math.SmallestNonzeroFloat64, math.Inf(1), math.Inf(-1), math.NaN(), 1/3.0)
	case 2:
		return float64(s.Range(-1000, 1000)) / float64(s.Range(1, 64))
	default:
		return float64(s.Range(0, 9))
	}
}

func newVec(s *stitch.Source) *Vec {
	var v []float64
	for s.More() && len(v) < 16 {
		v = append(v, value(s))
	}
	return &Vec{v}
}

func vecLen(n int, s *stitch.Source) *Vec {
	v := make([]float64, n)
	for i := range v {
		v[i] = value(s)
	}
	return &Vec{v}
}

// sortedKeys: registration order must be the same in every process
func sortedKeys[T any](m map[string]T) []string {
	ks := make([]string, 0, len(m))
	for k := range m {
		ks = append(ks, k)
	}
	sort.Strings(ks)
	return ks
}

func finite(v []float64) bool {
	for _, x := range v {
		if math.IsNaN(x) || math.IsInf(x, 0) {
			return false
		}
	}
	return true
}

func near(a, b, tol float64) bool {
	if math.IsNaN(a) && math.IsNaN(b) {
		return true
	}
	if math.IsInf(a, 0) || math.IsInf(b, 0) {
		return a == b
	}
	return math.Abs(a-b) <= tol*math.Max(1, math.Max(math.Abs(a), math.Abs(b)))
}

func sameLen(a, b *Vec) {
	stitch.Require(len(a.v) == len(b.v), "length mismatch")
}

func nonEmpty(a *Vec) {
	stitch.Require(len(a.v) > 0, "empty")
}
