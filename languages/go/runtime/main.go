package stitch

import (
	"bufio"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"reflect"
	"runtime/debug"
	"strings"
	"time"
)

// Main is the entry point of the generated worker / reproducer binary:
//
//	worker                                          engine worker (fds 3 and 4)
//	[--trace] [--plan FILE] [--manifest] FILE.stp   run programs (reproducer)
//	--each [--timeout S] [--files-from LIST] FILE   run all, keep going (stitch cov)
func Main() {
	debug.SetMaxStack(256 << 20) // find unbounded recursion quickly
	args := os.Args[1:]
	for _, fn := range reg.inits {
		fn()
	}
	if len(args) > 0 && args[0] == "worker" {
		worker()
		os.Exit(0)
	}
	os.Exit(repro(args))
}

func readMsg(r io.Reader) (byte, []byte, error) {
	var h [5]byte
	if _, err := io.ReadFull(r, h[:]); err != nil {
		return 0, nil, err
	}
	p := make([]byte, binary.LittleEndian.Uint32(h[1:]))
	_, err := io.ReadFull(r, p)
	return h[0], p, err
}

func send(w *bufio.Writer, tag byte, p []byte) {
	var h [5]byte
	h[0] = tag
	binary.LittleEndian.PutUint32(h[1:], uint32(len(p)))
	w.Write(h[:])
	w.Write(p)
	w.Flush()
}

func worker() {
	in := bufio.NewReaderSize(os.NewFile(3, "stitch-in"), 1<<16)
	out := bufio.NewWriterSize(os.NewFile(4, "stitch-out"), 1<<16)
	m := []byte(reg.manifest())
	hello := make([]byte, 4, 4+len(m))
	binary.LittleEndian.PutUint32(hello, uint32(mapSize()))
	send(out, 'H', append(hello, m...))
	dts := reg.datatypes
	for {
		tag, p, err := readMsg(in)
		if err != nil {
			return
		}
		switch tag {
		case 'X':
			flags := binary.LittleEndian.Uint32(p)
			cmplog = flags&2 != 0
			r := RT.run(p[4:], flags&1 != 0)
			t := byte('O')
			if r == "finding" {
				t = 'C'
			}
			send(out, t, RT.resultsBytes())
		case 'D':
			dt, cap := int(binary.LittleEndian.Uint32(p)), int(binary.LittleEndian.Uint32(p[4:]))
			ch := p[8:]
			v := ch
			if len(v) > cap {
				v = v[:cap]
			}
			if dt < len(dts) && dts[dt].decoder != nil {
				c := cap
				if len(ch) > c {
					c = len(ch)
				}
				v = RT.runDecoder(dts[dt], newSource(ch, c, false, 0, nil, nil), cap)
			}
			send(out, 'R', v)
		case 'G':
			dt, seed, cap := int(binary.LittleEndian.Uint32(p)), binary.LittleEndian.Uint64(p[4:]), int(binary.LittleEndian.Uint32(p[12:]))
			pre := p[16:]
			if len(pre) > cap {
				pre = pre[:cap]
			}
			var v []byte
			if dt < len(dts) && dts[dt].decoder != nil {
				s := newSource(pre, cap, true, seed, dts[dt].dict, nil)
				RT.runDecoder(dts[dt], s, -1)
				v = s.buf
				if len(v) > cap {
					v = v[:cap]
				}
			}
			send(out, 'R', v)
		case 'S':
			dt := int(binary.LittleEndian.Uint32(p))
			var spans []uint32
			if dt < len(dts) && dts[dt].decoder != nil {
				RT.runDecoder(dts[dt], newSource(p[4:], len(p)-4, false, 0, nil, &spans), -1)
			}
			b := make([]byte, 4*len(spans))
			for i, x := range spans {
				binary.LittleEndian.PutUint32(b[4*i:], x)
			}
			send(out, 'R', b)
		case 'I':
			dt := int(binary.LittleEndian.Uint32(p))
			var items []uint32
			if dt < len(dts) && dts[dt].decoder != nil {
				s := newSource(p[4:], len(p)-4, false, 0, nil, nil)
				s.items = &items
				RT.runDecoder(dts[dt], s, -1)
				s.finishItems()
			}
			b := make([]byte, 4*len(items))
			for i, x := range items {
				binary.LittleEndian.PutUint32(b[4*i:], x)
			}
			send(out, 'R', b)
		case 'T':
			dt := int(binary.LittleEndian.Uint32(p))
			if dt < len(dts) && len(dts[dt].dict) < 4096 {
				tok := append([]byte(nil), p[4:]...)
				dup := false
				for _, x := range dts[dt].dict {
					dup = dup || string(x) == string(tok)
				}
				if !dup {
					dts[dt].dict = append(dts[dt].dict, tok)
				}
			}
		case 'P':
			if RT.plan != nil {
				RT.plan.Close()
				RT.plan = nil
			}
			if len(p) > 0 {
				f, err := os.Create(string(p))
				if err == nil {
					RT.plan = f
				}
			}
		case 'Q':
			if RT.plan != nil {
				RT.plan.Close()
			}
			return
		}
	}
}

