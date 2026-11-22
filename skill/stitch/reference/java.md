# Java harnesses

Stubs are `public static` methods annotated with `@Endpoint`. The method signature is the spec.
JDK 21 and Maven are in the image; the library and its dependencies go in `/deps` as jars. The
fuzzer talks to a JVM worker process.

## Project setup

```sh
stitch init commons-csv --lang java --git https://github.com/apache/commons-csv --rev rel/commons-csv-1.9.0
cd commons-csv
```

`Dockerfile`: build the library and put its jar and runtime dependencies in `/deps`.

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN mvn -q -B -DskipTests -Drat.skip=true -Dmaven.javadoc.skip=true package \
      dependency:copy-dependencies -DincludeScope=runtime -DoutputDirectory=/deps \
 && cp target/commons-csv-*[0-9].jar /deps/
```

- `-DincludeScope=runtime` leaves test dependencies (JUnit, Mockito) out of `/deps`.
- Copy only the main jar, not `-sources.jar` or `-tests.jar`.
- Apache projects run license checks during `package`: `-Drat.skip=true` turns them off.
- Gradle: `RUN ./gradlew jar && cp build/libs/*.jar /deps/`.

`stitch.toml`:

```toml
[project]
name = "commons-csv"
language = "java"

[source]
git = "https://github.com/apache/commons-csv"
rev = "rel/commons-csv-1.9.0"

[java]
classpath = ["/deps/*"]
include = ["org.apache.commons.csv"]     # packages of the code under test; empty: all non-JDK classes
sources = ["/src/src/main/java"]         # source roots for coverage reports
# jvm_options = ["-Xmx2g", "-Xss8m"]

[fuzz]
timeout_ms = 3000
```

`include` decides which classes get coverage and comparison tracing. Set it to the library's
packages.

## Files

```
harness/
  Formats.java         stubs and decoders; harness/*.java is compiled against the classpath
  Parse.java
  data/Csv/            seeds/, *.dict for the Csv data type (not compiled)
```

Keep `.java` files directly in `harness/`: files under `harness/data/` are not compiled, and
coverage reports count harness files in subfolders as library code.

## A complete example

```java
import java.io.IOException;
import java.io.StringReader;
import java.util.List;
import org.apache.commons.csv.*;
import stitch.*;

public class Stubs {
    @Endpoint
    @Obj("Format")                                // name of the returned object type
    public static CSVFormat predefined(CSVFormat.Predefined p) {   // an enum parameter: one of its constants
        return p.getFormat();
    }

    @Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
    public static CSVParser parse(@Obj("Format") CSVFormat format,
                                  @Str(max = 2048, type = "Csv") String text) throws IOException {
        return CSVParser.parse(text, format);
    }

    @Endpoint(optional = true, bailsOn = {IllegalStateException.class})
    public static CSVRecord next_record(CSVParser p) {
        var it = p.iterator();
        return it.hasNext() ? it.next() : null;     // null: no record, not a bail
    }

    @Endpoint
    public static void get_index(CSVRecord r, @Max(4) Source s) {
        Stitch.require(r.size() > 0, "empty record");
        int i = s.choice(r.size());                 // pick an existing field
        Stitch.check(java.util.Objects.equals(r.get(i), r.toList().get(i)), "get(i) matches values()");
    }

    @Endpoint(bailsOn = {IOException.class})
    public static void all_records(@Take CSVParser p) throws IOException {
        try { p.getRecords(); } finally { p.close(); }
    }
}
```

## Objects

| In the signature | Meaning |
|---|---|
| a parameter of a non-data type | uses an object; it stays available. Its type is the class's simple name |
| `@Obj("Name")` on a parameter or the method | names the object type (for the parameter, or for the returned object) |
| `@Take` on a parameter | the call consumes the object |
| `Stitch.consume(x)` | on a borrowed input: the library took ownership in this call |
| a non-void return | a new object; `null` is a bail |
| `@Endpoint(optional = true)` | the returned object may be `null`: no object, not a bail |

A stub returns at most one object. When the kind of result depends on the input, write one stub
per kind (`next_record` and `next_record_with_header`).

```java
@Endpoint(bailsOn = {IllegalStateException.class})
public static void drain(CSVParser p, @Range(lo = 0, hi = 8) int n) throws IOException {
    var it = p.iterator();
    for (int i = 0; i < n && it.hasNext(); i++) it.next();
    if (!it.hasNext()) {
        p.close();
        Stitch.consume(p);                          // closed: later calls must not use it
    }
}

@Drop                                               // runs on objects of this type left at the end
public static void close(CSVParser p) throws IOException { p.close(); }

@Init                                               // runs once per worker process
public static void setup() { java.util.Locale.setDefault(java.util.Locale.ROOT); }
```

## Fuzzed parameters

| Parameter | Value |
|---|---|
| `int`, `long`, `short`, `byte`, `char`, `boolean`, `double`, `float` (and boxed) | the value |
| `@Range(lo = 0, hi = 8) int n` | integer in [lo, hi] |
| `@OneOf(ints = {'"', '\'', 0}) int q` | one of the numbers (chars are ints) |
| `@OneOf(strings = {",", ";", "\t"}) String d` | one of the strings |
| `@Flags({1, 2, 4}) int opts` | OR of a subset |
| an enum type | one of its constants |
| `@Str(max = 2048, type = "Csv") String` | text of at most `max` bytes; `type` is an optional data type |
| `@Bytes(max = 2048, type = "Csv") byte[]` | bytes |
| `@FileParam(max = 4096, type = "Csv") Path` (or `File`) | temporary file holding the data |
| `@Val(value = "Row", max = 256) List<String>` | the value the `Row` decoder returns (any type) |
| `@Max(64) Source s` | a `stitch.Source` with 64 bytes of choices |

```java
@Endpoint(bailsOn = {IllegalArgumentException.class})
@Obj("Format")
public static CSVFormat customize(@Obj("Format") CSVFormat base,
                                  @OneOf(strings = {",", ";", "\t", "|"}) String delimiter,
                                  @OneOf(ints = {'"', '\'', 0}) int quote,
                                  QuoteMode mode,
                                  @Flags({1, 2, 4}) int opts) {
    return CSVFormat.Builder.create(base)
        .setDelimiter(delimiter)
        .setQuote(quote == 0 ? null : (char) quote)
        .setQuoteMode(mode)
        .setIgnoreEmptyLines((opts & 1) != 0)
        .setTrim((opts & 2) != 0)
        .setIgnoreSurroundingSpaces((opts & 4) != 0)
        .build();
}
```

## Bail, require, check

```java
throw Stitch.bail("reason");            // stop this call; not a bug
Stitch.require(cond, "reason");         // bail unless cond
Stitch.check(cond, "what");             // a bug if false: "check failed: what"
Stitch.log("n=%d", n);                  // printf-style; printed by `stitch exec --trace`
```

These are bugs: any `Throwable` not listed in `bailsOn` (`NullPointerException`,
`IndexOutOfBoundsException`, `ClassCastException`, `StackOverflowError`, `OutOfMemoryError`, ...),
a failed `Stitch.check`, and programs that run past `timeout_ms`. `bailsOn` matches subclasses.
List checked exceptions too: a stub that `throws IOException` needs `IOException.class` in
`bailsOn` if an I/O error on fuzzed input is not a bug.

## Attributes

```java
Stitch.set(p, "count", Stitch.getLong(p, "count", 0) + 1);   // any Object, lists included
Stitch.getLong(p, "count", 0);          // long, with a default
Stitch.getString(p, "name", "");
Stitch.get(p, "rows");                  // Object or null; a miss is reported by `stitch probe`
Stitch.has(p, "count");
Stitch.del(p, "count");
Stitch.globalSet("mode", 1);            // program-wide; reset for every program
Stitch.globalGet("mode");
```

Links to other objects:

```java
@Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
public static CSVParser parse(@Obj("Format") CSVFormat format, @Str(max = 2048, type = "Csv") String text) throws IOException {
    CSVParser p = CSVParser.parse(text, format);
    Stitch.set(p, "format", format);            // link the parser to its format
    return p;
}

@Endpoint(optional = true, bailsOn = {IllegalStateException.class})
public static CSVRecord next_record(CSVParser p) {
    var it = p.iterator();
    if (!it.hasNext()) return null;
    CSVRecord r = it.next();
    Stitch.set(r, "format", Stitch.getObj(p, "format", CSVFormat.class));   // null once consumed
    return r;
}
```

## Data types and decoders

Tag a `String`, `byte[]` or file parameter with `type = "Name"` and create
`harness/data/<Name>/` with `seeds/` (sample inputs) and `*.dict` files (one quoted token per
line). Decoders are static methods in any harness class (not under `harness/data/`):

```java
@Decoder("Csv")
public static String csv(Source s) {
    StringBuilder b = new StringBuilder();
    while (s.more()) {
        int n = (int) s.range(1, 6);
        for (int i = 0; i < n; i++) {
            if (i > 0) b.append(',');
            switch (s.choice(4)) {
                case 0: b.append(s.str(10)); break;
                case 1: b.append('"').append(s.str(10).replace("\"", "\"\"")).append('"'); break;
                case 2: b.append(s.token()); break;
                default: break;                      // empty field
            }
        }
        b.append(s.coin() ? "\r\n" : "\n");
    }
    return b.toString();
}
```

`stitch sample Csv -n 5` prints values the decoder makes.

A decoder can return any type (`String` or `byte[]` serve `@Str`/`@Bytes` parameters). Take
other types with `@Val`, and build decoders on each other with `s.draw("Name")`:

```java
@Decoder("Field")
public static String field(Source s) { return s.coin() ? s.str(8) : s.token(); }

@Decoder("Row")
public static List<String> row(Source s) {
    List<String> r = new ArrayList<>();
    do r.add(s.draw("Field")); while (s.more());
    return r;
}

@Endpoint(bailsOn = {IOException.class, IllegalArgumentException.class})
public static void print_row(CSVPrinter p, @Val(value = "Row", max = 256) List<String> row) throws IOException {
    p.printRecord(row);
}
```

`stitch.Source` methods:

| Method | Returns |
|---|---|
| `u8()` .. `u64()`, `i8()` .. `i64()`, `f32()`, `f64()` | a number |
| `range(lo, hi)` | `long` in [lo, hi] (cast for an int: `(int) s.range(1, 6)`) |
| `choice(n)` | `int` in [0, n) |
| `pick(a, b, c)`, `pick(list)` | one of the items |
| `coin()`, `coin(p)` | true with probability about p (default 0.5) |
| `more()` | loop condition |
| `f64(lo, hi)`, `f32(lo, hi)` | finite float in [lo, hi] |
| `str(max)`, `bytes(max)` | text or bytes of at most `max` bytes |
| `fixed(n)` | exactly n bytes |
| `rest()` | every byte that is left |
| `token()` | a token from the data type's `.dict` files |
| `draw("Name")` | a value from data type `Name`'s decoder |

## Reproducers

`stitch write c1` writes `.stitch/standalone/c1/Repro.java`, which calls the stubs with their
recorded arguments:

```java
var format1 = Standalone.call(1, "customize", () -> Formats.customize(format0, "|", 0, 92, 35, org.apache.commons.csv.QuoteMode.MINIMAL, 0));
```

Shrink a copy to plain library code and check it after each edit. `stitch try` compiles any
`.java` file against the classpath and runs the class it declares. End with
`throw new AssertionError("X")` for a failed check:

```sh
cp .stitch/standalone/c1/Repro.java .stitch/standalone/c1/Min.java
stitch try .stitch/standalone/c1/Min.java
```

```java
import java.io.StringReader;
import org.apache.commons.csv.CSVFormat;
import org.apache.commons.csv.CSVParser;
import org.apache.commons.csv.CSVPrinter;

public class Min {
    public static void main(String[] args) throws Exception {
        CSVFormat f = CSVFormat.Builder.create(CSVFormat.DEFAULT)
            .setQuote(null).setEscape('\\').setCommentMarker('#').build();
        StringBuilder out = new StringBuilder();
        try (CSVPrinter p = new CSVPrinter(out, f)) {
            p.printRecord("#");   // written as "#" with no escape: reads back as a comment line
        }
        int n = CSVParser.parse(new StringReader(out.toString()), f).getRecords().size();
        if (n != 1) throw new AssertionError("print/parse round trip");
    }
}
```

```
SAME CRASH: .stitch/standalone/c1/Min.java -> check failed: print/parse round trip
  16 code lines, plain library code
```

Details: `stitch guide reproducers`.

## Coverage

While fuzzing, a Java agent instruments the `include` packages. `stitch cov` uses JaCoCo.
