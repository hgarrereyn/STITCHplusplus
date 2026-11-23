package stitch;

import java.lang.annotation.*;

/**
 * A parameter whose value is built by a data type's decoder: {@code @Val("Polygon") List<Point> p}
 * with {@code @Decoder("Polygon") public static List<Point> polygon(Source s)}. The decoder can
 * return any type; the parameter holds at most max bytes of choices.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Val {
    String value();

    int max() default 1024;
}
