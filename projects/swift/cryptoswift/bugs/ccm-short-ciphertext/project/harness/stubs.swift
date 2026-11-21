// CryptoSwift: block and stream ciphers (encrypt/decrypt round trips, decrypting arbitrary data),
// padding removal, hashes, MACs, KDFs, hex / base64 parsing, RSA keys from DER (the ASN.1 decoder).
// Oracles: decrypt(encrypt(m)) == m; remove(add(m)) == m; an RSA key's DER parses back the same.
// Thrown errors are bails (wrong key sizes, bad padding, malformed DER).
import CryptoSwift
import Foundation
import Stitch

enum Der: DecodedBytes {
    // DER TLVs: tag, length (short, long or indefinite forms), contents; nested SEQUENCE / SET
    static func decode(_ s: Source) -> [UInt8] {
        var out: [UInt8] = []
        func tlv(_ depth: Int) {
            let tag = s.pick([0x30, 0x31, 0x02, 0x03, 0x04, 0x05, 0x06, 0x0c, 0x13, 0x17, 0xa0, 0xa1, UInt8(truncatingIfNeeded: s.u8())])
            var body: [UInt8] = []
            if (tag == 0x30 || tag == 0x31 || tag >= 0xa0) && depth < 6 {
                let saved = out
                out = []
                while s.more() { tlv(depth + 1) }
                body = out
                out = saved
            } else {
                body = s.bytes(32)
            }
            out.append(tag)
            let n = body.count + (s.coin(0.15) ? s.range(-2, 200) : 0)
            switch s.choice(4) {
            case 0 where n < 128 && n >= 0: out.append(UInt8(n))
            case 1: out += [0x81, UInt8(truncatingIfNeeded: n)]
            case 2: out += [0x82, UInt8(truncatingIfNeeded: n >> 8), UInt8(truncatingIfNeeded: n)]
            case 3: out += s.pick([[0x80], [0x84, 0xff, 0xff, 0xff, 0xff], [0x89, 1, 2, 3, 4, 5, 6, 7, 8, 9], [0x85, 0, 0, 0, 0, 1]])
            default: out.append(UInt8(truncatingIfNeeded: n))
            }
            out += body
        }
        tlv(0)
        while s.more() { tlv(0) }
        return out
    }
}

enum Mode: CaseIterable { case cbc, ctr, ecb, cfb, ofb, pcbc, gcm, ccm }
enum Pad: CaseIterable {
    case none, pkcs7, pkcs5, zero, iso78164, iso10126
    var padding: Padding {
        switch self {
        case .none: return .noPadding
        case .pkcs7: return .pkcs7
        case .pkcs5: return .pkcs5
        case .zero: return .zeroPadding
        case .iso78164: return .iso78164
        case .iso10126: return .iso10126
        }
    }
}

func blockMode(_ m: Mode, _ iv: [UInt8]) -> BlockMode {
    switch m {
    case .cbc: return CBC(iv: iv)
    case .ctr: return CTR(iv: iv)
    case .ecb: return ECB()
    case .cfb: return CFB(iv: iv)
    case .ofb: return OFB(iv: iv)
    case .pcbc: return PCBC(iv: iv)
    case .gcm: return GCM(iv: iv, mode: .combined)
    case .ccm: return CCM(iv: iv, tagLength: 16, messageLength: 0)
    }
}

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

let aesDecrypt = endpoint("aes_decrypt") { (key: Bytes<32, Plain>, iv: Bytes<16, Plain>, mode: Pick<Mode>, pad: Pick<Pad>,
                                           data: Bytes<256, Plain>) throws in
    let aes = try AES(key: key.value, blockMode: blockMode(mode.value, iv.value), padding: pad.value.padding)
    _ = try aes.decrypt(data.value)
}

