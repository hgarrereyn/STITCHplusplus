package stitch;

import java.lang.annotation.*;

/** A String parameter of at most max UTF-8 bytes, optionally of a data type. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Str {
    int max() default 256;
    String type() default "";
}
