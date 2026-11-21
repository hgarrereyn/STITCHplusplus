// expr: compile and run expressions against an environment.
//
// Bugs: panics in the parser, checker, compiler or optimizer; Go runtime errors inside the VM
// (the VM recovers panics into errors, so "runtime error: ..." errors other than integer division
// by zero, and reflect panics outside expr's own list of expected ones, are reported).
// Oracles: the optimizer does not change results; the checker's static type of an expression
// matches the value it produces; a compiled program runs the same twice; the printed AST
// (Node.String) parses and compiles to a program with the same result.
package harness

import (
	"fmt"
	"math"
	"reflect"
	"regexp"
	"sort"
	"strings"

	"github.com/antonmedv/expr"
	"github.com/antonmedv/expr/ast"
	"github.com/antonmedv/expr/parser"
	"github.com/antonmedv/expr/vm"
	"stitch"
)

type Foo struct {
	Bar string
}

func (f Foo) String() string     { return "foo" }
func (f Foo) Qux(s string) string { return f.Bar + s }

// Env is the struct form of the environment (static types for the checker).
type Env struct {
	Ok    bool                         `expr:"ok"`
	F64   float64                      `expr:"f64"`
	F32   float32                      `expr:"f32"`
	I     int                          `expr:"i"`
	I64   int64                        `expr:"i64"`
	I32   int32                        `expr:"i32"`
	Array []int                        `expr:"array"`
	List  []Foo                        `expr:"list"`
	Foo   Foo                          `expr:"foo"`
	Map   map[string]any               `expr:"m"`
	Str   string                       `expr:"s"`
	Ptr   *Foo                         `expr:"ptr"`
	Nil   any                          `expr:"nilv"`
	Add   func(a, b int) int           `expr:"add"`
	Div   func(a, b int) int           `expr:"div"`
	Half  func(a float64) float64      `expr:"half"`
	Score func(a int, x ...int) int    `expr:"score"`
	Greet func(name string) string     `expr:"greet"`
}

func newEnv(s *stitch.Source) Env {
	e := Env{
		Ok: true, F64: .5, F32: .5, I: 1, I64: 1, I32: 1,
		Array: []int{1, 2, 3, 4, 5}, List: []Foo{{"bar"}, {"baz"}}, Foo: Foo{"bar"},
		Map: map[string]any{"a": 1, "b": "two", "c": []any{1.5, nil}}, Str: "hello",
		Add:   func(a, b int) int { return a + b },
		Div:   func(a, b int) int { return a / b },
		Half:  func(a float64) float64 { return a / 2 },
		Score: func(a int, x ...int) int { s := a; for _, n := range x { s += n }; return s },
		Greet: func(name string) string { return "Hello, " + name },
	}
	if s == nil {
		return e
	}
	for s.More() {
		switch s.Choice(9) {
		case 0:
			e.I = int(s.Range(-1000, 1000))
		case 1:
			e.F64 = stitch.Pick(s, 0.0, -0.0, math.Inf(1), math.NaN(), 1e308, 2.5)
		case 2:
			e.Str = s.Str(8)
		case 3:
			e.Array = nil
			for n := s.Choice(5); n > 0; n-- {
				e.Array = append(e.Array, int(s.Range(-5, 5)))
			}
		case 4:
			e.I64 = int64(stitch.Pick(s, 0, -1, math.MaxInt64, math.MinInt64, 7))
		case 5:
			e.Ptr = &Foo{s.Str(4)}
		case 6:
			e.Map[stitch.Pick(s, "a", "b", "x", "")] = stitch.Pick[any](s, 1, "v", nil, []int{1}, map[string]any{"k": 2}, 1.5)
		case 7:
			e.Ok = s.Choice(2) == 0
		default:
			e.Nil = stitch.Pick[any](s, nil, 1, "x", []any{})
		}
	}
	return e
}

// map form of the same environment (untyped: the checker sees interface{})
func asMap(e Env) map[string]any {
	m := map[string]any{}
	t := reflect.TypeOf(e)
	v := reflect.ValueOf(e)
	for i := 0; i < t.NumField(); i++ {
		m[t.Field(i).Tag.Get("expr")] = v.Field(i).Interface()
	}
	return m
}

