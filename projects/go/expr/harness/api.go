// The rest of the public API: compile options, Eval, the hand-built pipeline (conf, parser,
// checker, optimizer, compiler), reusable VMs, types-described environments, Program/AST/source
// accessors and file.Error values.
package harness

import (
	"bytes"
	"errors"
	"fmt"
	"reflect"
	"strings"
	_ "time/tzdata" // Timezone() loads zones; the image may have no zoneinfo

	"github.com/expr-lang/expr"
	"github.com/expr-lang/expr/ast"
	"github.com/expr-lang/expr/checker"
	"github.com/expr-lang/expr/compiler"
	"github.com/expr-lang/expr/conf"
	"github.com/expr-lang/expr/file"
	"github.com/expr-lang/expr/optimizer"
	"github.com/expr-lang/expr/parser"
	"github.com/expr-lang/expr/types"
	"github.com/expr-lang/expr/vm"
	"stitch"
)

func init() {
	stitch.Decoder("CompileOpts", compileOpts)
	stitch.Endpoint("compile_opts", compileWithOpts, "src:max=512,type=ExprText", "typed", "o:val=CompileOpts,max=64")
	stitch.Endpoint("short_circuit", shortCircuit, "p")
	stitch.Endpoint("patch_identity", patchIdentity, "p")
	stitch.Endpoint("vm_reuse", vmReuse, "a", "b", "s:max=32")
	stitch.Endpoint("program_info", programInfo, "p")
	stitch.Endpoint("pipeline", pipeline, "src:max=512,type=ExprText", "typed")
	stitch.Endpoint("eval", eval, "src:max=512,type=ExprText", "s:max=64")
	stitch.Endpoint("types_env", typesEnv, "src:max=512,type=ExprText")
}

// inspectErr: the file.Error methods on a parse/compile error; hands the error back (a bail)
func inspectErr(err error) error {
	var fe *file.Error
	if errors.As(err, &fe) {
		_ = fe.Error()
		_ = fe.Unwrap()
		_ = fe.Bind(file.NewSource("x\ny")).Error()
	}
	return err
}

// triple: a Function with overloads; ident: an untyped Function
func triple(params ...any) (any, error) {
	switch x := params[0].(type) {
	case int:
		return x * 3, nil
	case float64:
		return x * 3, nil
	case string:
		return strings.Repeat(x, 3), nil
	}
	return nil, fmt.Errorf("triple: bad argument %T", params[0])
}

func ident(params ...any) (any, error) {
	if len(params) == 0 {
		return nil, nil
	}
	return params[0], nil
}

// CompileOpts: a set of compile options, applied in the order the docs require (ConstExpr after
// the Env/Function that defines it; WarnOnAny after an As* option)
type CompileOpts struct {
	as         int // 0: none; n: asOpts[n-1]
	kind       reflect.Kind
	warnOnAny  bool
	constExpr  []string
	disableAll bool
	disabled   []string
	enabled    []string
	disableIf  bool
	maxNodes   int // -1: default
	operators  bool
	patch      bool
	tz         string
	withCtx    bool
	noShort    bool
	undef      bool
	desc       []string
}

func (o CompileOpts) String() string { return strings.Join(o.desc, ",") }

var builtinNames = []string{"len", "filter", "map", "upper", "split", "max", "sum", "toJSON", "date", "now", "duration", "keys", "sort", "int", "abs", "type", "get"}

