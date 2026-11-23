package stitch;

import java.lang.annotation.*;

/** An int or long parameter: the bitwise OR of a subset of the flags. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Flags {
    long[] value();
}
