package stitch;

import java.lang.annotation.*;

/** An int or long parameter in [lo, hi]. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Range {
    long lo();
    long hi();
}