func compileOpts(s *stitch.Source) CompileOpts {
	o := CompileOpts{maxNodes: -1}
	for s.More() {
		switch s.Choice(13) {
		case 0:
			o.as = s.Choice(5) + 1
			o.desc = append(o.desc, fmt.Sprintf("as%d", o.as))
		case 1:
			o.kind = stitch.Pick(s, reflect.Int, reflect.Float64, reflect.Bool, reflect.String, reflect.Slice, reflect.Map, reflect.Interface, reflect.Int64)
			o.desc = append(o.desc, "kind:"+o.kind.String())
		case 2:
			o.warnOnAny = true
			o.desc = append(o.desc, "warnOnAny")
		case 3:
			f := stitch.Pick(s, "add", "half", "greet", "score", "div", "triple")
			o.constExpr = append(o.constExpr, f)
			o.desc = append(o.desc, "const:"+f)
		case 4:
			o.disableAll = true
			o.desc = append(o.desc, "disableAll")
		case 5:
			f := stitch.Pick(s, builtinNames...)
			o.disabled = append(o.disabled, f)
			o.desc = append(o.desc, "disable:"+f)
		case 6:
			f := stitch.Pick(s, builtinNames...)
			o.enabled = append(o.enabled, f)
			o.desc = append(o.desc, "enable:"+f)
		case 7:
			o.disableIf = true
			o.desc = append(o.desc, "disableIf")
		case 8:
			o.maxNodes = int(stitch.Pick(s, 0, 1, 5, 20, 100))
			o.desc = append(o.desc, fmt.Sprintf("maxNodes:%d", o.maxNodes))
		case 9:
			o.operators = true
			o.desc = append(o.desc, "operators")
		case 10:
			o.patch = true
			o.desc = append(o.desc, "patch")
		case 11:
			o.tz = stitch.Pick(s, "UTC", "America/New_York", "Asia/Kolkata", "Australia/Lord_Howe", "Local")
			o.desc = append(o.desc, "tz:"+o.tz)
		default:
			switch s.Choice(3) {
			case 0:
				o.withCtx = true
				o.desc = append(o.desc, "withContext")
			case 1:
				o.noShort = true
				o.desc = append(o.desc, "noShortCircuit")
			default:
				o.undef = true
				o.desc = append(o.desc, "undef")
			}
		}
	}
	return o
}

func (o CompileOpts) build(typed, optimize bool) []expr.Option {
	var env any = asMap(newEnv(nil))
	if typed {
		env = newEnv(nil)
	}
	l := []expr.Option{expr.Env(env),
		expr.Function("triple", triple, new(func(int) int), new(func(float64) float64), new(func(string) string)),
		expr.Function("ident", ident)}
	for _, f := range o.constExpr {
		l = append(l, expr.ConstExpr(f))
	}
	if o.as > 0 {
		l = append(l, asOpts[o.as-1]())
	}
	if o.kind != reflect.Invalid {
		l = append(l, expr.AsKind(o.kind))
	}
	if o.warnOnAny && (o.as > 1 || o.kind != reflect.Invalid) { // (WarnOnAny panics without an As* option: documented)
		l = append(l, expr.WarnOnAny())
	}
	if o.disableAll {
		l = append(l, expr.DisableAllBuiltins())
	}
	for _, f := range o.disabled {
		l = append(l, expr.DisableBuiltin(f))
	}
	for _, f := range o.enabled {
		l = append(l, expr.EnableBuiltin(f))
	}
	if o.disableIf {
		l = append(l, expr.DisableIfOperator())
	}
	if o.maxNodes >= 0 {
		l = append(l, expr.MaxNodes(uint(o.maxNodes)))
	}
	if o.operators {
		l = append(l, expr.Operator("+", "addFoo"), expr.Operator("<", "lessFoo"))
	}
	if o.patch {
		l = append(l, expr.Patch(&counter{}))
	}
	if o.tz != "" {
		l = append(l, expr.Timezone(o.tz))
	}
	if o.withCtx {
		l = append(l, expr.WithContext("ctx"))
	}
	if o.noShort {
		l = append(l, expr.DisableShortCircuit())
	}
	if o.undef {
		l = append(l, expr.AllowUndefinedVariables())
	}
	return append(l, expr.Optimize(optimize))
}

func compileWithOpts(src string, typed bool, o CompileOpts) (*Prog, error) {
	p, err := expr.Compile(src, o.build(typed, true)...)
	if err != nil {
		return nil, inspectErr(err)
	}
	noopt, err := expr.Compile(src, o.build(typed, false)...)
	if err != nil {
		stitch.Log("opts %s: unoptimized: %v", o, err)
	}
	stitch.Check(err == nil, "a program that compiles optimized also compiles unoptimized")
	return &Prog{src: src, typed: typed, as: -1, undef: o.undef, p: p, noopt: noopt,
		opts: func(optimize bool) []expr.Option { return o.build(typed, optimize) }}, nil
}