let streamCiphers = endpoint("stream_ciphers") { (kind: Between<0, 3>, key: Bytes<32, Plain>, iv: Bytes<16, Plain>, msg: Bytes<256, Plain>) throws in
    switch kind.value {
    case 0:
        let c = try ChaCha20(key: key.value, iv: iv.value)
        let e = try c.encrypt(msg.value)
        try check(try ChaCha20(key: key.value, iv: iv.value).decrypt(e) == msg.value, "ChaCha20 round trip")
    case 1:
        let c = try Rabbit(key: key.value, iv: iv.value)
        let e = try c.encrypt(msg.value)
        try check(try Rabbit(key: key.value, iv: iv.value).decrypt(e) == msg.value, "Rabbit round trip")
    case 2:
        let b = try Blowfish(key: key.value, blockMode: CBC(iv: iv.value), padding: .pkcs7)
        let e = try b.encrypt(msg.value)
        try check(try Blowfish(key: key.value, blockMode: CBC(iv: iv.value), padding: .pkcs7).decrypt(e) == msg.value, "Blowfish round trip")
    default:
        let c = try ChaCha20(key: key.value, iv: iv.value)
        _ = try c.decrypt(msg.value)
    }
}

let padding = endpoint("padding") { (pad: Pick<Pad>, block: Between<0, 64>, msg: Bytes<128, Plain>) throws in
    let p = pad.value.padding
    let added = p.add(to: msg.value, blockSize: block.value)
    let removed = p.remove(from: added, blockSize: block.value)
    if pad.value == .pkcs7 || pad.value == .pkcs5 || pad.value == .iso78164 {
        try check(removed == msg.value, "remove(add(m)) == m")
    }
}

let removePadding = endpoint("remove_padding") { (pad: Pick<Pad>, block: Between<0, 64>, data: Bytes<128, Plain>) throws in
    _ = pad.value.padding.remove(from: data.value, blockSize: block.value)
}

let hashes = endpoint("hashes") { (data: Bytes<512, Plain>, which: Between<0, 6>) throws in
    switch which.value {
    case 0: _ = data.value.md5()
    case 1: _ = data.value.sha1()
    case 2: _ = data.value.sha224()
    case 3: _ = data.value.sha512()
    case 4: _ = data.value.sha3(.keccak256)
    case 5: _ = data.value.crc32()
    default: _ = Checksum.crc16(data.value)
    }
}

let hmac = endpoint("hmac") { (key: Bytes<64, Plain>, data: Bytes<256, Plain>, v: Between<0, 4>) throws in
    let variants: [HMAC.Variant] = [.md5, .sha1, .sha2(.sha256), .sha2(.sha512), .sha3(.sha256)]
    _ = try HMAC(key: key.value, variant: variants[v.value]).authenticate(data.value)
    _ = try CMAC(key: key.value).authenticate(data.value)
}

let kdf = endpoint("kdf") { (password: Bytes<32, Plain>, salt: Bytes<32, Plain>, iterations: Between<1, 64>, keyLength: Between<0, 128>,
                             which: Between<0, 2>) throws in
    switch which.value {
    case 0: _ = try PKCS5.PBKDF2(password: password.value, salt: salt.value, iterations: iterations.value, keyLength: keyLength.value,
                                 variant: .sha2(.sha256)).calculate()
    case 1: _ = try HKDF(password: password.value, salt: salt.value, info: nil, keyLength: keyLength.value, variant: .sha2(.sha256)).calculate()
    default: _ = try Scrypt(password: password.value, salt: salt.value, dkLen: keyLength.value, N: 16, r: 1, p: 1).calculate()
    }
}

let parseText = endpoint("parse_text") { (text: Str<128, Plain>) throws in
    _ = [UInt8](hex: text.value)
    _ = try? text.value.decryptBase64ToString(cipher: try AES(key: Array(repeating: 1, count: 16), blockMode: ECB()))
    _ = Data(hex: text.value)
}

// RSA private keys from DER (the internal ASN.1 decoder); the representation parses back the same
let rsaKey = endpoint("rsa_key") { (der: Bytes<1024, Der>) throws in
    let key = try RSA(rawRepresentation: Data(der.value))
    let raw = try key.externalRepresentation()
    let again = try RSA(rawRepresentation: raw)
    try check(try again.externalRepresentation() == raw, "RSA externalRepresentation is stable")
}
