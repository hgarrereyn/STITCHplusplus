// Package stitch is the STITCH++ harness API for Go.
//
//	func init() {
//		stitch.Object[*yaml.Node]("Node", nil)
//		stitch.Endpoint("parse", parse, "text:max=4096,type=YamlText")
//		stitch.Endpoint("encode", encode, "node")
//	}
//
//	func parse(text string) (*yaml.Node, error) { ... }   // a non-nil error is a bail
//
// A panic in a stub is a bug. Reference: `stitch guide go`.
package stitch

import (
	"fmt"
	"os"
)

// Bail stops this call; not a bug. The program continues.
func Bail(reason string) {
	panic(bailSignal{reason})
}

// Require bails unless cond holds.
func Require(cond bool, reason string) {
	if !cond {
		panic(bailSignal{reason})
	}
}

// Check is an oracle: a false condition is reported as a bug ("check failed: what").
func Check(cond bool, what string) {
	if !cond {
		panic(checkFailed{what})
	}
}

// Log prints a line in `stitch exec --trace`.
func Log(format string, args ...any) {
	if RT.trace {
		fmt.Fprintf(os.Stderr, "[stitch]     log: "+format+"\n", args...)
	}
}

// Consume: the library took ownership of a borrowed input in this call.
func Consume(obj any) { RT.consume(obj) }

// Set attaches state to an object: numbers, strings, other objects (links die with them), anything.
func Set(obj any, key string, value any) { RT.attrSet(obj, key, value) }

// Get reads an attribute. A missing key is reported by `stitch probe`.
func Get(obj any, key string) (any, bool) { return RT.attrGet(obj, key, true) }

// GetInt reads an integer attribute, or def.
func GetInt(obj any, key string, def int64) int64 {
	v, _ := RT.attrGet(obj, key, false)
	switch x := v.(type) {
	case int:
		return int64(x)
	case int64:
		return x
	case int32:
		return int64(x)
	case uint64:
		return int64(x)
	}
	return def
}

// GetString reads a string attribute, or def.
func GetString(obj any, key, def string) string {
	v, _ := RT.attrGet(obj, key, false)
	if s, ok := v.(string); ok {
		return s
	}
	return def
}

// GetObj reads a linked object; ok is false if it is missing or was consumed.
func GetObj[T any](obj any, key string) (T, bool) {
	v, _ := RT.attrGet(obj, key, false)
	t, ok := v.(T)
	return t, ok
}

// Has reports whether the object has the attribute.
func Has(obj any, key string) bool { _, ok := RT.attrGet(obj, key, false); return ok }

// Del removes an attribute.
func Del(obj any, key string) {
	if i := RT.slotOf(obj); i >= 0 {
		delete(RT.pool[i].attrs, key)
	}
}

// GlobalSet / GlobalGet: state for the whole program.
func GlobalSet(key string, value any) { RT.globals[key] = value }

func GlobalGet(key string) (any, bool) {
	v, ok := RT.globals[key]
	if !ok {
		RT.noteMiss(key)
	}
	return v, ok
}
