package stitch;

/** Thrown by Stitch.bail: this call stops; not a bug. An Error, so library catch blocks skip it. */
public class Bail extends Error {
    public final String reason;

    public Bail(String reason) {
        super(reason, null, false, false);
        this.reason = reason;
    }
}