// shortCircuit: without short-circuit evaluation a program may fail more often, but when it
// succeeds it gives the same result
func shortCircuit(p *Prog) {
	q, err := expr.Compile(p.src, append(p.opts(true), expr.DisableShortCircuit())...)
	if err != nil {
		stitch.Log("compile error: %v", err)
		return // (a Patch from the options can see a different tree)
	}
	env := envFor(p, nil)
	a, errA := runChecked(p.p, env)
	b, errB := runChecked(q, env)
	stitch.Log("short-circuit: %#v %v | none: %#v %v", a, errA, b, errB)
	if errB == nil {
		stitch.Check(errA == nil, "a program that runs without short-circuit also runs with it")
		stitch.Check(sameIn(p.src, a, b), "DisableShortCircuit does not change the result")
	}
}

// identityPatch rewrites every integer literal into a new, equal node with ast.Patch
type identityPatch struct{ n int }

func (v *identityPatch) Visit(node *ast.Node) {
	if n, ok := (*node).(*ast.IntegerNode); ok {
		ast.Patch(node, &ast.IntegerNode{Value: n.Value})
		v.n++
	}
}

// patchIdentity: a Patch visitor that replaces nodes with equal ones does not change the result
func patchIdentity(p *Prog) {
	q, err := expr.Compile(p.src, append(p.opts(true), expr.Patch(&identityPatch{}))...)
	if err != nil {
		stitch.Log("compile error: %v", err)
		stitch.Check(false, "a no-op Patch does not make a program fail to compile")
	}
	env := envFor(p, nil)
	a, errA := runChecked(p.p, env)
	b, errB := runChecked(q, env)
	stitch.Log("original: %#v %v | patched: %#v %v", a, errA, b, errB)
	stitch.Check((errA == nil) == (errB == nil), "a no-op Patch does not change whether a program fails")
	if errA == nil && errB == nil {
		stitch.Check(sameIn(p.src, a, b), "a no-op Patch does not change the result")
	}
}

// vmReuse: one VM (vm.Debug(), or a zero VM) runs programs in turn with the results of fresh runs
func vmReuse(a, b *Prog, s *stitch.Source) {
	var m *vm.VM
	if s.Choice(2) == 0 {
		m = vm.Debug()
		_ = m.Position()
	} else {
		m = &vm.VM{}
	}
	envA, envB := envFor(a, nil), envFor(b, nil)
	ra, errA := runChecked(a.p, envA)
	rb, errB := runChecked(b.p, envB)
	ra2, errA2 := m.Run(a.p, envA)
	rb2, errB2 := m.Run(b.p, envB)
	ra3, errA3 := m.Run(a.p, envA)
	stitch.Log("a: %#v %v / %#v %v / %#v %v", ra, errA, ra2, errA2, ra3, errA3)
	stitch.Log("b: %#v %v / %#v %v", rb, errB, rb2, errB2)
	stitch.Check((errA == nil) == (errA2 == nil) && (errA == nil) == (errA3 == nil) && (errB == nil) == (errB2 == nil),
		"a reused VM fails where a fresh one fails")
	if errA == nil && errA2 == nil && errA3 == nil {
		stitch.Check(sameIn(a.src, ra, ra2) && sameIn(a.src, ra, ra3), "a reused VM gives the result of a fresh one")
	}
	if errB == nil && errB2 == nil {
		stitch.Check(sameIn(b.src, rb, rb2), "a reused VM gives the result of a fresh one")
	}
}

