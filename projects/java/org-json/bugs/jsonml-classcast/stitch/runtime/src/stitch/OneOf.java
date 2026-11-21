package stitch;

import java.lang.annotation.*;

/** One of the listed values: ints = {...} for int/long parameters, strings = {...} for String. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface OneOf {
    long[] ints() default {};
    String[] strings() default {};
}
