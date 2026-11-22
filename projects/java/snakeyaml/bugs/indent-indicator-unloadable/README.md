# snakeyaml: AssertionError in Stubs.dump_all_roundtrip

| | |
|---|---|
| Kind | `AssertionError` |
| Location | `Stubs.dump_all_roundtrip (stub)` |
| Library source | [https://bitbucket.org/snakeyaml/snakeyaml](https://bitbucket.org/snakeyaml/snakeyaml) @ `49227c24d741` |
| Reproduce | `./run.sh` (needs only Docker; builds the library and the harness from scratch) |
| Verified | rebuilt from scratch with `run.sh`: same crash |
| Crash id | `snakeyaml-run3-c6` |

## Standalone reproducer

`standalone/min.java` (17 lines of code) needs only the library: no harness, no STITCH++ runtime.

```java
import java.util.*;
import org.yaml.snakeyaml.*;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.representer.Representer;

public class min {
    public static void main(String[] args) {
        DumperOptions d = new DumperOptions();
        d.setDefaultFlowStyle(DumperOptions.FlowStyle.BLOCK);
        d.setIndent(10);
        d.setIndicatorIndent(7);
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("b", Arrays.asList("x", Collections.singletonMap("z", 1)));
        String out = new Yaml(new Representer(d), d).dump(m);
        System.out.println(out);
        new Yaml().load(out);   // ParserException: expected <block end>, but found '<block mapping start>'
    }
}
```

Run it: `./run.sh --standalone` (builds the library at the pinned commit and runs this file in the project image).

`standalone/generated.java` (10 lines of code) is what `stitch write` produced before minimization; `stitch try` confirmed that the minimized file crashes the same way (`standalone/expected.json`).

## Call sequence (3 calls)

Each line is one harness stub call with its decoded arguments; `oN.K` is output K of call N.

```
#0 yaml_options(bits=130, flow=org.yaml.snakeyaml.DumperOptions.FlowStyle.BLOCK, scalar=org.yaml.snakeyaml.DumperOptions.ScalarStyle.SINGLE_QUOTED, indent=10, indicator=7, width=20, lineBreak=org.yaml.snakeyaml.DumperOptions.LineBreak.MAC, maxAliases=50, maxSimpleKey=3, nps=org.yaml.snakeyaml.DumperOptions.NonPrintableStyle.ESCAPE)
#1 load_all(y=o0.0, text=Str[7]"[y,{1}]")
#2 dump_all_roundtrip(y=o0.0, docs=o1.0)
```

## Stubs used

What each call does (from `project/harness/`):

`yaml_options` · harness/Stubs.java

```java
    @Endpoint(bailsOn = {YAMLException.class})
    @Obj("Yaml")
    public static Yaml yaml_options(@Flags({1, 2, 4, 8, 16, 32, 64, 128, 256, 512}) int bits,
```

`load_all` · harness/Stubs.java

```java
    @Endpoint(bailsOn = {YAMLException.class})
    @Obj("DataList")
    public static List<Object> load_all(Yaml y, @Str(max = 1024, type = "YamlText") String text) {
        List<Object> out = new ArrayList<>();
        try { for (Object o : y.loadAll(text)) out.add(o); }
        catch (YAMLException e) { throw e; }
        catch (RuntimeException e) {
            for (StackTraceElement f : e.getStackTrace()) if (f.getClassName().contains("Constructor")) throw Stitch.bail("constructor leak");
            throw e;
        }
        return out;
    }
```

`dump_all_roundtrip` · harness/Stubs.java

```java
    @Endpoint
    public static void dump_all_roundtrip(Yaml y, @Obj("DataList") List<Object> docs) {
        Stitch.require(!docs.isEmpty(), "no docs");
        for (Object d : docs) requirePrintable(d, new IdentityHashMap<>(), Stitch.getLong(y, "esc", 0) == 1);
        String out;
        try { out = y.dumpAll(docs.iterator()); } catch (YAMLException e) { if (String.valueOf(e.getMessage()).contains("invalid string value")) throw Stitch.bail("lone surrogate"); throw e; }
        List<Object> back = new ArrayList<>();
        try {
            for (Object o : new Yaml(new SafeConstructor(new LoaderOptions())).loadAll(out)) back.add(o);
        } catch (RuntimeException e) {
            throw new AssertionError("dumpAll output does not load: " + e.getMessage().replace('\n', ' '));
        }
        Stitch.check(back.size() == docs.size(), "dumpAll/loadAll document count");
        for (int i = 0; i < back.size(); i++)
            Stitch.check(eq(docs.get(i), back.get(i), new IdentityHashMap<>()), "loadAll(dumpAll(x))[i] equals x");
    }
```

## Report

```
[stitch] EXCEPTION in dump_all_roundtrip: AssertionError: dumpAll output does not load: expected '<document start>', but found '<block mapping start>'  in 'reader', line 4, column 3:       ? !!int '1'       ^ 
    #0 0x0 in Stubs.dump_all_roundtrip harness/Stubs.java:521
```

## Files

- `run.sh`: builds the toolchain image, fetches the pinned source, builds the library with the project `Dockerfile`, compiles the harness and runs `crash.stp`
- `project/`: `Dockerfile`, `stitch.toml` and the harness stubs
- `stitch/`: the STITCH++ java toolchain (Dockerfile, runtime, build scripts); no fuzzer needed
- `crash.stp`: the crashing program; `trace.txt`: the full recorded output
- `standalone/`: the same bug without the fuzzer (`./run.sh --standalone`), if one was written

