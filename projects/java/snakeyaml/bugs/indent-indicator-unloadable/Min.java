import java.util.*;
import org.yaml.snakeyaml.*;
import org.yaml.snakeyaml.constructor.SafeConstructor;
import org.yaml.snakeyaml.representer.Representer;

public class Min {
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
