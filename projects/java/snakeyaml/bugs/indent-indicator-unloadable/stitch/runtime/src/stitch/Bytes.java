package stitch;

import java.lang.annotation.*;

/** A byte[] parameter of at most max bytes, optionally of a data type. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Bytes {
    int max() default 256;
    String type() default "";
}
