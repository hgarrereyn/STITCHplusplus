package stitch

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"os"
	"reflect"
	"regexp"
	"runtime"
	"strings"
)

const (
	stOK          = 1
	stBail        = 2
	stSkipMissing = 3
	stSkipInvalid = 4
	maxNodes      = 256
	stringsSize   = 65536
)

type bailSignal struct{ reason string }
type checkFailed struct{ what string }
type finding struct{}

type slot struct {
	typ   *objType
	value reflect.Value
	alive bool
	gen   int
	node  int
	out   int
	attrs map[string]any
}

type ref struct{ slot, gen int }

type pnode struct {
	name   string
	ins    [][2]int
	forms  []byte
	params [][]byte
}

type nodeResult struct {
	status, nOut     uint8
	outMask          uint16
	reason, miss     uint32
	consumed         uint32
}

type rt struct {
	trace      bool
	plan       io.WriteCloser
	inExec     bool
	curNode    int
	pool       []*slot
	objOf      map[[2]int]int
	globals    map[string]any
	pending    []pendingAttr
	curIn      []int
	curConsume uint32
	lastMiss   uint32
	gen        int
	tmpFiles   []string
	excBail    bool
	planSrc    int
	// results
	nNodes  int
	current int32
	crashPc uint64
	res     []nodeResult
	strs    bytes.Buffer
	seen    map[uint32]bool
	// standalone use (no program running)
	loose map[any]map[string]any
}

type pendingAttr struct {
	obj any
	key string
	val any
}

var RT = &rt{curNode: -1, current: -1, objOf: map[[2]int]int{}, globals: map[string]any{}, gen: 1,
	excBail: os.Getenv("STITCH_EXCEPTIONS") == "bail", seen: map[uint32]bool{}, loose: map[any]map[string]any{}}

func fnv1a(b []byte) uint32 {
	h := uint32(2166136261)
	for _, c := range b {
		h = (h ^ uint32(c)) * 16777619
	}
	if h == 0 {
		return 1
	}
	return h
}

func (r *rt) resetResults(n int) {
	r.nNodes, r.current, r.crashPc = n, -1, 0
	r.res = make([]nodeResult, n)
	r.strs.Reset()
	r.seen = map[uint32]bool{}
}

func (r *rt) intern(s string) uint32 {
	b := []byte(s)
	h := fnv1a(b)
	if r.seen[h] {
		return h
	}
	if len(b) > 512 {
		b = b[:512]
	}
	if r.strs.Len()+6+len(b) <= stringsSize {
		var hd [6]byte
		binary.LittleEndian.PutUint32(hd[:], h)
		binary.LittleEndian.PutUint16(hd[4:], uint16(len(b)))
		r.strs.Write(hd[:])
		r.strs.Write(b)
		r.seen[h] = true
	}
	return h
}

func (r *rt) resultsBytes() []byte {
	var b bytes.Buffer
	w := func(v any) { binary.Write(&b, binary.LittleEndian, v) }
	w(uint32(r.nNodes))
	w(r.current)
	w(r.crashPc)
	for _, n := range r.res {
		w(n.status)
		w(n.nOut)
		w(n.outMask)
		w(n.reason)
		w(n.miss)
		w(n.consumed)
	}
	w(uint32(r.strs.Len()))
	b.Write(r.strs.Bytes())
	b.Write(edgesBytes())
	b.Write(cmpsBytes())
	return b.Bytes()
}

// ------------------------------------------------------------------ objects + attributes
func (r *rt) live(i int) *slot {
	if i >= 0 && i < len(r.pool) && r.pool[i].alive {
		return r.pool[i]
	}
	return nil
}

func same(a reflect.Value, b any) bool {
	if !a.IsValid() {
		return false
	}
	av := a.Interface()
	at := reflect.TypeOf(av)
	if at == nil || reflect.TypeOf(b) != at || !at.Comparable() {
		return false
	}
	return av == b
}

func (r *rt) slotOf(o any) int {
	for i := len(r.pool) - 1; i >= 0; i-- {
		if r.pool[i].alive && same(r.pool[i].value, o) {
			return i
		}
	}
	return -1
}

func (r *rt) noteMiss(key string) {
	if r.inExec {
		r.lastMiss = r.intern(key)
	}
}

