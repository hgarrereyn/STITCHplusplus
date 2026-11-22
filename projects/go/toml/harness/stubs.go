// Stubs: register one function per API call in init(). The signature is the spec:
// parameters of registered object types are objects ("name:take" consumes), results are new
// objects (a trailing error is a bail), and string, []byte, ints, floats, bool, stitch.Path and
// *stitch.Source parameters are fuzzed ("text:max=4096", "n:range=0..9", "mode:oneof=1|2").
// A panic is a bug.
package harness

import (
	"stitch"
	// "example.com/foo"
)

func init() {
	// stitch.Object[*foo.Doc]("Doc", nil)
	// stitch.Endpoint("parse", foo.Parse, "text:max=4096")
	// stitch.Endpoint("render", render, "doc")
	_ = stitch.Bail
}

// func render(doc *foo.Doc) { doc.Render() }