func repro(args []string) int {
	trace, each, manifest := false, false, false
	var files []string
	plan := ""
	timeout := 10
	for i := 0; i < len(args); i++ {
		switch a := args[i]; a {
		case "--trace":
			trace = true
		case "--each":
			each = true
		case "--manifest":
			manifest = true
		case "--plan":
			i++
			plan = args[i]
		case "--timeout":
			i++
			fmt.Sscan(args[i], &timeout)
		case "--files-from":
			i++
			b, _ := os.ReadFile(args[i])
			for _, l := range strings.Split(string(b), "\n") {
				if l = strings.TrimSpace(l); l != "" {
					files = append(files, l)
				}
			}
		default:
			if !strings.HasPrefix(a, "-") {
				files = append(files, a)
			}
		}
	}
	if manifest {
		fmt.Println(reg.manifest())
		return 0
	}
	if plan != "" {
		f, err := os.Create(plan)
		if err == nil {
			RT.plan = f
			defer f.Close()
		}
	}
	if each {
		st := map[string]int{"programs": 0, "ok": 0, "crashed": 0, "timeouts": 0, "invalid": 0}
		for _, f := range files {
			st["programs"]++
			data, err := os.ReadFile(f)
			if err != nil {
				st["invalid"]++
				continue
			}
			done := make(chan string, 1)
			go func() { done <- RT.run(data, false) }()
			select {
			case r := <-done:
				st[map[string]string{"ok": "ok", "finding": "crashed", "invalid": "invalid"}[r]]++
			case <-time.After(time.Duration(timeout) * time.Second):
				st["timeouts"]++
				b, _ := json.Marshal(st)
				fmt.Println(string(b))
				return 0 // a stuck program cannot be stopped: report what ran
			}
		}
		b, _ := json.Marshal(st)
		fmt.Println(string(b))
		return 0
	}
	for _, f := range files {
		if trace {
			fmt.Fprintf(os.Stderr, "[stitch] running %s\n", f)
		}
		data, err := os.ReadFile(f)
		if err != nil {
			fmt.Fprintf(os.Stderr, "[stitch] cannot open %s: %v\n", f, err)
			return 2
		}
		switch RT.run(data, trace) {
		case "finding":
			return 1
		case "invalid":
			return 2
		}
	}
	return 0
}

// ------------------------------------------------------------------ standalone reproducers

var lastFrames [][2]string

var cur = struct {
	n    int
	name string
}{-1, "?"}

// Run runs a standalone reproducer program and reports a crash the way the fuzzer does.
func Run(main func()) {
	for _, fn := range reg.inits {
		fn()
	}
	defer func() {
		v := recover()
		if v == nil {
			return
		}
		if c, ok := v.(checkFailed); ok {
			fmt.Fprintf(os.Stderr, "\n[stitch] CHECK FAILED: %s (node %d)\n", c.what, cur.n)
			os.Exit(1)
		}
		kind, msg := panicKind(v)
		fmt.Fprintf(os.Stderr, "\n[stitch] EXCEPTION in %s: %s: %s\n", cur.name, kind, strings.ReplaceAll(msg, "\n", " "))
		frames := lastFrames
		if frames == nil {
			frames = panicFrames(1)
		}
		for k, f := range frames {
			fmt.Fprintf(os.Stderr, "    #%d 0x0 in %s %s\n", k, f[0], f[1])
		}
		os.Exit(1)
	}()
	main()
}

// Call runs stub `name` (call #n of the recorded program) with concrete arguments and returns
// its results. A bail here means the reproducer diverged from the recorded run (exit 99).
func Call(n int, name string, args ...any) []any {
	outs, bailed, reason := call(n, name, args)
	if bailed {
		fmt.Fprintf(os.Stderr, "\n[repro] DIVERGED at call #%d %s: bailed: %q (it succeeded in the original run)\n", n, name, reason)
		os.Exit(99)
	}
	return outs
}

// CallBails runs a call that bailed in the recorded run.
func CallBails(n int, name string, args ...any) {
	call(n, name, args)
}

func call(n int, name string, args []any) (outs []any, bailed bool, reason string) {
	cur.n, cur.name = n, name
	s := reg.byName[name]
	if s == nil {
		panic("unknown stub " + name)
	}
	in := make([]reflect.Value, len(args))
	t := s.fn.Type()
	for i, a := range args {
		if a == nil {
			in[i] = reflect.Zero(t.In(i))
		} else {
			in[i] = reflect.ValueOf(a).Convert(t.In(i))
		}
	}
	defer func() {
		if v := recover(); v != nil {
			if b, ok := v.(bailSignal); ok {
				bailed, reason = true, b.reason
				return
			}
			lastFrames = panicFrames(1) // the re-panic below loses the original stack
			panic(v)
		}
	}()
	res := s.fn.Call(in)
	if s.errReturn && !res[len(res)-1].IsNil() {
		return nil, true, fmt.Sprint(res[len(res)-1].Interface())
	}
	for _, r := range res[:len(s.outs)] {
		outs = append(outs, r.Interface())
	}
	if os.Getenv("STITCH_REPRO_TRACE") != "" {
		fmt.Fprintf(os.Stderr, "[repro] #%d %s -> ok\n", n, name)
	}
	return outs, false, ""
}

// TempFile writes data to a temporary file and returns its path (a Path parameter).
func TempFile(data []byte) Path {
	f, err := os.CreateTemp("", "stitch-repro-")
	if err != nil {
		panic(err)
	}
	f.Write(data)
	f.Close()
	return Path(f.Name())
}