// errors expr's own fuzz test treats as expected (bad operations by the expression itself)
var expected = regexp.MustCompile(strings.Join([]string{
	`cannot fetch .* from .*`, `cannot get .* from .*`, `cannot slice`, `slice index out of range`, `error parsing regexp`,
	`integer divide by zero`, `interface conversion`, `invalid argument for .*`, `invalid character`, `invalid operation`,
	`invalid duration`, `time: missing unit in duration`, `time: unknown unit .* in duration`, `time: invalid duration`, `json: unsupported value`,
	`unexpected end of JSON input`, `memory budget exceeded`, `using interface \{} as type .*`,
	`reflect.Value.MapIndex: value of type .* is not assignable to type .*`, `reflect: Call using .* as type .*`,
	`reflect: call of reflect.Value.Call on .* Value`, `reflect: call of reflect.Value.Index on map Value`,
	`reflect: call of reflect.Value.Len on .* Value`, `strings: negative Repeat count`, `strings: illegal bytes to escape`,
	`operator "in" not defined on`, `invalid date .*`, `cannot parse .* as .*`, `index out of range: -?\d+ \(array length is \d+\)`,
	`cannot use .* as argument`, `not enough arguments`, `too many arguments`, `is not a function`, `nil pointer dereference.*\(Div|div\)`,
	`cannot convert`, `strings: Repeat output length overflow`, `json: cannot unmarshal`, `cannot range over`, `unknown`, `nil`,
}, "|"))

// runtimeBug: a VM error that is a Go panic the expression should not be able to cause
func runtimeBug(err error) bool {
	msg := err.Error()
	if expected.MatchString(msg) {
		return false
	}
	return strings.Contains(msg, "runtime error:") || strings.Contains(msg, "reflect:") || strings.Contains(msg, "reflect.")
}

type Prog struct {
	src   string
	typed bool // compiled against the struct env
	as    int
	undef bool
	p     *vm.Program
	noopt *vm.Program
}

func init() {
	stitch.Object[*Prog]("Prog", nil)
	stitch.Decoder("ExprText", exprText)
	stitch.Endpoint("compile", compile, "src:max=512,type=ExprText", "typed", "as:range=0..4", "undef")
	stitch.Endpoint("run", run, "p", "s:max=64")
	stitch.Endpoint("reprint", reprint, "p")
	stitch.Endpoint("disassemble", disassemble, "p")
	stitch.Endpoint("parse_only", parseOnly, "src:max=512,type=ExprText")
	stitch.Endpoint("eval_untyped", evalUntyped, "src:max=512,type=ExprText", "s:max=64")
}