func (r *rt) wrap(v any) any {
	if v == nil {
		return v
	}
	if i := r.slotOf(v); i >= 0 {
		return ref{i, r.pool[i].gen}
	}
	return v
}

func (r *rt) unwrap(v any) any {
	if x, ok := v.(ref); ok {
		if s := r.live(x.slot); s != nil && s.gen == x.gen {
			return s.value.Interface()
		}
		return nil
	}
	return v
}

func (r *rt) attrSet(o any, key string, v any) {
	if i := r.slotOf(o); i >= 0 {
		r.pool[i].attrs[key] = r.wrap(v)
	} else if r.inExec {
		r.pending = append(r.pending, pendingAttr{o, key, r.wrap(v)})
	} else if reflect.TypeOf(o) != nil && reflect.TypeOf(o).Comparable() {
		if r.loose[o] == nil {
			r.loose[o] = map[string]any{}
		}
		r.loose[o][key] = v
	}
}

func (r *rt) attrGet(o any, key string, note bool) (any, bool) {
	i := r.slotOf(o)
	if i >= 0 {
		if v, ok := r.pool[i].attrs[key]; ok {
			return r.unwrap(v), true
		}
	} else {
		for k := len(r.pending) - 1; k >= 0; k-- {
			if p := r.pending[k]; p.key == key && reflect.TypeOf(p.obj) == reflect.TypeOf(o) && reflect.TypeOf(o).Comparable() && p.obj == o {
				return r.unwrap(p.val), true
			}
		}
		if reflect.TypeOf(o) != nil && reflect.TypeOf(o).Comparable() {
			if v, ok := r.loose[o][key]; ok {
				return v, true
			}
		}
	}
	if note {
		r.noteMiss(key)
	}
	return nil, false
}

func (r *rt) consume(o any) {
	i := r.slotOf(o)
	for k, s := range r.curIn {
		if s == i {
			r.curConsume |= 1 << k
		}
	}
}

// ------------------------------------------------------------------ params
func (r *rt) tempFile(data []byte) string {
	dir := os.Getenv("STITCH_TMPDIR")
	f, err := os.CreateTemp(dir, "stitch-")
	if err != nil {
		panic(err)
	}
	f.Write(data)
	f.Close()
	r.tmpFiles = append(r.tmpFiles, f.Name())
	return f.Name()
}

func (r *rt) decodeParam(dt string, raw []byte, form byte, max int) []byte {
	if form == 1 && dt != "" {
		d := reg.datatype(dt)
		if d.decoder != nil {
			c := max
			if len(raw) > c {
				c = len(raw)
			}
			return r.runDecoder(d, newSource(raw, c, false, 0, nil, nil), max)
		}
	}
	if len(raw) > max {
		raw = raw[:max]
	}
	return append([]byte(nil), raw...)
}

func (r *rt) runDecoder(d *datatype, s *Source, max int) (out []byte) {
	defer func() {
		if recover() != nil {
			out = nil
		}
	}()
	out = d.decoder(s)
	if max >= 0 && len(out) > max {
		out = out[:max]
	}
	return out
}

func lei(raw []byte, n int) uint64 {
	if len(raw) > n {
		raw = raw[:n]
	}
	return le(raw)
}

func (p *param) rawInt(raw []byte) int64 {
	v := lei(raw, p.size)
	if p.signed && p.size < 8 && (v>>(8*p.size-1))&1 == 1 {
		v |= ^uint64(0) << (8 * p.size)
	}
	return int64(v)
}

func (p *param) oneofIndex(raw []byte) int {
	v := int64(lei(raw, 8))
	for i, x := range p.values {
		if x == v {
			return i
		}
	}
	return int(uint64(v) % uint64(len(p.values)))
}

func (p *param) bufValue(raw []byte, form byte) []byte {
	return RT.decodeParam(p.datatype, raw, form, p.size)
}

