# Java harnesses

Stubs are public static methods annotated with `@Endpoint`. JDK 21 and Maven are in the image; the
library and its dependencies go in `/deps` as jars.

## Project

```sh
stitch init jsoup --lang java --git https://github.com/jhy/jsoup --rev jsoup-1.15.2
```

```toml
[project]
language = "java"

[java]
classpath = ["/deps/*"]
include = ["org.jsoup"]                  # packages to instrument
sources = ["/src/src/main/java"]         # for coverage reports
```

```dockerfile
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN mvn -q -B -DskipTests package dependency:copy-dependencies -DoutputDirectory=/deps && cp target/*.jar /deps/
```

## Stubs

`harness/**/*.java` is compiled with the classpath (and `-parameters`, so names reach the manifest).

```java
import org.jsoup.Jsoup;
import org.jsoup.nodes.Document;
import org.jsoup.nodes.Element;
import stitch.*;

public class Stubs {
    @Endpoint
    public static Document parse(@Str(max = 4096, type = "Html") String html) {
        return Jsoup.parse(html);
    }

    @Endpoint(optional = true, bailsOn = {IllegalArgumentException.class})
    public static Element select(Document doc, @Str(max = 64) String css, @Range(lo = 0, hi = 8) int i) {
        var els = doc.select(css);
        return els.isEmpty() ? null : els.get(i % els.size());
    }

    @Endpoint
    public static void remove(@Take Element e) {
        Stitch.require(e.parent() != null, "detached");
        e.remove();
    }
}
```

| In the signature | Meaning |
|---|---|
| a parameter of a non-data type | an object input (its class simple name is the type; `@Obj("Name")` renames) |
| `@Take` on it | the call consumes it |
| a non-void return | a new object (`null` is a bail unless `@Endpoint(optional = true)`) |
| `int`, `long`, `short`, `byte`, `char`, `boolean`, `double`, `float` (and boxed) | fuzzed numbers |
| `@Range(lo, hi)`, `@OneOf(ints = {...})`, `@OneOf(strings = {...})`, `@Flags({...})` | constrained values |
| an enum | one of its constants |
| `String` (`@Str(max, type)`), `byte[]` (`@Bytes`), `Path`/`File` (`@FileParam`) | buffers |
| `stitch.Source` (`@Max(n)`) | choices inside the stub |

Also: `@Decoder("Type") static String f(Source s)`, `@Init static void setup()`,
`@Drop static void close(T obj)`, and `Stitch.bail/require/check/log/consume/set/get/getObj`.

## Bugs

Any `Throwable` not listed in `bailsOn`: `NullPointerException`, `IndexOutOfBoundsException`,
`ClassCastException`, `StackOverflowError`, `OutOfMemoryError`, ... and failed `Stitch.check`.

## Coverage and comparisons

While fuzzing, a Java agent instruments the `include` packages (counters per basic block, int
comparisons, `switch`, `String.equals`/`startsWith`/`contains`, `Map.get`). `stitch cov` uses JaCoCo.

## Reproducers

`stitch write c1` writes `Repro.java`, calling the stubs (`var doc0 = Standalone.call(0, "parse",
() -> Stubs.parse("..."));`). `stitch try` compiles any `.java` file (class name taken from the file)
against the classpath; a plain minimized file is compared through the JVM's stack trace.
