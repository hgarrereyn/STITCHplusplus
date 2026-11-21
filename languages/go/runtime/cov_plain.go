//go:build !stitchcov

package stitch

// Plain build (reproducers, coverage reports): no edge counters.

func mapSize() int        { return 1 }
func resetCounters()      {}
func edgesBytes() []byte  { return []byte{0, 0, 0, 0} }