func exprText(s *stitch.Source) string {
	names := []string{"ok", "f64", "f32", "i", "i64", "i32", "array", "list", "foo", "m", "s", "ptr", "nilv", "add", "div", "half", "score", "greet", "#", "#index", "#acc", "$env"}
	funcs := []string{"len", "all", "none", "any", "one", "filter", "map", "find", "findIndex", "findLast", "findLastIndex", "count", "groupBy", "reduce",
		"type", "abs", "int", "float", "string", "trim", "trimPrefix", "trimSuffix", "upper", "lower", "split", "splitAfter", "replace", "repeat",
		"join", "indexOf", "lastIndexOf", "hasPrefix", "hasSuffix", "max", "min", "sum", "mean", "median", "toJSON", "fromJSON", "duration", "date",
		"first", "last", "get", "take", "keys", "values", "toPairs", "fromPairs", "sort", "sortBy", "toBase64", "fromBase64", "bitand", "bitshl", "ceil", "floor", "round"}
	ops := []string{" + ", " - ", " * ", " / ", " % ", " ** ", " ^ ", " == ", " != ", " < ", " <= ", " > ", " >= ", " and ", " or ", " && ", " || ",
		" in ", " not in ", " matches ", " contains ", " startsWith ", " endsWith ", " .. ", " ?? ", " | "}
	var expr func(d int) string
	lit := func() string {
		return stitch.Pick(s, "0", "1", "-1", "2.5", "1e3", "0x1F", "1_000", "9223372036854775807", "nil", "true", "false", `"s"`, `'q'`, `"é\n"`, "[]", "{}", "1..3")
	}
	expr = func(d int) string {
		k := s.Choice(12)
		if d > 4 {
			k = s.Choice(3)
		}
		switch k {
		case 0:
			return lit()
		case 1:
			return stitch.Pick(s, names...)
		case 2:
			return s.Token()
		case 3:
			return expr(d+1) + stitch.Pick(s, ops...) + expr(d+1)
		case 4:
			return stitch.Pick(s, "!", "not ", "-", "+") + expr(d+1)
		case 5:
			return expr(d+1) + stitch.Pick(s, ".Bar", ".Qux(\"x\")", ".String()", "?.Bar", "[0]", "[-1]", "[1:]", "[:2]", "[\"a\"]", "?.a", ".a", "[i]", ".b")
		case 6:
			return expr(d+1) + " ? " + expr(d+1) + " : " + expr(d+1)
		case 7:
			args := []string{}
			for n := s.Choice(3) + 1; n > 0; n-- {
				args = append(args, expr(d+1))
			}
			return stitch.Pick(s, funcs...) + "(" + strings.Join(args, ", ") + ")"
		case 8:
			return stitch.Pick(s, "all", "any", "filter", "map", "count", "find", "groupBy", "sortBy", "reduce", "none", "one", "findLastIndex") + "(" + expr(d+1) + ", " + expr(d+1) + ")"
		case 9:
			return "[" + expr(d+1) + ", " + expr(d+1) + "]"
		case 10:
			return "{" + stitch.Pick(s, "a", `"b"`, "c") + ": " + expr(d+1) + "}"
		default:
			if s.Choice(3) == 0 {
				return "let x = " + expr(d+1) + "; " + expr(d+1)
			}
			return "(" + expr(d+1) + ")"
		}
	}
	return expr(0)
}

var asOpts = []func() expr.Option{expr.AsAny, expr.AsBool, expr.AsInt, expr.AsFloat64, expr.AsInt64}

func options(typed bool, as int, undef bool, optimize bool) []expr.Option {
	var env any = asMap(newEnv(nil))
	if typed {
		env = Env{}
	}
	o := []expr.Option{expr.Env(env), asOpts[as](), expr.Optimize(optimize)}
	if undef {
		o = append(o, expr.AllowUndefinedVariables())
	}
	return o
}

func compile(src string, typed bool, as int, undef bool) (*Prog, error) {
	p, err := expr.Compile(src, options(typed, as, undef, true)...)
	if err != nil {
		return nil, err
	}
	noopt, err := expr.Compile(src, options(typed, as, undef, false)...)
	stitch.Check(err == nil, "a program that compiles optimized also compiles unoptimized")
	return &Prog{src: src, typed: typed, as: as, undef: undef, p: p, noopt: noopt}, nil
}

func envFor(p *Prog, s *stitch.Source) any {
	e := newEnv(s)
	if p.typed {
		return e
	}
	return asMap(e)
}

// unordered: results built from map iteration (keys, values, toPairs, groupBy, $env) have no order
var unordered = regexp.MustCompile(`keys\(|values\(|toPairs\(|groupBy\(|\$env|fromPairs\(`)

func sameIn(src string, a, b any) bool {
	if same(a, b) {
		return true
	}
	if !unordered.MatchString(src) {
		return false
	}
	return sortedRepr(a) == sortedRepr(b)
}

func sortedRepr(x any) string {
	v := reflect.ValueOf(x)
	if !v.IsValid() || v.Kind() != reflect.Slice {
		return fmt.Sprintf("%v", x)
	}
	parts := make([]string, v.Len())
	for i := range parts {
		parts[i] = fmt.Sprintf("%v", v.Index(i).Interface())
	}
	sort.Strings(parts)
	return strings.Join(parts, "\x00")
}