// value the stub receives
func (p *param) decode(raw []byte, form byte) reflect.Value {
	v := reflect.New(p.typ).Elem()
	switch p.kind {
	case "bool":
		v.SetBool(len(raw) > 0 && raw[0]&1 == 1)
	case "float":
		if p.size == 4 {
			v.SetFloat(float64(math.Float32frombits(uint32(lei(raw, 4)))))
		} else {
			v.SetFloat(math.Float64frombits(lei(raw, 8)))
		}
	case "str":
		v.SetString(string(p.bufValue(raw, form)))
	case "bytes":
		v.SetBytes(p.bufValue(raw, form))
	case "file":
		v.SetString(RT.tempFile(p.bufValue(raw, form)))
	case "source":
		b := raw
		if len(b) > p.size {
			b = b[:p.size]
		}
		return reflect.ValueOf(RT.newSource(b, p.size))
	case "oneof":
		i := p.oneofIndex(raw)
		if p.choices != nil {
			v.SetString(p.choices[i])
		} else {
			setInt(v, p.values[i])
		}
	case "range":
		x := int64(lei(raw, 8))
		if x < p.lo || x > p.hi {
			span := uint64(p.hi) - uint64(p.lo) + 1
			off := uint64(x) - uint64(p.lo)
			if span != 0 {
				off %= span
			}
			x = int64(uint64(p.lo) + off)
		}
		setInt(v, x)
	case "flags":
		setInt(v, int64(lei(raw, 8))&p.mask)
	default:
		setInt(v, p.rawInt(raw))
	}
	return v
}

func setInt(v reflect.Value, x int64) {
	if v.Kind() >= reflect.Uint && v.Kind() <= reflect.Uintptr {
		v.SetUint(uint64(x))
	} else {
		v.SetInt(x)
	}
}

func (r *rt) newSource(b []byte, cap int) *Source {
	s := newSource(b, cap, false, 0, nil, nil)
	if r.plan != nil && r.inExec && r.curNode >= 0 {
		sid, node := r.planSrc, r.curNode
		r.planSrc++
		s.log = func(op string, v any, isBytes bool) { r.planDraw(node, sid, op, v, isBytes) }
	}
	return s
}

func preview(b []byte) string {
	var s strings.Builder
	s.WriteByte('"')
	for i, c := range b {
		if i >= 160 {
			break
		}
		switch {
		case c == '"' || c == '\\':
			s.WriteByte('\\')
			s.WriteByte(c)
		case c >= 32 && c < 127:
			s.WriteByte(c)
		case c == '\n':
			s.WriteString("\\n")
		default:
			fmt.Fprintf(&s, "\\x%02x", c)
		}
	}
	s.WriteByte('"')
	if len(b) > 160 {
		s.WriteString("...")
	}
	return s.String()
}

func (r *rt) describe(p *param, raw []byte, form byte) string {
	switch p.kind {
	case "str", "bytes", "file":
		v := p.bufValue(raw, form)
		d := ""
		if form == 1 {
			d = ",decoded"
		}
		return fmt.Sprintf("%s[%d%s]%s", p.ctype, len(v), d, preview(v))
	case "source":
		b := raw
		if len(b) > 48 {
			b = b[:48]
		}
		return fmt.Sprintf("Src[%d]%x", len(raw), b)
	}
	return goLiteral(p.decode(raw, form))
}

// goLiteral is a Go expression for a recorded value.
func goLiteral(v reflect.Value) string {
	switch v.Kind() {
	case reflect.String:
		if v.Type() == tPath {
			return fmt.Sprintf("stitch.Path(%q)", v.String())
		}
		if v.Type().Name() != "string" && v.Type().PkgPath() != "" {
			return fmt.Sprintf("%s(%q)", v.Type().String(), v.String())
		}
		return fmt.Sprintf("%q", v.String())
	case reflect.Bool:
		return fmt.Sprint(v.Bool())
	case reflect.Float32, reflect.Float64:
		f := v.Float()
		switch {
		case math.IsNaN(f):
			return fmt.Sprintf("%s(math.NaN())", v.Type())
		case math.IsInf(f, 1):
			return fmt.Sprintf("%s(math.Inf(1))", v.Type())
		case math.IsInf(f, -1):
			return fmt.Sprintf("%s(math.Inf(-1))", v.Type())
		}
		return fmt.Sprintf("%s(%v)", v.Type(), f)
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return fmt.Sprintf("%s(%d)", v.Type(), v.Int())
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return fmt.Sprintf("%s(%d)", v.Type(), v.Uint())
	case reflect.Slice:
		if v.Type() == tBytes {
			return fmt.Sprintf("[]byte(%q)", v.Bytes())
		}
	}
	return fmt.Sprintf("%#v", v.Interface())
}

