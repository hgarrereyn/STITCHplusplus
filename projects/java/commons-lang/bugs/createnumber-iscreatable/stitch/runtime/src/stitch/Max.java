package stitch;

import java.lang.annotation.*;

/** Size limit of a stitch.Source parameter (bytes of choices). */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface Max {
    int value();
}
