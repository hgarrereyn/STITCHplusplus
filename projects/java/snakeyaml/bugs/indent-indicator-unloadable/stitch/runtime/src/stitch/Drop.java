package stitch;

import java.lang.annotation.*;

/** Frees an object left over at the end of a program: public static void method(T obj). */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Drop {
    String value() default "";
}
