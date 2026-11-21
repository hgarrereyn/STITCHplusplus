package stitch;

import java.lang.annotation.*;

/** A decoder for a data type: public static String or byte[] method(stitch.Source). */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Decoder {
    String value();
}
