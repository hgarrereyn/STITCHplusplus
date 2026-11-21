package stitch;

import java.lang.annotation.*;

/** Object type name for a parameter or return value (default: the class simple name). */
@Retention(RetentionPolicy.RUNTIME)
@Target({ElementType.PARAMETER, ElementType.METHOD})
public @interface Obj {
    String value();
}
