# handlebars 4.7.6 (large project)

handlebars.js. Its parser is generated at release time (jison), so the image installs the published
npm package of the version in the checked-out `package.json` (this still follows `--rev`).

## Harness

- **Stubs.** `env` (an isolated `Handlebars.create()` with helpers and partials), `register_partial`,
  `compile` (9 compile options fuzzed: strict, assumeObjects, knownHelpersOnly, noEscape, compat,
  preventIndent, explicitPartialContext, ignoreStandalone, data), `render` (fuzzed context and
  runtime options incl. prototype access), `parse_print`, `canary`.
- **Oracles.** The precompiled template is valid JavaScript (a SyntaxError means template text got
  into the generated code); rendering never runs planted code (a global canary); `compile()` and
  `precompile()` + `template()` render the same; the runtime reports prototype pollution.
- **Errors.** `Handlebars.Exception`, jison parse errors, helper errors, stack exhaustion from
  self-including partials, and TypeErrors in strict/assumeObjects modes bail.

## Harness fixes during probing

- **Block helpers used inline** (`{{if x}}`, `(with 1)`) throw TypeErrors by design: the generator
  now only puts block helpers in block position, and TypeErrors thrown inside the built-in helpers'
  own files are treated as misuse.

## Results

23 buckets. Checked at 4.7.10: the code-injection class (template text breaking out of the
generated JavaScript, CVE-2021-23369's root cause) and a resolvePartial TypeError are fixed upstream
(6 buckets). Still reproducing: `compile()` and `precompile()`+`template()` rendering differently,
and TypeErrors instead of Handlebars errors for calls of non-functions, unknown decorators and
`@partial-block` with arguments.
