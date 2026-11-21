# gjson: check failed: Valid() agrees with encoding/json in valid

| | |
|---|---|
| Kind | `check failed: Valid() agrees with encoding/json` |
| Location | – |
| Library source | [https://github.com/tidwall/gjson](https://github.com/tidwall/gjson) @ `f0ee9ebde4b6` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `valid-lone-minus` |

## Standalone reproducer

`standalone/min.go` (12 lines of code) calls the library directly; it uses the STITCH stand-in only to report the failed check the same way.

```go
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
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit and runs this file in the project image).

`standalone/generated.go` (10 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (1 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 valid(text=Str[17,decoded]"{\"#\":1.5e3,\"*\":-}")
```

## Stubs used

What each call does (from `project/harness/`):

`valid` · harness/stubs.go

```go
func valid(text string) {
	stitch.Check(gjson.Valid(text) == json.Valid([]byte(text)), "Valid() agrees with encoding/json")
}
```

## Report

```
[stitch] CHECK FAILED: Valid() agrees with encoding/json (node 0)
    #0 0x0 in stitchharness.valid .stitch/build/app/mod/stubs.go:128
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH go toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

