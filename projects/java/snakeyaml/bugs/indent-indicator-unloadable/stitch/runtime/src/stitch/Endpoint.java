package stitch;

import java.lang.annotation.*;

/** A stub: a public static method. Its signature is the spec. Exceptions listed in bailsOn are not bugs. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Endpoint {
    String name() default "";
    Class<? extends Throwable>[] bailsOn() default {};
    /** The returned object may be null (an optional output). */
    boolean optional() default false;
}
