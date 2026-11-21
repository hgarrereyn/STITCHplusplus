package stitch;

/** Thrown by Stitch.check: an oracle failed (reported as a bug). */
public class CheckFailed extends Error {
    public final String what;

    public CheckFailed(String what) {
        super(what);
        this.what = what;
    }
}