// ------------------------------------------------------------------ plan mode
func (r *rt) planLine(d map[string]any) {
	if r.plan == nil {
		return
	}
	b, _ := json.Marshal(d)
	r.plan.Write(append(b, '\n'))
}

func (r *rt) planDraw(node, src int, op string, v any, isBytes bool) {
	d := map[string]any{"t": "draw", "node": node, "src": src, "op": op}
	if isBytes {
		d["hex"] = hex.EncodeToString(v.([]byte))
	} else {
		d["v"] = fmt.Sprint(v)
	}
	r.planLine(d)
}

func (r *rt) planParam(a arg, raw []byte, form byte) map[string]any {
	p := a.param
	d := map[string]any{"kind": p.kind, "size": p.size, "name": a.name, "gotype": p.typ.String()}
	switch p.kind {
	case "str", "bytes", "file":
		d["decoded"] = form == 1
		d["hex"] = hex.EncodeToString(p.bufValue(raw, form))
	case "source":
		b := raw
		if len(b) > p.size {
			b = b[:p.size]
		}
		d["hex"] = hex.EncodeToString(b)
	default:
		v := p.decode(raw, form)
		d["v"] = fmt.Sprint(v.Interface())
		d["go"] = goLiteral(v)
	}
	if p.datatype != "" {
		d["datatype"] = p.datatype
	}
	return d
}

// ------------------------------------------------------------------ program
func parseProgram(data []byte) []pnode {
	rd := bytes.NewReader(data)
	var magic [4]byte
	if _, err := io.ReadFull(rd, magic[:]); err != nil || string(magic[:]) != "STCH" {
		return nil
	}
	var nn uint32
	if binary.Read(rd, binary.LittleEndian, &nn) != nil || nn > 100000 {
		return nil
	}
	names := make([]string, nn)
	for i := range names {
		var l uint16
		if binary.Read(rd, binary.LittleEndian, &l) != nil {
			return nil
		}
		b := make([]byte, l)
		if _, err := io.ReadFull(rd, b); err != nil {
			return nil
		}
		names[i] = string(b)
	}
	var n uint32
	if binary.Read(rd, binary.LittleEndian, &n) != nil || n > maxNodes {
		return nil
	}
	nodes := make([]pnode, 0, n)
	for i := uint32(0); i < n; i++ {
		var nd pnode
		var ni uint32
		var nin, np uint8
		if binary.Read(rd, binary.LittleEndian, &ni) != nil || binary.Read(rd, binary.LittleEndian, &nin) != nil {
			return nil
		}
		nd.name = "?"
		if int(ni) < len(names) {
			nd.name = names[ni]
		}
		for k := 0; k < int(nin); k++ {
			var src uint32
			var out uint8
			if binary.Read(rd, binary.LittleEndian, &src) != nil || binary.Read(rd, binary.LittleEndian, &out) != nil {
				return nil
			}
			nd.ins = append(nd.ins, [2]int{int(src), int(out)})
		}
		if binary.Read(rd, binary.LittleEndian, &np) != nil {
			return nil
		}
		for k := 0; k < int(np); k++ {
			var form uint8
			var l uint32
			if binary.Read(rd, binary.LittleEndian, &form) != nil || binary.Read(rd, binary.LittleEndian, &l) != nil || l > 16<<20 || int(l) > rd.Len() {
				return nil
			}
			b := make([]byte, l)
			io.ReadFull(rd, b)
			nd.forms = append(nd.forms, form)
			nd.params = append(nd.params, b)
		}
		nodes = append(nodes, nd)
	}
	return nodes
}

var digits = regexp.MustCompile(`\d+`)

// panicKind buckets a recovered panic value.
func panicKind(v any) (kind, msg string) {
	switch x := v.(type) {
	case runtime.Error:
		// "runtime error: index out of range [5] with length 3" -> "index out of range"
		msg = x.Error()
		k := strings.TrimPrefix(msg, "runtime error: ")
		if i := strings.IndexAny(k, "[:"); i > 0 {
			k = strings.TrimSpace(k[:i])
		}
		k = strings.TrimPrefix(k, "invalid memory address or ")
		return k, msg
	case error:
		return fmt.Sprintf("panic(%T)", x), x.Error()
	case string:
		return "panic " + shortMsg(x), x
	}
	return fmt.Sprintf("panic(%T)", v), fmt.Sprint(v)
}