// programInfo: Source, Locations, DisassembleWriter, Node, the AST helpers and source snippets
func programInfo(p *Prog) {
	src := p.p.Source()
	stitch.Check(src.String() == p.src, "Program.Source is the input")
	stitch.Check(file.NewSource(p.src).String() == p.src, "NewSource keeps its contents")
	for line := 1; line < 8; line++ {
		if _, ok := src.Snippet(line); !ok {
			break
		}
	}
	spans := 0
	for _, l := range p.p.Locations() {
		spans += l.To - l.From
	}
	var w bytes.Buffer // (unaligned: Disassemble pads the columns with a tabwriter)
	p.p.DisassembleWriter(&w)
	stitch.Check(w.Len() > 0 || len(p.p.Bytecode) == 0, "DisassembleWriter writes the bytecode")
	n := p.p.Node()
	_ = ast.Dump(n)
	_ = n.Location()
	_ = n.Type()
	_ = ast.Find(n, func(x ast.Node) bool {
		_, ok := x.(*ast.CallNode)
		return ok
	})
}

// pipeline: the steps of Compile by hand (conf, parser, checker, optimizer, compiler) make a
// program with Compile's result
func pipeline(src string, typed bool) error {
	var env any = asMap(newEnv(nil))
	if typed {
		env = Env{}
	}
	var cfg *conf.Config
	if typed {
		cfg = conf.New(env)
	} else {
		cfg = conf.CreateNew()
		cfg.WithEnv(env)
	}
	cfg.Check()
	tree, err := parser.ParseWithConfig(src, cfg)
	if err != nil {
		return inspectErr(err)
	}
	if _, err := checker.Check(tree, cfg); err != nil {
		return inspectErr(err)
	}
	if err := optimizer.Optimize(&tree.Node, cfg); err != nil {
		return inspectErr(err)
	}
	prog, err := compiler.Compile(tree, cfg)
	if err != nil {
		return inspectErr(err)
	}
	// the same through ParseCheck
	if t2, err := checker.ParseCheck(src, conf.New(env)); err == nil {
		_ = t2.Node.String()
	}
	ref, err := expr.Compile(src, expr.Env(env))
	stitch.Check(err == nil, "Compile accepts what the hand-built pipeline accepts")
	if strings.Contains(src, "f32") {
		return nil // (Compile also rewrites float32 comparison literals after checking)
	}
	runEnv := any(asMap(newEnv(nil)))
	if typed {
		runEnv = newEnv(nil)
	}
	a, errA := runChecked(prog, runEnv)
	b, errB := runChecked(ref, runEnv)
	stitch.Log("pipeline: %#v %v | Compile: %#v %v", a, errA, b, errB)
	stitch.Check((errA == nil) == (errB == nil), "the hand-built pipeline fails where Compile fails")
	if errA == nil && errB == nil {
		stitch.Check(sameIn(src, a, b), "the hand-built pipeline gives Compile's result")
	}
	return nil
}

// eval: Eval parses and compiles without the type checker, then runs. Builtins and env functions
// then see arguments the checker would reject, so Go panics the VM recovers into errors are
// expected here; only panics that escape Eval are bugs.
func eval(src string, s *stitch.Source) error {
	env := asMap(newEnv(s))
	_, err := expr.Eval(src, env)
	return inspectErr(err)
}

// typesEnv: an environment described with the types package (types.Map, types.Array, TypeOf),
// run with the default environment it describes
func typesEnv(src string) error {
	e := newEnv(nil)
	env := types.Map{
		"i":     types.Int,
		"f64":   types.Float64,
		"s":     types.String,
		"ok":    types.Bool,
		"array": types.Array(types.Int),
		"m":     types.Map{"a": types.Int, "b": types.String, "c": types.Array(types.Any), types.Extra: types.Any},
		"foo":   types.TypeOf(e.Foo),
		"list":  types.Array(types.TypeOf(Foo{})),
		"nilv":  types.Nil,
		"add":   types.TypeOf(e.Add),
		"greet": types.TypeOf(e.Greet),
	}
	_ = env["m"].String()
	p, err := expr.Compile(src, expr.Env(env))
	if err != nil {
		return inspectErr(err)
	}
	_, err = runChecked(p, asMap(e))
	return err
}
