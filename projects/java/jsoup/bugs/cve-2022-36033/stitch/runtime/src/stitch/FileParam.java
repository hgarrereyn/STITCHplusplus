package stitch;

import java.lang.annotation.*;

/** A java.nio.file.Path parameter: a temporary file holding at most max bytes. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.PARAMETER)
public @interface FileParam {
    int max() default 4096;
    String type() default "";
}
