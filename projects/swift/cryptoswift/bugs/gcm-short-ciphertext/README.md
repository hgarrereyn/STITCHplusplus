# cryptoswift: Can't take a prefix of negative length from a collection in BlockDecryptor.update

| | |
|---|---|
| Kind | `Can't take a prefix of negative length from a collection` |
| Location | `BlockDecryptor.update BlockDecryptor.swift:48:57`, `AES.decrypt AES.swift:552:33`, `Cipher.decrypt Cipher.swift:45:14` |
| Library source | [https://github.com/krzyzanowskim/CryptoSwift](https://github.com/krzyzanowskim/CryptoSwift) @ `19b3c3ceed11` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `gcm-short-ciphertext` |

## Call sequence (4 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 padding(pad=3, block=64, msg=Bytes[2]"%J")
#1 aes_roundtrip(key=Bytes[32]"='LR~B|de\\<tNRMaAq|Q08l,(%x Bp91", iv=Bytes[3]"\x80s<", mode=0, pad=5, msg=Bytes[1]"B")
#2 remove_padding(pad=3, block=10, data=Bytes[12]"?F\x1a\x08\xfbze~\x81L\xf7\xa2")
#3 aes_decrypt(key=Bytes[16]">l/oCmuaB}fXrTGk", iv=Bytes[3]"Int", mode=6, pad=5, data=Bytes[2]"\x03\x82")
```

## Stubs used

What each call does (from `project/harness/`):

`padding` · harness/stubs.swift

```swift
let padding = endpoint("padding") { (pad: Pick<Pad>, block: Between<1, 64>, msg: Bytes<128, Plain>) throws in
    let p = pad.value.padding
    let added = p.add(to: msg.value, blockSize: block.value)
    let removed = p.remove(from: added, blockSize: block.value)
    if pad.value == .pkcs7 || pad.value == .pkcs5 || pad.value == .iso78164 {
        try check(removed == msg.value, "remove(add(m)) == m")
    }
}
```

`aes_roundtrip` · harness/stubs.swift

```swift
let aesRoundtrip = endpoint("aes_roundtrip") { (key: Bytes<32, Plain>, iv: Bytes<16, Plain>, mode: Pick<Mode>, pad: Pick<Pad>,
                                                 msg: Bytes<256, Plain>) throws in
    try require(mode.value != .ccm, "CCM needs the message length")
    let aes = try AES(key: key.value, blockMode: blockMode(mode.value, iv.value), padding: pad.value.padding)
    let c = try aes.encrypt(msg.value)
    let aes2 = try AES(key: key.value, blockMode: blockMode(mode.value, iv.value), padding: pad.value.padding)
    let m = try aes2.decrypt(c)
    // without padding (or with zero padding) the message comes back padded or truncated by design
    if pad.value != .none && pad.value != .zero { try check(m == msg.value, "AES decrypt(encrypt(m)) == m") }
}
```

`remove_padding` · harness/stubs.swift

```swift
let removePadding = endpoint("remove_padding") { (pad: Pick<Pad>, block: Between<1, 64>, data: Bytes<128, Plain>) throws in
    _ = pad.value.padding.remove(from: data.value, blockSize: block.value)
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
==12==ERROR: AddressSanitizer: TRAP on unknown address 0xaaaae6edf360 (pc 0xaaaae6edf360 bp 0xffffd1566450 sp 0xffffd1565da0 T0)
    #0 0xaaaae6edf360 in Swift runtime failure: Can't take a prefix of negative length from a collection /<compiler-generated>
    #1 0xaaaae6edf360 in specialized Collection.prefix(_:) /<compiler-generated>
    #2 0xaaaae6edf360 in BlockDecryptor.update(withBytes:isLast:) /src/Sources/CryptoSwift/BlockDecryptor.swift:48:57
    #3 0xaaaae6edffac in protocol witness for Updatable.update(withBytes:isLast:) in conformance BlockDecryptor /<compiler-generated>
    #4 0xaaaae6ea74c0 in AES.decrypt(_:) /src/Sources/CryptoSwift/AES.swift:552:33
    #5 0xaaaae6ea79f0 in protocol witness for Cipher.decrypt(_:) in conformance AES /<compiler-generated>
    #6 0xaaaae6f18290 in Cipher.decrypt(_:) /src/Sources/CryptoSwift/Cipher.swift:45:14
    #7 0xaaaae7042f90 in closure #1 in variable initialization expression of aesDecrypt .stitch/build/app/pkg/Sources/worker/stubs.swift:84:17
    #8 0xaaaae7018724 in closure #1 in endpoint<A, B, C, D, E>(_:file:_:) /stitch/swift/Stitch/Sources/Stitch/Endpoints.swift:132:93
    #9 0xaaaae700ecc4 in partial apply for closure #1 in endpoint<A, B, C, D, E>(_:file:_:) Endpoints.swift.o
    #10 0xaaaae7018b00 in partial apply for closure #1 in endpoint<A, B, C, D, E>(_:file:_:) Endpoints.swift.o
    #11 0xaaaae702f9ec in thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #12 0xaaaae7035eb8 in partial apply for thunk for @escaping @callee_guaranteed (@unowned Call) -> (@error @owned Error) /<compiler-generated>
    #13 0xaaaae702f00c in closure #1 in static Runtime.setup() /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:475:42
    #14 0xaaaae704ef60 in void (anonymous namespace)::invoke<1>(stitch::detail::Ctx&) /stitch/swift/bridge/bridge.cpp:47:25
    #19 0xaaaae70301a4 in stitchMain(argNames:_:) /stitch/swift/Stitch/Sources/Stitch/Stitch.swift:543:12
    #20 0xaaaae703d6f0 in worker_main .stitch/build/app/pkg/Sources/worker/main.swift:7:6

AddressSanitizer can not provide additional info.
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH swift toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

