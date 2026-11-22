package stitch;

import java.lang.annotation.*;

/** A decoder for a data type: public static T method(stitch.Source). String or byte[]: values for
 * String / byte[] parameters of that type; any other T: values for {@code @Val} parameters. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Decoder {
    String value();
}
