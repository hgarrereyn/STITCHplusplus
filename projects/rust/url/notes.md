# url (servo/rust-url @ v2.2.0)

7 stubs: `parse`, `join`, `set` (scheme, host, path, query, fragment, username, password, port, IP
host), `clear` (query, fragment, host, password), `path_segments` (`push`, `pop`,
`pop_if_empty`, `clear`), `query_pairs`, `inspect`. Object: `U` (a `Url`). Decoder: `UrlText`
(schemes such as `file:`, `mailto:`, `data:` and special ones, hosts like IPv6, IPv4,
`0x7f.1`, punycode, percent-escapes, dot segments). Oracle: `Url::parse(u.as_str()) == u` and
the serialization is stable, after parsing, joining and every setter. Setters that return
`Err` are documented refusals.

Campaign: 15 min, 2 workers: 1.1M execs (1.2k/s), 4031 edges, 18 buckets.

## Library bugs

Checked against v2.5.9 (the latest release) with `stitch exec <id> --rev v2.5.9`.

Still present in v2.5.9:
1. **Setter assertion failures.** `set_host(None)` on a URL whose path was edited asserts
   `self.byte_at(self.path_start) == b'/'` (lib.rs). After `set_host(None)` and other setters,
   `set_username` asserts `self.slice(self.scheme_end..username_start) == "://"`, or slices out
   of bounds ("begin <= end (5 <= 3)", "byte index 5 is out of bounds of `ps:/`"), and
   `set_password(None)` asserts `has_username_or_password`. That's 5 buckets. Reproducer:
   `bugs/set-host-assert/`.
2. **The URL after a setter does not parse back to itself.** `clear` (`set_host(None)`) on a URL
   with a dotted path leaves a serialization that does not parse, or parses to a different URL
   (2 buckets).

Fixed between v2.2.0 and v2.5.9 (11 buckets):
- `PathSegmentsMut::pop` / `pop_if_empty` byte-index panics on cannot-be-a-base and odd `file:`
  URLs.
- An assertion in `Parser::parse_file` when joining `..` onto some `file:` URLs.
- Round-trip failures after `set_path`, `set_password`, `set_ip_host` and `join`.

## Harness notes

- The first version resolved hosts (`socket_addrs`), which made DNS lookups. It was removed.
