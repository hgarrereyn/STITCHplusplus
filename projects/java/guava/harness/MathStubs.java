import com.google.common.math.*;
import java.math.BigInteger;
import java.math.RoundingMode;
import stitch.*;

/** Guava math: overflow-checked, saturated and exact integer arithmetic against BigInteger; rounding modes against BigDecimal. */
public class MathStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    static final RoundingMode[] MODES = RoundingMode.values();
    static long lv(Source s) { return s.coin(0.3) ? (long) s.pick(Long.MIN_VALUE, Long.MAX_VALUE, 0L, -1L, 1L << 62, (1L << 31) - 1) : s.range(-100000, 100000) * (s.coin() ? 1 : 65537L); }
    static int iv(Source s) { return s.coin(0.3) ? (int) s.pick(Integer.MIN_VALUE, Integer.MAX_VALUE, 0, -1, 1 << 30) : ri(s, -100000, 100000); }

    @Endpoint
    public static void intmath_checked_add(@stitch.Max(32) Source s) {
        int a = iv(s), b = iv(s);
        BigInteger exact = BigInteger.valueOf(a).add(BigInteger.valueOf(b));
        try {
            int r = IntMath.checkedAdd(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedAdd is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0), "checkedAdd throws only on overflow");
        }
        int sat = IntMath.saturatedAdd(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedAdd clamps");
    }

    @Endpoint
    public static void intmath_checked_subtract(@stitch.Max(32) Source s) {
        int a = iv(s), b = iv(s);
        BigInteger exact = BigInteger.valueOf(a).subtract(BigInteger.valueOf(b));
        try {
            int r = IntMath.checkedSubtract(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedSubtract is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0), "checkedSubtract throws only on overflow");
        }
        int sat = IntMath.saturatedSubtract(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedSubtract clamps");
    }

    @Endpoint
    public static void intmath_checked_multiply(@stitch.Max(32) Source s) {
        int a = iv(s), b = iv(s);
        BigInteger exact = BigInteger.valueOf(a).multiply(BigInteger.valueOf(b));
        try {
            int r = IntMath.checkedMultiply(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedMultiply is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0), "checkedMultiply throws only on overflow");
        }
        int sat = IntMath.saturatedMultiply(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedMultiply clamps");
    }

    @Endpoint
    public static void intmath_pow(@stitch.Max(16) Source s) {
        int b = (int) ri(s, -10, 10);
        int k = ri(s, 0, 70);
        BigInteger exact = BigInteger.valueOf(b).pow(k);
        int p = IntMath.pow(b, k);
        Stitch.check(BigInteger.valueOf(p).equals(exact.mod(BigInteger.ONE.shiftLeft(8*4)).subtract(exact.mod(BigInteger.ONE.shiftLeft(32)).testBit(31) ? BigInteger.ONE.shiftLeft(32) : BigInteger.ZERO)), "pow wraps like repeated multiplication");
        try {
            int c = IntMath.checkedPow(b, k);
            Stitch.check(BigInteger.valueOf(c).equals(exact), "checkedPow is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Integer.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) <= 0), "checkedPow throws only on overflow");
        }
        Stitch.check(BigInteger.valueOf(IntMath.saturatedPow(b, k)).equals(exact.max(BigInteger.valueOf(Integer.MIN_VALUE)).min(BigInteger.valueOf(Integer.MAX_VALUE))), "saturatedPow clamps");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void intmath_sqrt_log(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        int x = iv(s);
        RoundingMode rm = MODES[mode];
        try {
            int r = IntMath.sqrt(x, rm);
            BigInteger exactFloor = BigInteger.valueOf(x).sqrt();
            boolean perfect = exactFloor.multiply(exactFloor).equals(BigInteger.valueOf(x));
            Stitch.check(BigInteger.valueOf(r).subtract(exactFloor).abs().compareTo(BigInteger.ONE) <= 0, "sqrt is the floor or the ceiling");
            if (perfect) Stitch.check(BigInteger.valueOf(r).equals(exactFloor), "sqrt of a perfect square is exact");
        } catch (ArithmeticException e) {
            Stitch.check(rm == RoundingMode.UNNECESSARY, "sqrt throws ArithmeticException only for UNNECESSARY");
        }
        try {
            int l2 = IntMath.log2(x, rm);
            int bl = BigInteger.valueOf(x).bitLength();
            Stitch.check(l2 == bl - 1 || l2 == bl, "log2 is the floor or the ceiling");
            int l10 = IntMath.log10(x, rm);
            Stitch.check(l10 >= 0 && l10 <= 19, "log10 in range");
        } catch (ArithmeticException e) {
            Stitch.check(rm == RoundingMode.UNNECESSARY, "log throws ArithmeticException only for UNNECESSARY");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, ArithmeticException.class})
    public static void intmath_divide(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        int a = iv(s), b = iv(s);
        RoundingMode rm = MODES[mode];
        Stitch.require(!(a == Integer.MIN_VALUE && b == -1), "MIN_VALUE / -1 overflows (wraps like Java's /)");
        int q = IntMath.divide(a, b, rm);
        java.math.BigDecimal exact = new java.math.BigDecimal(a).divide(new java.math.BigDecimal(b), 0, rm);
        Stitch.check(exact.toBigIntegerExact().equals(BigInteger.valueOf(q)), "divide rounds like BigDecimal (" + a + " / " + b + " " + rm + ")");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, ArithmeticException.class})
    public static void intmath_number_theory(@stitch.Max(16) Source s) {
        int a = (int) Math.abs((long) iv(s) % 1000000), b = (int) Math.abs((long) iv(s) % 1000000);
        Stitch.check(BigInteger.valueOf(IntMath.gcd(a, b)).equals(BigInteger.valueOf(a).gcd(BigInteger.valueOf(b))), "gcd");
        int m = (int) (b == 0 ? 1 : b);
        Stitch.check(BigInteger.valueOf(IntMath.mod((int) -a, m)).equals(BigInteger.valueOf(-a).mod(BigInteger.valueOf(m))), "mod is non-negative");
        Stitch.check(IntMath.isPowerOfTwo(a) == (a > 0 && Long.bitCount(a) == 1), "isPowerOfTwo");
        Stitch.check(IntMath.isPrime(a) == BigInteger.valueOf(a).isProbablePrime(50), "isPrime");
        int lo = (int) Math.min(a, b), hi = (int) Math.max(a, b);
        Stitch.check(IntMath.mean(lo, hi) == (BigInteger.valueOf(lo).add(BigInteger.valueOf(hi))).shiftRight(1).longValue(), "mean rounds toward negative infinity");
        int n = ri(s, 0, 30), k = ri(s, 0, 30);
        if (k <= n) Stitch.check(BigInteger.valueOf(IntMath.binomial(n, k)).equals(BigIntegerMath.binomial(n, k).min(BigInteger.valueOf(Integer.MAX_VALUE))) || BigIntegerMath.binomial(n, k).compareTo(BigInteger.valueOf(Integer.MAX_VALUE)) > 0, "binomial");
        IntMath.factorial(n);
    }

    @Endpoint
    public static void longmath_checked_add(@stitch.Max(32) Source s) {
        long a = lv(s), b = lv(s);
        BigInteger exact = BigInteger.valueOf(a).add(BigInteger.valueOf(b));
        try {
            long r = LongMath.checkedAdd(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedAdd is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0), "checkedAdd throws only on overflow");
        }
        long sat = LongMath.saturatedAdd(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Long.MIN_VALUE)).min(BigInteger.valueOf(Long.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedAdd clamps");
    }

    @Endpoint
    public static void longmath_checked_subtract(@stitch.Max(32) Source s) {
        long a = lv(s), b = lv(s);
        BigInteger exact = BigInteger.valueOf(a).subtract(BigInteger.valueOf(b));
        try {
            long r = LongMath.checkedSubtract(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedSubtract is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0), "checkedSubtract throws only on overflow");
        }
        long sat = LongMath.saturatedSubtract(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Long.MIN_VALUE)).min(BigInteger.valueOf(Long.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedSubtract clamps");
    }

    @Endpoint
    public static void longmath_checked_multiply(@stitch.Max(32) Source s) {
        long a = lv(s), b = lv(s);
        BigInteger exact = BigInteger.valueOf(a).multiply(BigInteger.valueOf(b));
        try {
            long r = LongMath.checkedMultiply(a, b);
            Stitch.check((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0 && BigInteger.valueOf(r).equals(exact), "checkedMultiply is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0), "checkedMultiply throws only on overflow");
        }
        long sat = LongMath.saturatedMultiply(a, b);
        BigInteger clamp = exact.max(BigInteger.valueOf(Long.MIN_VALUE)).min(BigInteger.valueOf(Long.MAX_VALUE));
        Stitch.check(BigInteger.valueOf(sat).equals(clamp), "saturatedMultiply clamps");
    }

    @Endpoint
    public static void longmath_pow(@stitch.Max(16) Source s) {
        long b = (long) s.range(-10, 10);
        int k = ri(s, 0, 70);
        BigInteger exact = BigInteger.valueOf(b).pow(k);
        long p = LongMath.pow(b, k);
        Stitch.check(BigInteger.valueOf(p).equals(exact.mod(BigInteger.ONE.shiftLeft(8*8)).subtract(exact.mod(BigInteger.ONE.shiftLeft(64)).testBit(63) ? BigInteger.ONE.shiftLeft(64) : BigInteger.ZERO)), "pow wraps like repeated multiplication");
        try {
            long c = LongMath.checkedPow(b, k);
            Stitch.check(BigInteger.valueOf(c).equals(exact), "checkedPow is exact");
        } catch (ArithmeticException e) {
            Stitch.check(!((exact).compareTo(BigInteger.valueOf(Long.MIN_VALUE)) >= 0 && (exact).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) <= 0), "checkedPow throws only on overflow");
        }
        Stitch.check(BigInteger.valueOf(LongMath.saturatedPow(b, k)).equals(exact.max(BigInteger.valueOf(Long.MIN_VALUE)).min(BigInteger.valueOf(Long.MAX_VALUE))), "saturatedPow clamps");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void longmath_sqrt_log(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        long x = lv(s);
        RoundingMode rm = MODES[mode];
        try {
            long r = LongMath.sqrt(x, rm);
            BigInteger exactFloor = BigInteger.valueOf(x).sqrt();
            boolean perfect = exactFloor.multiply(exactFloor).equals(BigInteger.valueOf(x));
            Stitch.check(BigInteger.valueOf(r).subtract(exactFloor).abs().compareTo(BigInteger.ONE) <= 0, "sqrt is the floor or the ceiling");
            if (perfect) Stitch.check(BigInteger.valueOf(r).equals(exactFloor), "sqrt of a perfect square is exact");
        } catch (ArithmeticException e) {
            Stitch.check(rm == RoundingMode.UNNECESSARY, "sqrt throws ArithmeticException only for UNNECESSARY");
        }
        try {
            int l2 = LongMath.log2(x, rm);
            int bl = BigInteger.valueOf(x).bitLength();
            Stitch.check(l2 == bl - 1 || l2 == bl, "log2 is the floor or the ceiling");
            int l10 = LongMath.log10(x, rm);
            Stitch.check(l10 >= 0 && l10 <= 19, "log10 in range");
        } catch (ArithmeticException e) {
            Stitch.check(rm == RoundingMode.UNNECESSARY, "log throws ArithmeticException only for UNNECESSARY");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, ArithmeticException.class})
    public static void longmath_divide(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        long a = lv(s), b = lv(s);
        RoundingMode rm = MODES[mode];
        Stitch.require(!(a == Long.MIN_VALUE && b == -1), "MIN_VALUE / -1 overflows (wraps like Java's /)");
        long q = LongMath.divide(a, b, rm);
        java.math.BigDecimal exact = new java.math.BigDecimal(a).divide(new java.math.BigDecimal(b), 0, rm);
        Stitch.check(exact.toBigIntegerExact().equals(BigInteger.valueOf(q)), "divide rounds like BigDecimal (" + a + " / " + b + " " + rm + ")");
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, ArithmeticException.class})
    public static void longmath_number_theory(@stitch.Max(16) Source s) {
        long a = (long) Math.abs((long) lv(s) % 1000000), b = (long) Math.abs((long) lv(s) % 1000000);
        Stitch.check(BigInteger.valueOf(LongMath.gcd(a, b)).equals(BigInteger.valueOf(a).gcd(BigInteger.valueOf(b))), "gcd");
        long m = (long) (b == 0 ? 1 : b);
        Stitch.check(BigInteger.valueOf(LongMath.mod((long) -a, m)).equals(BigInteger.valueOf(-a).mod(BigInteger.valueOf(m))), "mod is non-negative");
        Stitch.check(LongMath.isPowerOfTwo(a) == (a > 0 && Long.bitCount(a) == 1), "isPowerOfTwo");
        Stitch.check(LongMath.isPrime(a) == BigInteger.valueOf(a).isProbablePrime(50), "isPrime");
        long lo = (long) Math.min(a, b), hi = (long) Math.max(a, b);
        Stitch.check(LongMath.mean(lo, hi) == (BigInteger.valueOf(lo).add(BigInteger.valueOf(hi))).shiftRight(1).longValue(), "mean rounds toward negative infinity");
        int n = ri(s, 0, 30), k = ri(s, 0, 30);
        if (k <= n) Stitch.check(BigInteger.valueOf(LongMath.binomial(n, k)).equals(BigIntegerMath.binomial(n, k).min(BigInteger.valueOf(Long.MAX_VALUE))) || BigIntegerMath.binomial(n, k).compareTo(BigInteger.valueOf(Long.MAX_VALUE)) > 0, "binomial");
        LongMath.factorial(n);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void long_math_extras(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        long a = lv(s);
        Stitch.check(LongMath.floorPowerOfTwo(Math.max(a, 1)) <= Math.max(a, 1), "floorPowerOfTwo");
        if (a > 0 && a <= (1L << 62)) Stitch.check(LongMath.ceilingPowerOfTwo(a) >= a, "ceilingPowerOfTwo");
        try {
            double d = LongMath.roundToDouble(a, MODES[mode]);
            Stitch.check(Math.abs(d - (double) a) <= Math.ulp(d), "roundToDouble is within an ulp");
        } catch (ArithmeticException e) {
            Stitch.check(MODES[mode] == RoundingMode.UNNECESSARY, "roundToDouble throws only for UNNECESSARY");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void big_integer_math(@stitch.Max(32) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        BigInteger x = new BigInteger(ri(s, 1, 200), new java.util.Random(ri(s, 0, 1000)));
        RoundingMode rm = MODES[mode];
        try {
            BigInteger r = BigIntegerMath.sqrt(x, rm);
            BigInteger f = x.sqrt();
            Stitch.check(r.subtract(f).abs().compareTo(BigInteger.ONE) <= 0, "BigIntegerMath.sqrt is the floor or the ceiling");
            int l = BigIntegerMath.log2(x, rm);
            Stitch.check(l == x.bitLength() - 1 || l == x.bitLength(), "log2");
            int l10 = BigIntegerMath.log10(x, rm);
            Stitch.check(Math.abs(l10 - (x.toString().length() - 1)) <= 1, "log10");
            BigIntegerMath.roundToDouble(x, rm);
        } catch (ArithmeticException e) {
            Stitch.check(rm == RoundingMode.UNNECESSARY, "throws only for UNNECESSARY");
        }
        int n = ri(s, 0, 60);
        Stitch.check(BigIntegerMath.factorial(n).equals(java.util.stream.IntStream.rangeClosed(1, n).mapToObj(BigInteger::valueOf).reduce(BigInteger.ONE, BigInteger::multiply)), "factorial");
        BigIntegerMath.divide(x, BigInteger.valueOf(ri(s, 1, 100)), rm == RoundingMode.UNNECESSARY ? RoundingMode.FLOOR : rm);
        BigIntegerMath.isPowerOfTwo(x);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void double_math(@stitch.Max(16) Source s, @stitch.Range(lo = 0, hi = 7) int mode) {
        double x = s.pick(0.0, -0.0, 0.5, 1.5, 2.5, -2.5, 1e18, 9.223372036854776E18, -9.223372036854776E18, 1e300, Double.NaN, Double.MIN_VALUE, 4503599627370497.0, ri(s, -1000, 1000) / 7.0);
        RoundingMode rm = MODES[mode];
        try {
            long l = DoubleMath.roundToLong(x, rm);
            java.math.BigDecimal exact = new java.math.BigDecimal(x).setScale(0, rm);
            Stitch.check(exact.longValueExact() == l, "roundToLong agrees with BigDecimal (" + x + " " + rm + ")");
        } catch (ArithmeticException e) {
            boolean big = Double.isNaN(x) || Double.isInfinite(x) || Math.abs(x) >= 9.223372036854775807E18 || rm == RoundingMode.UNNECESSARY;
            Stitch.check(big, "roundToLong throws only out of range or for UNNECESSARY (" + x + " " + rm + ")");
        }
        try {
            BigInteger b = DoubleMath.roundToBigInteger(x, rm);
            Stitch.check(new java.math.BigDecimal(x).setScale(0, rm).toBigIntegerExact().equals(b), "roundToBigInteger");
        } catch (ArithmeticException e) {
            Stitch.check(Double.isNaN(x) || Double.isInfinite(x) || rm == RoundingMode.UNNECESSARY, "roundToBigInteger throws only for non-finite or UNNECESSARY");
        }
        Stitch.check(DoubleMath.isMathematicalInteger(x) == (!Double.isNaN(x) && !Double.isInfinite(x) && x == Math.rint(x)), "isMathematicalInteger");
        if (x > 0 && !Double.isInfinite(x)) {
            try {
                int lg = DoubleMath.log2(x, rm);
                Stitch.check(Math.abs(lg - Math.getExponent(x)) <= 1 || x < Double.MIN_NORMAL, "log2");
            } catch (ArithmeticException e) {
                Stitch.check(rm == RoundingMode.UNNECESSARY, "log2 throws only for UNNECESSARY");
            }
        }
        Stitch.check(DoubleMath.fuzzyEquals(x, x, 0) == !Double.isNaN(x) || DoubleMath.fuzzyEquals(x, x, 0), "fuzzyEquals(x, x)");
        DoubleMath.fuzzyCompare(x, 1.0, 1e-9);
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, IllegalStateException.class})
    public static void stats(@stitch.Max(64) Source s) {
        double[] v = new double[ri(s, 0, 10)];
        for (int i = 0; i < v.length; i++) v[i] = s.coin(0.1) ? s.pick(Double.NaN, Double.POSITIVE_INFINITY, 1e308) : ri(s, -100, 100) / 4.0;
        Stats st = Stats.of(v);
        StatsAccumulator acc = new StatsAccumulator();
        for (double x : v) acc.add(x);
        Stitch.check(acc.snapshot().equals(st) || java.util.Arrays.stream(v).anyMatch(Double::isNaN), "accumulator == Stats.of");
        if (v.length > 0) {
            double naive = java.util.Arrays.stream(v).sum() / v.length;
            Stitch.check(Double.isNaN(naive) || Double.isInfinite(naive) || Math.abs(st.mean() - naive) <= 1e-9 * Math.max(1, Math.abs(naive)), "mean");
            Stitch.check(st.max() == java.util.Arrays.stream(v).max().getAsDouble() || Double.isNaN(st.max()), "max");
            Stitch.check(Double.isNaN(st.populationVariance()) || st.populationVariance() >= 0, "variance >= 0");
            Stats back = Stats.fromByteArray(st.toByteArray());
            Stitch.check(back.equals(st), "Stats byte round trip");
            StatsAccumulator a2 = new StatsAccumulator();
            a2.addAll(java.util.Arrays.copyOfRange(v, 0, v.length / 2));
            a2.addAll(Stats.of(java.util.Arrays.copyOfRange(v, v.length / 2, v.length)));
            // (only for moderate values: with values near Double.MAX_VALUE the running means legitimately differ)
            if (java.util.Arrays.stream(v).allMatch(x -> Math.abs(x) < 1e100))
                Stitch.check(Math.abs(a2.mean() - st.mean()) <= 1e-9 * Math.max(1, Math.abs(st.mean())), "merging accumulators");
        }
        if (v.length > 0 && java.util.Arrays.stream(v).allMatch(Double::isFinite)) {
            double[] sorted = v.clone();
            java.util.Arrays.sort(sorted);
            double med = Quantiles.median().compute(v);
            Stitch.check(med >= sorted[0] && med <= sorted[sorted.length - 1], "median within the data");
            java.util.Map<Integer, Double> qs = Quantiles.scale(100).indexes(0, 25, 50, 75, 100).compute(v);
            Stitch.log("data " + java.util.Arrays.toString(v) + " median " + med + " quantiles " + qs);
            Stitch.check(qs.get(0) == sorted[0] && qs.get(100) == sorted[sorted.length - 1], "0th and 100th percentiles are min and max");
            Stitch.check(qs.get(25) <= qs.get(50) && qs.get(50) <= qs.get(75), "percentiles are monotone");
            Stitch.check(qs.get(50).equals(med), "median == 50th percentile");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, IllegalStateException.class})
    public static void paired_stats(@stitch.Max(64) Source s) {
        PairedStatsAccumulator p = new PairedStatsAccumulator();
        int n = ri(s, 0, 10);
        for (int i = 0; i < n; i++) p.add(ri(s, -20, 20), ri(s, -20, 20));
        PairedStats ps = p.snapshot();
        Stitch.check(PairedStats.fromByteArray(ps.toByteArray()).equals(ps), "PairedStats byte round trip");
        double r = ps.pearsonsCorrelationCoefficient();
        Stitch.check(Double.isNaN(r) || Math.abs(r) <= 1 + 1e-12, "|pearson| <= 1");
        ps.leastSquaresFit();
    }
}
