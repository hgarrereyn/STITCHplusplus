# toml_edit (toml-rs/toml @ v0.19.15)

6 stubs: `parse`, `new_doc`, `set` (a dotted path to an integer, float, string, bool, array,
inline table, table or array of tables), `remove`, `format_ops` (`fmt`, `sort_values`,
`set_implicit`, `clear`) and `decor` (the whitespace or comment before a key). Object: `Doc`
(`Document`). Decoder: `TomlText` (tables, arrays of tables, comments, and keys and values of
every kind, including literal and multi-line strings). Oracles: parse then print reproduces the
input text, and after every edit the printed document parses and prints the same again. The
oracle logs the printed text when it fails (`stitch exec <id> --trace`).

Campaign: 15 min, 2 workers: 1.8M execs (2.0k/s), 3768 edges, 1 bucket.

## Library bug

**Multi-line literal strings with CRLF don't print back as parsed.** `'lit' = '''\r\n'''` and
`x-y = '''"""\r\n'''` print differently from their input. Fixed by 0.23 (checked against 0.23.10).

## Harness bugs found and fixed

- **Decor inside table headers.** A table's key sits inside `[...]`, where TOML allows only spaces
  and tabs. Setting a newline or a comment as that key's decor made unparseable output (3
  buckets). toml_edit leaves decor validity to the caller, so `decor` now uses only spaces and
  tabs for table keys.
- **`set` turning a key into a table** kept the key's old decor (valid for `key = value`, not in a
  header). The key is now removed first, so the new table gets a fresh one.
