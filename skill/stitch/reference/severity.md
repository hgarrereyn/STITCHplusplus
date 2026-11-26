# Severity

Rate every library bug after [triage](triage.md) confirms it. The rating says how much the bug
matters to people who use the library. It does not measure how interesting the bug is to find.

Most STITCH++ findings need an unusual chain of API calls and end in a crash. Those are real bugs,
but they rarely matter. Rate them low. Save high ratings for bugs that an attacker can trigger
with data, through the way the library is meant to be used, with a result worse than a crash.

## The label

Every bug gets a **class** and a **priority**:

| Class | Meaning |
|---|---|
| `security` | Someone who controls an input the library is designed to handle gains a capability they did not have: running code, reading or writing memory or data they should not, getting past a check, or taking down a service other people use. |
| `functional` | The library does the wrong thing (a crash, a wrong result, lost data, a failed `check`), but no plausible attacker gains anything. |
| `nit` | Cosmetic or contract-level: a wrong exception type, a misleading error, output that is valid but not canonical, a leak of a few bytes. |

| Priority | Name | Meaning |
|---|---|---|
| `P0` | critical | Patch now. Remote attacker, data alone, intended usage, code execution or controlled memory corruption or a bypass of a security feature. |
| `P1` | high | Patch in the next release. Serious impact with one weakening condition, or a likely silent data corruption. |
| `P2` | medium | Worth fixing. A denial of service from crafted data, or corruption that needs several conditions to line up. |
| `P3` | low | Fix when convenient. Edge cases, unusual API sequences, failures a caller sees and handles. |

Allowed combinations: `security P0` to `security P3`, `functional P1` to `functional P3`, and `nit`
(no priority). A functional bug is never P0. A bug you cannot tie to a real attacker is not
`security`, however bad the crash looks.

## Step 1: Find the threat model

Before rating, write down who the attacker is and what they control. Take the first source that
answers:

1. **Your instructions.** A scope given in the task or the campaign brief wins.
2. **The project.** A `## Threat model` section in the project's `notes.md`, if there is one.
3. **The library.** `SECURITY.md`, the security policy, the docs, and past advisories (CVEs,
   RUSTSEC, GHSA). A statement such as "safe to use on untrusted input" or "do not pass untrusted
   templates" sets the scope. Bug classes that upstream has published advisories for are in scope.
4. **The library's kind**, if nothing above answers:

| Kind of library | Usually untrusted | Usually trusted |
|---|---|---|
| Parser or decoder of an interchange format (JSON, XML, YAML, images, archives, protocols) | the bytes being parsed | options, the API sequence |
| Configuration format (TOML, INI) | rarely anything: the file is written by the operator | the file |
| HTTP, URL, email, MIME | headers, URLs, bodies from the network | server configuration |
| Sanitizer, escaper, cleaner | all of the input, by design | the policy |
| Template engine | the data rendered into a template | the template itself |
| Crypto | ciphertext, signatures, peer messages | keys, parameters |
| Script interpreter (QuickJS, Lua) | scripts, if the embedding sandboxes them | the host program |
| Regex | the subject string; the pattern only if the library says patterns may be untrusted | the pattern |
| Data structure, math, utility | rarely anything | everything: values come from the programmer |

Write the threat model in one sentence:

> A remote, unauthenticated client controls the YAML body of a request that the service
> unmarshals; they gain the ability to crash the worker process.

If you cannot write that sentence with an attacker who **gains** something, the bug is not
`security`. A capability the attacker already has does not count. A command injection reachable
only by a logged-in shell user gives that user nothing new. A crash that only fails the
attacker's own request takes nothing away from anyone else.

## Step 2: Assess the factors

Rate each factor and keep the ratings for the report.

| Factor | Question | Strong | Weak |
|---|---|---|---|
| **Impact** | What is the worst realistic outcome? | code execution, controlled memory write, bypass of a security check | a crash that one request sees and handles, a wrong error type |
| **Trigger** | What sets it off? | crafted data passed to a normal call (a parser bug) | a specific sequence of API calls, whatever the data |
| **Path** | Is it the intended way to use the library? | the main entry points, with default options | a non-default option, a legacy or deprecated API, debug helpers, a call order the docs discourage |
| **Exposure** | Where does the input come from in real deployments? | unauthenticated network users | the programmer, or the local user running the program on their own files |
| **Gain** | Does the attacker get something new? | yes: code execution, data, or an outage for others | no: they could already do it |

Why trigger matters so much: a bug that an API sequence triggers alone fires every time that code
runs. The developer sees it in testing, or never writes that sequence. A bug that depends on the
data hides in deployed code until someone sends the input that triggers it.

### Impact by crash kind

Read the report and the source at the crash site. The kind of crash only sets a starting point.