// shortMsg: a panic message as a bucket kind: at most 48 characters cut at a word, ": " -> " - "
// (the kind ends at the first ": "), numbers -> N. Same rule in the CLI and the engine triage.
func shortMsg(msg string) string {
	r := []rune(strings.ReplaceAll(msg, "\n", " "))
	if len(r) > 48 {
		r = r[:48]
		if i := strings.LastIndex(string(r), " "); i > 0 {
			r = []rune(string(r)[:i])
		}
	}
	k := strings.ReplaceAll(string(r), ": ", " - ")
	return strings.TrimRight(strings.TrimSpace(digits.ReplaceAllString(k, "N")), " -:")
}

// goFunc: "gopkg.in/yaml%2ev3.(*parser).parse" -> "yaml.v3.parser.parse"
func goFunc(fn string) string {
	if i := strings.LastIndex(fn, "/"); i >= 0 {
		fn = fn[i+1:]
	}
	return strings.NewReplacer("(*", "", "(", "", ")", "", "%2e", ".").Replace(fn)
}

// panicFrames: innermost-first (function, location) of the panicking goroutine, without the
// runtime's own frames and STITCH++'s.
func panicFrames(skip int) [][2]string {
	pcs := make([]uintptr, 64)
	n := runtime.Callers(skip, pcs)
	frames := runtime.CallersFrames(pcs[:n])
	var out [][2]string
	started := false
	for {
		f, more := frames.Next()
		fn := f.Function
		stop := false
		switch {
		case fn == "runtime.gopanic":
			started = true
			out = out[:0]
		case !started || strings.HasPrefix(fn, "runtime."):
		case strings.HasPrefix(fn, "reflect.") || strings.HasPrefix(fn, "stitch.(*rt)"):
			stop = true // the call into the stub: everything below is the runtime
		case strings.HasPrefix(fn, "stitch."):
			// stitch.Check / stitch.Bail called by the stub
		default:
			out = append(out, [2]string{goFunc(fn), fmt.Sprintf("%s:%d", f.File, f.Line)})
		}
		if stop || !more {
			break
		}
	}
	return out
}

func (r *rt) report(ep, kind, msg string, frames [][2]string, what *string) {
	w := os.Stderr
	if what != nil {
		r.crashPc = 1<<63 | uint64(fnv1a([]byte(*what)))
		fmt.Fprintf(w, "\n[stitch] CHECK FAILED: %s (node %d)\n", *what, r.curNode)
	} else {
		top := ""
		for _, f := range frames {
			if !strings.Contains(f[1], "/harness/") {
				top = f[1]
				break
			}
		}
		idx := 0
		for i, s := range reg.stubs {
			if s.name == ep {
				idx = i
			}
		}
		r.crashPc = 1<<63 | uint64(idx)<<32 | uint64(fnv1a([]byte(kind+"|"+top)))
		m := strings.ReplaceAll(msg, "\n", " ")
		if len(m) > 500 {
			m = m[:500]
		}
		fmt.Fprintf(w, "\n[stitch] EXCEPTION in %s: %s: %s\n", ep, kind, m)
	}
	for k, f := range frames {
		if k >= 24 {
			break
		}
		fmt.Fprintf(w, "    #%d 0x0 in %s %s\n", k, f[0], f[1])
	}
	fmt.Fprintln(w)
}

// invoke runs one stub; it returns its results, or the bail reason, or reports a finding.
func (r *rt) invoke(s *stub, args []reflect.Value) (outs []reflect.Value, bail *string) {
	defer func() {
		v := recover()
		if v == nil {
			return
		}
		inStub = false
		switch x := v.(type) {
		case bailSignal:
			reason := x.reason
			bail = &reason
		case checkFailed:
			r.report(s.name, "", "", panicFrames(1), &x.what)
			panic(finding{})
		default:
			kind, msg := panicKind(v)
			if r.excBail {
				reason := "panic"
				bail = &reason
				return
			}
			r.report(s.name, kind, msg, panicFrames(1), nil)
			panic(finding{})
		}
	}()
	inStub = true
	outs = s.fn.Call(args)
	inStub = false
	return outs, nil
}

