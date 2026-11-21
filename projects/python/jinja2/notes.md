# Jinja2 3.0.0 (large project)

pallets/jinja, installed from source in the Python 3.12 image. 9 stubs over environments (all
constructor options fuzzed: autoescape, trim/lstrip, undefined kinds, extensions, line statements,
sandboxed, optimized) and compiled templates.

## Harness

- **Who is to blame for an exception.** Template errors (`TemplateError`), errors raised in template
  code itself, and errors where Jinja applies template data to Python operations (filters, tests,
  `cycler()`, iteration, calls) bail. Anything else raised inside Jinja's lexer, parser, compiler,
  optimizer, runtime or environment is a bug. This is decided from the innermost traceback frame.
- **Oracles.** The Python source Jinja generates must compile; sync and async rendering agree (same
  output or both fail); `render() == "".join(generate())`; macros and exports of `make_module` are
  callable; the AST lexer/meta helpers run on every parsed template.
- **Sandbox.** A Python audit hook watches sandboxed rendering for `os.*`, `subprocess`, `socket`,
  writes through `open`, `ctypes`, ...; a canary object returns a fresh random value from a private
  method that must never reach the output.
- **Data type.** JinjaText: a template grammar (blocks, macros, call blocks, filters, tests,
  inheritance, includes/imports, whitespace control), seeded with 400 templates from Jinja's tests.

## Harness fixes during triage

- **The fuzzer learned the sandbox marker.** It was a constant; cmplog copied it into a template.
  Now a new random value per render.
- **Object addresses** in reprs differ between runs: normalized before comparing.
- **Audit events Jinja raises itself** (`object.__setattr__`, `code.__new__` for traceback rewriting)
  removed from the watch list; `include` of a non-iterable is the template's fault.

## Results

28 buckets. Checked at 3.1.6: 2 fixed upstream (a parser AttributeError, an idtracking assertion),
about 20 still reproduce in a handful of classes: generated Python that does not compile
(`break` outside a loop, repeated keyword arguments, duplicate macro parameters), an optimizer
TypeError on unhashable dict keys, sync/async rendering disagreeing, KeyError/TypeError in async
error handling. 3 were harness noise (fixed).
