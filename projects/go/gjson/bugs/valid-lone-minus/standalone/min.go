// gjson v1.6.4: Valid accepts a member value that is a lone "-" (encoding/json rejects it).
package main

import (
	"encoding/json"

	"github.com/tidwall/gjson"
	"stitch"
)

func main() {
	text := `{"a":-}`
	stitch.Run(func() {
		stitch.Check(gjson.Valid(text) == json.Valid([]byte(text)), "Valid() agrees with encoding/json")
	})
}
