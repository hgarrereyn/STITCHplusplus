# CryptoSwift (krzyzanowskim/CryptoSwift @ 1.6.0)

10 stubs: `aes_roundtrip` and `aes_decrypt` (all block modes, all paddings, fuzzed key and IV
sizes), `stream_ciphers` (ChaCha20, Rabbit, Blowfish), `padding` and `remove_padding`, `hashes`,
`hmac` (+ CMAC), `kdf` (PBKDF2, HKDF, scrypt), `parse_text` (hex, base64), `rsa_key` (an RSA key
from DER, through the library's internal ASN.1 decoder). Decoder: `Der` (TLVs with short, long
and indefinite lengths, nested SEQUENCE/SET). A thrown error is a bail; traps are bugs. Oracles:
`decrypt(encrypt(m)) == m`, `remove(add(m)) == m`, an RSA key's DER representation parses back
the same.

Campaign: 15 min, 2 workers: 594k execs (659/s), 3.5k edges, 9 buckets.

## Library bugs

Checked against 1.10.0 (the latest release): all 9 buckets still reproduce.

1. **AES-GCM decryption of a short ciphertext traps.** In combined mode, a ciphertext shorter than
   the 16-byte tag (2 bytes) makes `BlockDecryptor.update` take a prefix of negative length
   instead of throwing (c5). Anyone who decrypts attacker-supplied data with AES-GCM can be made
   to crash. Reproducer: `bugs/gcm-short-ciphertext/`.
2. **AES-CCM decryption of a short ciphertext traps** ("Negative value is not representable" in
   `CCMModeWorker.init`) (c2; the harness passes a message length of 0). Reproducer:
   `bugs/ccm-short-ciphertext/`.
3. **Invalid parameters trap instead of throwing**: `ChaCha20(key:iv:)` with some IV lengths
   (`precondition`, c1), `Scrypt` (c4), `HKDF.calculate()` and PBKDF2 with a key length of 0 (range
   traps, c7, c8). These initializers are `throws` and throw for other invalid input.

## Harness bugs found and fixed

- `Padding.add/remove` with a block size of 0 divides by zero (c3, c6, c9): a block size of 0 is
  the caller's mistake. The stubs now start block sizes at 1.
- The library's ASN.1 decoder is internal in 1.6.0, so DER is reached only through
  `RSA(rawRepresentation:)`.
