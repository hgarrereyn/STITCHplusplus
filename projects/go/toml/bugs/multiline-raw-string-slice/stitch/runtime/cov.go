package stitch

import (
	"bytes"
	"encoding/binary"
)

// Comparison records (shared by the instrumented and plain builds).

var (
	cmplog bool // the engine traces this program
	inStub bool // ...and a stub is running
	node   int
	cmps   bytes.Buffer
	ncmps  int
	seen   = map[[3]uint64]bool{}
	seenB  = map[string]bool{}
)

func setNode(i int) { node = i }

func recInt(a, b uint64, size uint8) {
	k := [3]uint64{uint64(node), a, b}
	if ncmps >= 16384 || seen[k] {
		return
	}
	seen[k] = true
	var r [72]byte
	binary.LittleEndian.PutUint16(r[0:], uint16(node))
	r[2], r[3], r[4], r[5] = 0, size, size, size
	binary.LittleEndian.PutUint64(r[8:], a)
	binary.LittleEndian.PutUint64(r[40:], b)
	cmps.Write(r[:])
	ncmps++
}

func recBytes(a, b string) {
	if len(a) > 32 {
		a = a[:32]
	}
	if len(b) > 32 {
		b = b[:32]
	}
	if a == "" || b == "" || a == b {
		return
	}
	k := string(rune(node)) + "\x00" + a + "\x00" + b
	if ncmps >= 16384 || seenB[k] {
		return
	}
	seenB[k] = true
	var r [72]byte
	binary.LittleEndian.PutUint16(r[0:], uint16(node))
	r[2], r[4], r[5] = 1, byte(len(a)), byte(len(b))
	copy(r[8:], a)
	copy(r[40:], b)
	cmps.Write(r[:])
	ncmps++
}

func beginExec() {
	cmps.Reset()
	ncmps = 0
	clear(seen)
	clear(seenB)
	resetCounters()
}

func cmpsBytes() []byte {
	var h [4]byte
	binary.LittleEndian.PutUint32(h[:], uint32(ncmps))
	return append(h[:], cmps.Bytes()...)
}