| Report | Impact to start from |
|---|---|
| ASan WRITE (heap, stack or global buffer overflow, use-after-free), double free, invalid free | memory corruption. Controlled if the attacker's data chooses what is written or where |
| ASan READ out of bounds or after free | disclosure if the value read can reach output (a returned string, a script value); otherwise a crash |
| SEGV near address 0 | null dereference: a crash |
| SEGV at a wild address | investigate: it can be a corrupted pointer |
| Stack overflow from recursion, abort, assertion, Swift trap, Rust panic | a crash. It kills the process unless the language recovers it (a Go `net/http` handler recovers a panic; a C abort or a Swift trap is never recovered) |
| Python, Ruby, Java or JavaScript exception | usually a failed request: the framework catches it. An `OutOfMemoryError` or `StackOverflowError` can take the process |
| Timeout | a hang or superlinear time. In Node.js it blocks every request, since there is one event loop |
| Leak | `nit`, unless memory grows with each input that an attacker can send repeatedly |
| UBSan signed overflow, shift | `nit` or `functional`, unless the value becomes a size or an index |
| Failed `check` | `functional`. Then read [Logic bugs](#logic-bugs) |

## Step 3: Derive the priority

1. **Start from impact:**

   | Impact | Start at |
   |---|---|
   | code or command execution; controlled memory write; bypass of sanitizing, escaping, authentication or crypto | P0 |
   | uncontrolled memory corruption; disclosure of memory or data; prototype pollution; a security-relevant logic bug (below) | P1 |
   | denial of service: a crash that kills the process, a hang, superlinear time, unbounded memory | P2 |
   | a failure the caller sees and handles: an exception in one request, an error return | P3 |

2. **Lower it one level for each** of these that applies:
   - the trigger needs a specific API sequence or a non-default option, not just data
   - the path is uncommon, legacy, deprecated or discouraged
   - the attacker must control an input that is usually trusted (a template, a regex pattern, a
     config file), and the library does not say that input may be untrusted
   - the bug only matters if an application relies on the library for a security property the
     library does not promise
3. **Stop at P3.** If no attacker gains anything (step 1), the class is `functional` or `nit`,
   rated with [Logic bugs](#logic-bugs) below.
4. **Do not lower it** for a factor the library's threat model makes normal. Rust's `regex` says
   that patterns may be untrusted, so a pattern that explodes at compile time is not lowered for
   "the attacker controls the pattern".
5. **When torn between two levels, pick the lower** and say what evidence would raise it (a
   working write primitive, a known user that passes untrusted input to that call).

A bug fixed in the latest release keeps its priority. Report its status separately (below).

## Logic bugs

Failed `check`s are wrong results: a round trip that changes the value, two functions that
disagree, output that does not parse back. Most are `functional`. Look for a threat model first.
A logic bug is `security` when an application makes a security decision with the result.
Typical shapes:

| Shape | Threat model |
|---|---|
| A sanitizer or escaper whose output, parsed again, contains active content | XSS: the application trusts the output to be safe (P0 or P1) |
| Two parsers or two entry points disagree on the same input (URLs, HTML, MIME headers) | a validator checks one reading and the application acts on the other: SSRF, request smuggling, filter bypass (P1 or P2) |
| Validate-then-use pairs disagree (`isValid` accepts what `parse` rejects or reads differently) | a check passes on a value that is then used with another meaning (P2 or P3) |
| Path or key handling that writes outside the intended object (`__proto__`, `../`) | prototype pollution or path traversal (P1) |
| Serialization that changes data, where the data is signed, hashed or compared | a signature or a check covers different content than the one used (P1 or P2) |
| Crypto that accepts an invalid tag, signature or padding | forgery (P0) |

With no security angle, rate the bug for the users it hits:

| Priority | When |
|---|---|
| `functional P1` | Silent data loss or corruption on input that ordinary users produce, through the intended path. |
| `functional P2` | Silent wrong results on uncommon but valid input, or a loud failure (a crash, an exception) on ordinary usage. |
| `functional P3` | Edge cases: unusual API sequences, extreme values, misuse the docs do not quite forbid. |
| `nit` | Output that is valid but not canonical or not idempotent, wrong exception types, error message text. |

Silent beats loud: a crash gets noticed and worked around, and data dropped without an error
does not.

## In the report

Add three lines to each library bug in the report (step 9 of the [overview](overview.md)):

```markdown
- **Severity:** security P2
- **Threat model:** <attacker> controls <input> passed to <call>; they gain <capability>.
- **Factors:** impact=<...>; trigger=data|API sequence; path=intended|non-default|legacy;
  exposure=network|local|programmer; status=HEAD|latest release|fixed in <version>
```

For a `functional` bug, the threat model line is "none: <why no attacker gains anything>". Sort the
report by priority, `security` before `functional` at the same priority, and `nit`s last.

## Done when

Every library bug has a label, a one-sentence threat model, and the factors that led to it.
