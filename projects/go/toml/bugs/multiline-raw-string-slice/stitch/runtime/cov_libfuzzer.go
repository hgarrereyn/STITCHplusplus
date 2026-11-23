//go:build stitchcov

package stitch

// Instrumented build: the compiler (-d=libfuzzer) adds 8-bit edge counters, placed by the linker
// between internal/fuzz._counters and _ecounters, and calls the runtime.libfuzzer* hooks on
// comparisons (the same instrumentation as `go test -fuzz`). Link with -checklinkname=0.

import (
	"encoding/binary"
	"unsafe"
)

//go:linkname _counters internal/fuzz._counters
var _counters [0]byte

//go:linkname _ecounters internal/fuzz._ecounters
var _ecounters [0]byte

func counters() []byte {
	start := unsafe.Pointer(&_counters)
	n := int(uintptr(unsafe.Pointer(&_ecounters)) - uintptr(start))
	if n <= 0 {
		return nil
	}
	return unsafe.Slice((*byte)(start), n)
}

func mapSize() int {
	if n := len(counters()); n > 0 {
		return n
	}
	return 1
}

func resetCounters() { clear(counters()) }

func edgesBytes() []byte {
	c := counters()
	out := make([]byte, 4, 4+5*256)
	n := 0
	for i, v := range c {
		if v != 0 {
			var e [5]byte
			binary.LittleEndian.PutUint32(e[:], uint32(i))
			e[4] = v
			out = append(out, e[:]...)
			n++
		}
	}
	binary.LittleEndian.PutUint32(out, uint32(n))
	return out
}

//go:linkname libfuzzerTraceCmp1 runtime.libfuzzerTraceCmp1
func libfuzzerTraceCmp1(a, b uint8, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 1)
	}
}

//go:linkname libfuzzerTraceCmp2 runtime.libfuzzerTraceCmp2
func libfuzzerTraceCmp2(a, b uint16, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 2)
	}
}

//go:linkname libfuzzerTraceCmp4 runtime.libfuzzerTraceCmp4
func libfuzzerTraceCmp4(a, b uint32, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 4)
	}
}

//go:linkname libfuzzerTraceCmp8 runtime.libfuzzerTraceCmp8
func libfuzzerTraceCmp8(a, b uint64, _ uint) {
	if cmplog && inStub && a != b {
		recInt(a, b, 8)
	}
}

//go:linkname libfuzzerTraceConstCmp1 runtime.libfuzzerTraceConstCmp1
func libfuzzerTraceConstCmp1(a, b uint8, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 1)
	}
}

//go:linkname libfuzzerTraceConstCmp2 runtime.libfuzzerTraceConstCmp2
func libfuzzerTraceConstCmp2(a, b uint16, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 2)
	}
}

//go:linkname libfuzzerTraceConstCmp4 runtime.libfuzzerTraceConstCmp4
func libfuzzerTraceConstCmp4(a, b uint32, _ uint) {
	if cmplog && inStub && a != b {
		recInt(uint64(a), uint64(b), 4)
	}
}

//go:linkname libfuzzerTraceConstCmp8 runtime.libfuzzerTraceConstCmp8
func libfuzzerTraceConstCmp8(a, b uint64, _ uint) {
	if cmplog && inStub && a != b {
		recInt(a, b, 8)
	}
}

//go:linkname libfuzzerHookStrCmp runtime.libfuzzerHookStrCmp
func libfuzzerHookStrCmp(a, b string, _ uint) {
	if cmplog && inStub && a != b {
		recBytes(a, b)
	}
}

//go:linkname libfuzzerHookEqualFold runtime.libfuzzerHookEqualFold
func libfuzzerHookEqualFold(a, b string, _ uint) {
	if cmplog && inStub && a != b {
		recBytes(a, b)
	}
}
