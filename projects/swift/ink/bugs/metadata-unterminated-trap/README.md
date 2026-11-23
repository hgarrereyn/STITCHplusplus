# ink: String index is out of bounds in _assertionFailure

| | |
|---|---|
| Kind | `String index is out of bounds` |
| Location | `_assertionFailure `, `_StringGuts.validateScalarIndex `, `String.subscript.getter  ` |
| Library source | [https://github.com/JohnSundell/Ink](https://github.com/JohnSundell/Ink) @ `bd223a248244` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `ink-c1` |

## Call sequence (2 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 reparse_html(md=Str[3]":\x00#")
#1 reparse_html(md=Str[64,decoded]"---\n: dm\"\n\x16\xca\xa4\xef\xbf\xbd\xef\xbf\xbd\x1a: pt\nnal\nWY: t\nKtn|\xef\xbf\xbd\x01: RangeRe\nzx)S: \xef\xbf\xbd\x0c\n")
```

## Stubs used

What each call does (from `project/harness/`):

`reparse_html` · harness/stubs.swift

```swift
let reparseHtml = endpoint("reparse_html") { (md: Str<2048, MdText>) in
    // parsing is idempotent on its own output only roughly; just must not trap
    let p = MarkdownParser()
    _ = p.parse(p.html(from: md.value))
}
```

## Report

```
==12==ERROR: AddressSanitizer: ILL on unknown address 0x7f7b89f34946 (pc 0x7f7b89f34946 bp 0x7ffc8efd1710 sp 0x7ffc8efd1680 T0)
    #0 0x7f7b89f34946 in _assertionFailure(_:_:file:line:flags:) (/usr/lib/swift/linux/libswiftCore.so+0x2e2946) (BuildId: 3ae6b39175b7898f05c1f3a999d498a323303137)
    #1 0x7f7b89e58d4d in _StringGuts.validateScalarIndex(_:) crtstuff.c
    #2 0x7f7b89fbe05f in String.subscript.getter (/usr/lib/swift/linux/libswiftCore.so+0x36c05f) (BuildId: 3ae6b39175b7898f05c1f3a999d498a323303137)
    #3 0x56129adb9034 in Reader.currentCharacter.getter /src/Sources/Ink/Internal/Reader.swift:22:45
    #4 0x56129adb9034 in specialized static Metadata.read(using:) /src/Sources/Ink/Internal/Metadata.swift:20:26
    #5 0x56129adad54d in static Metadata.read(using:) /<compiler-generated>
    #6 0x56129adad54d in protocol witness for static Readable.read(using:) in conformance Metadata /<compiler-generated>:10:17
    #7 0x56129adad54d in specialized static Readable.readOrRewind(using:) /src/Sources/Ink/Internal/Readable.swift:20:24
    #8 0x56129adad54d in MarkdownParser.parse(_:) /src/Sources/Ink/API/MarkdownParser.swift:55:59
    #9 0x56129ae27d3c in MarkdownParser.html(from:) .stitch/build/app/pkg/Sources/worker/stubs.swift
    #10 0x56129ae27d3c in closure #1 in variable initialization expression of reparseHtml .stitch/build/app/pkg/Sources/worker/stubs.swift:133:19
    #11 0x56129adccb1e in closure #1 in endpoint<A>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:98:77
    #12 0x56129adccc87 in partial apply for closure #1 in endpoint<A>(_:file:_:) /<compiler-generated>
    #13 0x56129ae0ed3b in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #14 0x56129ae15b24 in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #15 0x56129ae0e22f in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:527:42
    #16 0x56129ae3328b in void (anonymous namespace)::invoke<12>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #21 0x56129ae0f544 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:595:12
    #22 0x56129ae1e9de in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

==12==Register values:
rax = 0x000000000001b45d  rbx = 0x0000000000000001  rcx = 0x0000504000003fc0  rdx = 0x000000000000032a  
rdi = 0x00007f7b85ce9050  rsi = 0x00007f7b85ce90d0  rbp = 0x00007ffc8efd1710  rsp = 0x00007ffc8efd1680  
 r8 = 0x000000000000002b   r9 = 0x0000524000000000  r10 = 0x00007fffffffff01  r11 = 0x251113a968064a01  
r12 = 0x00007ffc8efd1680  r13 = 0x2000000000000000  r14 = 0x0000000000000002  r15 = 0x00007ffc8efd1680  
AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

