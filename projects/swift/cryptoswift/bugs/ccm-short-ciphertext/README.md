# cryptoswift: Negative value is not representable in CCMModeWorker.init

| | |
|---|---|
| Kind | `Negative value is not representable` |
| Location | `CCMModeWorker.init CCM.swift:117:14`, `CCMModeWorker.__allocating_init CCM.swift:125:3`, `CCM.worker CCM.swift:83:12` |
| Library source | [https://github.com/krzyzanowskim/CryptoSwift](https://github.com/krzyzanowskim/CryptoSwift) @ `19b3c3ceed11` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `ccm-short-ciphertext` |

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 rsa_key(der=Bytes[23,decoded]"\x03\x82\x00\x0dWtbuU7fab}0,Y\x05\x82\x00\xc40\x80")
#1 rsa_key(der=Bytes[34,decoded]"\x02\x82\x00\x16K_SskZ}eQRcM,[B5ff]s/T1\x00\xa0\x84\xff\xff\xff\xff")
#2 aes_decrypt(key=Bytes[32]"\x906\xa74\x86\xbeL\xff\"\n\x09\xfa\xcf=O\xd68\xd1\x0et\xad\x19G\xf2\xfe\xdd\x0c\xbe\xb8\xde\xe5\xd0", iv=Bytes[16]"K1+%(X?1'[E3u|W'", mode=7, pad=1, data=Bytes[13]"WP\x88_\xb9\x0b\x81\xee\xb1\xb1\x0f\xbbC")
```

## Stubs used

What each call does (from `project/harness/`):

`rsa_key` · harness/stubs.swift

```swift
// RSA private keys from DER (the internal ASN.1 decoder); the representation parses back the same
let rsaKey = endpoint("rsa_key") { (der: Bytes<1024, Der>) throws in
    let key = try RSA(rawRepresentation: Data(der.value))
    let raw = try key.externalRepresentation()
    let again = try RSA(rawRepresentation: raw)
    try check(try again.externalRepresentation() == raw, "RSA externalRepresentation is stable")
}
```

`aes_decrypt` · harness/stubs.swift

```swift
let aesDecrypt = endpoint("aes_decrypt") { (key: Bytes<32, Plain>, iv: Bytes<16, Plain>, mode: Pick<Mode>, pad: Pick<Pad>,
                                           data: Bytes<256, Plain>) throws in
    let aes = try AES(key: key.value, blockMode: blockMode(mode.value, iv.value), padding: pad.value.padding)
    _ = try aes.decrypt(data.value)
}
```

## Report

```
==13==ERROR: AddressSanitizer: TRAP on unknown address 0xaaaac4458610 (pc 0xaaaac4458610 bp 0xffffc2eaef30 sp 0xffffc2eaee60 T0)
    #0 0xaaaac4458610 in Swift runtime failure: Negative value is not representable /<compiler-generated>
    #1 0xaaaac4458610 in CCMModeWorker.init(blockSize:nonce:messageLength:additionalAuthenticatedData:expectedTag:tagLength:cipherOperation:) /src/Sources/CryptoSwift/BlockMode/CCM.swift:117:14
    #2 0xaaaac4458610 in CCMModeWorker.__allocating_init(blockSize:nonce:messageLength:additionalAuthenticatedData:expectedTag:tagLength:cipherOperation:) /src/Sources/CryptoSwift/BlockMode/CCM.swift:125:3
    #3 0xaaaac4458610 in CCM.worker(blockSize:cipherOperation:encryptionOperation:) /src/Sources/CryptoSwift/BlockMode/CCM.swift:83:12
    #4 0xaaaac4458910 in protocol witness for BlockMode.worker(blockSize:cipherOperation:encryptionOperation:) in conformance CCM /<compiler-generated>
    #5 0xaaaac43fa9fc in AES.makeDecryptor() /src/Sources/CryptoSwift/AES.Cryptors.swift:33:32
    #6 0xaaaac4406ec8 in AES.decrypt(_:) /src/Sources/CryptoSwift/AES.swift:538:30
    #7 0xaaaac44079f0 in protocol witness for Cipher.decrypt(_:) in conformance AES /<compiler-generated>
    #8 0xaaaac4478290 in Cipher.decrypt(_:) /src/Sources/CryptoSwift/Cipher.swift:45:14
    #9 0xaaaac45a2f90 in closure #1 in variable initialization expression of aesDecrypt .stitch/build/app/pkg/Sources/worker/stubs.swift:84:17
    #10 0xaaaac4578724 in closure #1 in endpoint<A, B, C, D, E>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:132:93
    #11 0xaaaac456ecc4 in partial apply for closure #1 in endpoint<A, B, C, D, E>(_:file:_:) Endpoints.swift.o
    #12 0xaaaac4578b00 in partial apply for closure #1 in endpoint<A, B, C, D, E>(_:file:_:) Endpoints.swift.o
    #13 0xaaaac458f9ec in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #14 0xaaaac4595eb8 in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #15 0xaaaac458f00c in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:475:42
    #16 0xaaaac45aef60 in void (anonymous namespace)::invoke<1>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #21 0xaaaac45901a4 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:543:12
    #22 0xaaaac459d6f0 in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

