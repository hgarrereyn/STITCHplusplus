package stitch;

import java.lang.annotation.*;

/** Runs once per worker process, after the harness is loaded. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface Init {
}