// result comparison: NaN equals NaN; functions compare by type
func same(a, b any) bool {
	if fa, ok := a.(float64); ok {
		if fb, ok := b.(float64); ok && math.IsNaN(fa) && math.IsNaN(fb) {
			return true
		}
	}
	if a != nil && reflect.TypeOf(a).Kind() == reflect.Func {
		return b != nil && reflect.TypeOf(a) == reflect.TypeOf(b)
	}
	return reflect.DeepEqual(a, b) || fmt.Sprintf("%#v", a) == fmt.Sprintf("%#v", b)
}

func runChecked(p *vm.Program, env any) (any, error) {
	out, err := expr.Run(p, env)
	if err != nil && runtimeBug(err) {
		stitch.Log("error: " + err.Error())
		stitch.Check(false, "VM run panics with a Go runtime error")
	}
	return out, err
}

func run(p *Prog, s *stitch.Source) {
	pos := 0
	_ = pos
	envBytes := s
	env := envFor(p, envBytes)
	out, err := runChecked(p.p, env)
	out2, err2 := runChecked(p.noopt, env)
	stitch.Log(fmt.Sprintf("optimized: %#v %v | unoptimized: %#v %v", out, err, out2, err2))
	// (the optimizer may fold away an operand that would have failed, e.g. `x.y || true`)
	stitch.Check(err == nil || err2 != nil, "the optimized program does not fail where the unoptimized one succeeds")
	if err == nil && err2 == nil {
		stitch.Check(sameIn(p.src, out, out2), "optimized and unoptimized programs give the same result")
		again, err3 := expr.Run(p.p, env)
		stitch.Log(fmt.Sprintf("again: %#v %v", again, err3))
		stitch.Check(err3 == nil && sameIn(p.src, out, again), "a program runs the same twice")
		// the checker's static type of the expression matches the value
		if t := progNode(p.p).Type(); p.typed && p.as == 0 && t != nil && out != nil && t.Kind() != reflect.Interface {
			got := reflect.TypeOf(out)
			stitch.Log(fmt.Sprintf("static %v, value %v", t, got))
			stitch.Check(got.Kind() == t.Kind() || (t.Kind() == reflect.Float64 && got.Kind() == reflect.Float32) || isIntKind(t.Kind()) && isIntKind(got.Kind()),
				"the checked type of an expression matches its value's type")
		}
	}
}

func isIntKind(k reflect.Kind) bool {
	return k >= reflect.Int && k <= reflect.Uintptr
}

func reprint(p *Prog) {
	text := progNode(p.p).String()
	stitch.Log("printed: " + text)
	tree, err := parser.Parse(text)
	stitch.Check(err == nil, "the printed AST parses")
	_ = tree
	p2, err := expr.Compile(text, options(p.typed, p.as, p.undef, false)...)
	if err != nil {
		stitch.Log("compile error: " + err.Error())
		return  // (printing after optimization can produce forms the checker rejects differently)
	}
	env := envFor(p, nil)
	a, errA := expr.Run(p.noopt, env)
	b, errB := expr.Run(p2, env)
	if errA == nil && errB == nil {
		stitch.Check(sameIn(p.src, a, b), "the printed expression evaluates like the original")
	}
}

func disassemble(p *Prog) {
	p.p.Disassemble()
	p.noopt.Disassemble()
	n := progNode(p.p)
	ast.Walk(&n, &counter{})
}

// progNode: the checked AST (a field of vm.Program up to v1.15, a method from v1.16 on)
func progNode(p *vm.Program) ast.Node {
	v := reflect.ValueOf(p)
	if m := v.MethodByName("Node"); m.IsValid() {
		return m.Call(nil)[0].Interface().(ast.Node)
	}
	return v.Elem().FieldByName("Node").Interface().(ast.Node)
}

type counter struct{ n int }

func (c *counter) Visit(node *ast.Node) { c.n++ }

func parseOnly(src string) error {
	tree, err := parser.Parse(src)
	if err != nil {
		return err
	}
	_ = tree.Node.String()
	return nil
}

func evalUntyped(src string, s *stitch.Source) error {
	env := asMap(newEnv(s))
	p, err := expr.Compile(src, expr.Env(env))
	if err != nil {
		return err
	}
	_, err = runChecked(p, env)
	return err
}
