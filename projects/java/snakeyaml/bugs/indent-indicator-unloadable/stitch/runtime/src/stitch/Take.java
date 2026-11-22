package stitch;

import java.lang.annotation.*;

/** An object input that the call consumes. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Take {
}