// run executes one program: "ok", "finding" or "invalid".
func (r *rt) run(data []byte, trace bool) (result string) {
	nodes := parseProgram(data)
	if nodes == nil {
		r.resetResults(0)
		if trace {
			fmt.Fprintln(os.Stderr, "[stitch] invalid program")
		}
		return "invalid"
	}
	r.resetResults(len(nodes))
	r.pool = nil
	r.objOf = map[[2]int]int{}
	r.globals = map[string]any{}
	r.trace = trace
	r.inExec = true
	beginExec()
	defer func() {
		r.inExec = false
		r.curNode = -1
		inStub = false
		cmplog = false
		for _, f := range r.tmpFiles {
			os.Remove(f)
		}
		r.tmpFiles = nil
		if v := recover(); v != nil {
			if _, ok := v.(finding); ok {
				result = "finding"
				return
			}
			panic(v)
		}
	}()
	r.runNodes(nodes)
	return "ok"
}

func (r *rt) runNodes(nodes []pnode) {
	e := os.Stderr
	for i, nd := range nodes {
		r.current = int32(i)
		r.curNode = i
		setNode(i)
		res := &r.res[i]
		s := reg.byName[nd.name]
		if s == nil {
			res.status = stSkipInvalid
			r.planLine(map[string]any{"t": "skip", "node": i, "why": "unknown endpoint"})
			if r.trace {
				fmt.Fprintf(e, "[stitch] #%d %s: unknown endpoint, skipped\n", i, nd.name)
			}
			continue
		}
		var inputs, params []arg
		for _, a := range s.args {
			if a.obj != nil {
				inputs = append(inputs, a)
			} else {
				params = append(params, a)
			}
		}
		res.nOut = uint8(len(s.outs))
		if len(nd.ins) != len(inputs) || len(nd.params) != len(params) {
			res.status = stSkipInvalid
			r.planLine(map[string]any{"t": "skip", "node": i, "why": "signature mismatch"})
			if r.trace {
				fmt.Fprintf(e, "[stitch] #%d %s: signature mismatch, skipped\n", i, nd.name)
			}
			continue
		}
		var inSlot []int
		missing := false
		for k, in := range nd.ins {
			si, ok := r.objOf[in]
			sl := r.live(si)
			if !ok || sl == nil || sl.typ != inputs[k].obj {
				missing = true
				break
			}
			for _, x := range inSlot {
				if x == si {
					missing = true
				}
			}
			inSlot = append(inSlot, si)
		}
		if missing {
			res.status = stSkipMissing
			r.planLine(map[string]any{"t": "skip", "node": i, "why": "input not available"})
			if r.trace {
				fmt.Fprintf(e, "[stitch] #%d %s: input not available, skipped\n", i, nd.name)
			}
			continue
		}
		if r.trace {
			var parts []string
			oi, pi := 0, 0
			for _, a := range s.args {
				if a.obj != nil {
					t := ""
					if a.take {
						t = "take "
					}
					parts = append(parts, fmt.Sprintf("%s=%so%d.%d", a.name, t, nd.ins[oi][0], nd.ins[oi][1]))
					oi++
				} else {
					parts = append(parts, a.name+"="+r.describe(a.param, nd.params[pi], nd.forms[pi]))
					pi++
				}
			}
			fmt.Fprintf(e, "[stitch] #%d %s(%s)\n", i, nd.name, strings.Join(parts, ", "))
		}
		if r.plan != nil {
			r.planCall(i, s, nd)
			r.planSrc = 0
		}
		r.curIn = inSlot
		r.curConsume = 0
		r.lastMiss = 0
		r.pending = nil
		args := make([]reflect.Value, len(s.args))
		oi, pi := 0, 0
		for k, a := range s.args {
			if a.obj != nil {
				args[k] = r.pool[inSlot[oi]].value
				oi++
			} else {
				args[k] = a.param.decode(nd.params[pi], nd.forms[pi])
				pi++
			}
		}
		outs, bail := r.invoke(s, args)
		reason := ""
		if bail != nil {
			res.status = stBail
			reason = *bail
		} else {
			res.status = stOK
			if s.errReturn {
				if err := outs[len(outs)-1]; !err.IsNil() {
					res.status = stBail
					reason = "error: " + digits.ReplaceAllString(fmt.Sprint(err.Interface()), "N")
					if len(reason) > 80 {
						reason = reason[:80]
					}
				}
				outs = outs[:len(outs)-1]
			}
		}
		for k, si := range inSlot {
			if inputs[k].take || r.curConsume&(1<<k) != 0 {
				r.pool[si].alive = false
				r.pool[si].value = reflect.Value{}
				if !inputs[k].take {
					res.consumed |= 1 << k
				}
			}
		}
		if res.status == stOK {
			for k, v := range outs {
				if isNil(v) {
					continue
				}
				sl := &slot{typ: s.outs[k], value: v, alive: true, gen: r.gen, node: i, out: k, attrs: map[string]any{}}
				r.gen++
				for _, p := range r.pending {
					if same(v, p.obj) {
						sl.attrs[p.key] = p.val
					}
				}
				r.pool = append(r.pool, sl)
				r.objOf[[2]int{i, k}] = len(r.pool) - 1
				res.outMask |= 1 << k
			}
			for k := range s.outs {
				if !s.optional[k] && res.outMask&(1<<k) == 0 {
					res.status = stBail
					reason = "returned nil"
				}
			}
		}
		if res.status == stBail {
			if reason == "" {
				reason = "bail"
			}
			res.reason = r.intern(reason)
			res.miss = r.lastMiss
		}
		if r.plan != nil {
			d := map[string]any{"t": "done", "node": i, "status": "ok", "outs": []int{}, "consumed": []int{}}
			if res.status == stBail {
				d["status"] = "bail"
				d["reason"] = reason
			}
			for k := range s.outs {
				if res.outMask&(1<<k) != 0 {
					d["outs"] = append(d["outs"].([]int), k)
				}
			}
			for k := range inSlot {
				if res.consumed&(1<<k) != 0 {
					d["consumed"] = append(d["consumed"].([]int), k)
				}
			}
			r.planLine(d)
		}
		if r.trace {
			if res.status == stOK {
				o := ""
				for k := range s.outs {
					if res.outMask&(1<<k) != 0 {
						o += fmt.Sprintf(" o%d.%d", i, k)
					}
				}
				if o != "" {
					o = " produced" + o
				}
				fmt.Fprintf(e, "[stitch]     -> ok%s\n", o)
			} else {
				fmt.Fprintf(e, "[stitch]     -> bail %q\n", reason)
			}
		}
	}
	r.current = int32(len(nodes))
	r.curNode = len(nodes)
	setNode(len(nodes))
	var drops []*slot
	for k := len(r.pool) - 1; k >= 0; k-- {
		if sl := r.pool[k]; sl.alive && sl.typ.drop.IsValid() {
			drops = append(drops, sl)
		}
	}
	if r.plan != nil {
		objs := [][2]int{}
		for _, sl := range drops {
			objs = append(objs, [2]int{sl.node, sl.out})
		}
		r.planLine(map[string]any{"t": "drops", "objs": objs})
	}
	for _, sl := range drops {
		func() {
			defer func() { recover() }()
			sl.typ.drop.Call([]reflect.Value{sl.value})
		}()
	}
	r.pool = nil
	r.current = -1
	r.planLine(map[string]any{"t": "end"})
}

func isNil(v reflect.Value) bool {
	switch v.Kind() {
	case reflect.Ptr, reflect.Interface, reflect.Map, reflect.Slice, reflect.Func, reflect.Chan:
		return v.IsNil()
	}
	return false
}

func (r *rt) planCall(i int, s *stub, nd pnode) {
	args := []map[string]any{}
	oi, pi := 0, 0
	for _, a := range s.args {
		if a.obj != nil {
			args = append(args, map[string]any{"obj": nd.ins[oi], "type": a.obj.name, "take": a.take, "name": a.name})
			oi++
		} else {
			args = append(args, r.planParam(a, nd.params[pi], nd.forms[pi]))
			pi++
		}
	}
	outs := []string{}
	for _, o := range s.outs {
		outs = append(outs, o.name)
	}
	r.planLine(map[string]any{"t": "call", "node": i, "ep": s.name, "args": args, "outs": outs, "multi": len(s.outs) > 1,
		"err": s.errReturn})
}
