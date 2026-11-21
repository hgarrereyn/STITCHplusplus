import com.google.common.net.*;
import java.net.InetAddress;
import stitch.*;

/** Guava net: domain names, host and port, IP addresses, media types, percent escaping. */
public class NetStubs {
    static int ri(Source s, long lo, long hi) { return (int) s.range(lo, hi); }

    @Decoder("Host")
    public static String host(Source s) {
        switch (s.choice(7)) {
            case 0: return s.pick("www.google.com", "a.b.co.uk", "foo.appspot.com", "localhost", "com", "xn--nxasmq6b.com", "a..b", ".com", "under_score.com", "ex-ample.org", "x.blogspot.co.uk");
            case 1: return s.pick("127.0.0.1", "::1", "::ffff:1.2.3.4", "2001:db8::1", "fe80::1%eth0", "1.2.3", "256.1.1.1", "::", "0:0:0:0:0:0:0:1", "2002:c0a8:101::", "2001:0:4136:e378:8000:63bf:3fff:fdd2", "1:2:3:4:5:6:7:8:9");
            case 2: return "[" + s.pick("::1", "2001:db8::1", "1.2.3.4", "") + "]" + s.pick("", ":80", ":0", ":65536", ":-1", ":");
            case 3: return s.pick("a", "bb", "c-d") + "." + s.pick("com", "co.uk", "github.io", "s3.amazonaws.com", "x");
            case 4: return s.token();
            case 5: return s.str(12);
            default: return s.pick("google.com:443", "1.2.3.4:8080", "host:", ":80", "a:b:c");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, IllegalStateException.class})
    public static void internet_domain_name(@Str(max = 64, type = "Host") String text) {
        InternetDomainName d = InternetDomainName.from(text);
        Stitch.check(InternetDomainName.isValid(text), "from() accepts only valid names");
        Stitch.check(InternetDomainName.from(d.toString()).equals(d), "from(toString()) == d");
        if (d.hasPublicSuffix()) {
            InternetDomainName ps = d.publicSuffix();
            Stitch.check(d.toString().endsWith(ps.toString()), "the public suffix is a suffix");
            if (d.isUnderPublicSuffix()) {
                InternetDomainName top = d.topPrivateDomain();
                Stitch.check(d.toString().endsWith(top.toString()) && top.parent().equals(ps), "topPrivateDomain is one label above the public suffix");
            }
        }
        if (d.hasParent()) Stitch.check(d.parent().parts().equals(d.parts().subList(1, d.parts().size())), "parent drops the first label");
        d.isRegistrySuffix();
        d.isTopDomainUnderRegistrySuffix();
        d.isPublicSuffix();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, IllegalStateException.class})
    public static void host_and_port(@Str(max = 64, type = "Host") String text, @stitch.Range(lo = 0, hi = 65535) int port) {
        HostAndPort hp = HostAndPort.fromString(text);
        HostAndPort back = HostAndPort.fromString(hp.toString());
        Stitch.check(back.equals(hp), "fromString(toString()) == hp (" + hp + ")");
        HostAndPort dp = hp.withDefaultPort(port);
        Stitch.check(dp.hasPort() && (hp.hasPort() ? dp.getPort() == hp.getPort() : dp.getPort() == port), "withDefaultPort");
        if (!hp.getHost().contains(":")) Stitch.check(HostAndPort.fromParts(hp.getHost(), port).getPort() == port, "fromParts");
        HostAndPort.fromHost(hp.getHost());
        hp.requireBracketsForIPv6();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void inet_addresses(@Str(max = 48, type = "Host") String text) throws java.net.UnknownHostException {
        boolean valid = InetAddresses.isInetAddress(text);
        InetAddress a;
        try {
            a = InetAddresses.forString(text);
        } catch (IllegalArgumentException e) {
            Stitch.check(!valid, "forString fails only for invalid literals");
            return;
        }
        Stitch.check(valid, "isInetAddress agrees with forString");
        String s = InetAddresses.toAddrString(a);
        Stitch.check(InetAddresses.forString(s).equals(a), "forString(toAddrString(a)) == a (" + s + ")");
        Stitch.check(InetAddresses.forUriString(InetAddresses.toUriString(a)).equals(a), "URI string round trip");
        Stitch.check(InetAddress.getByAddress(a.getAddress()).equals(a), "JDK agrees on the bytes");
        InetAddresses.coerceToInteger(a);
        if (a instanceof java.net.Inet6Address) {
            java.net.Inet6Address v6 = (java.net.Inet6Address) a;
            if (InetAddresses.hasEmbeddedIPv4ClientAddress(v6)) InetAddresses.getEmbeddedIPv4ClientAddress(v6);
            if (InetAddresses.is6to4Address(v6)) InetAddresses.get6to4IPv4Address(v6);
            if (InetAddresses.isTeredoAddress(v6)) InetAddresses.getTeredoInfo(v6);
            if (InetAddresses.isCompatIPv4Address(v6)) InetAddresses.getCompatIPv4Address(v6);
            InetAddresses.isMappedIPv4Address(text);
        }
        try {
            InetAddress n = InetAddresses.increment(a);
            Stitch.check(InetAddresses.decrement(n).equals(a), "decrement(increment(a)) == a");
        } catch (IllegalArgumentException e) {
            Stitch.check(InetAddresses.isMaximum(a), "increment fails only at the maximum");
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void media_type(@Str(max = 96) String text) {
        MediaType m = MediaType.parse(text);
        MediaType back = MediaType.parse(m.toString());
        Stitch.check(back.equals(m), "parse(toString()) == m (" + m + ")");
        Stitch.check(m.withoutParameters().parameters().isEmpty(), "withoutParameters");
        m.charset();
        m.is(MediaType.ANY_TEXT_TYPE);
        Stitch.check(m.is(m.withoutParameters()), "a media type is its own range");
        m.withCharset(java.nio.charset.StandardCharsets.UTF_8).charset();
        m.withParameter("q", "v").toString();
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class})
    public static void percent_escaper(@Str(max = 64) String text, @OneOf(strings = {"", "-_.*", "/:@", "+"}) String safe, boolean plusForSpace) {
        PercentEscaper e = new PercentEscaper(safe, plusForSpace);
        String out = e.escape(text);
        Stitch.check(out.chars().allMatch(c -> c < 128), "escaped text is ASCII");
        if (!plusForSpace || !safe.contains("+")) {
            try {
                String dec = java.net.URLDecoder.decode(plusForSpace ? out : out.replace("+", "%2B"), "UTF-8");
                Stitch.check(dec.equals(text), "percent escaping decodes back");
            } catch (java.io.UnsupportedEncodingException | IllegalArgumentException ignored) {
            }
        }
    }

    @Endpoint(bailsOn = {IllegalArgumentException.class, java.text.ParseException.class})
    public static void host_specifier(@Str(max = 64, type = "Host") String text) throws java.text.ParseException {
        HostSpecifier h = HostSpecifier.from(text);
        Stitch.check(HostSpecifier.isValid(text), "from accepts only valid specifiers");
        Stitch.check(HostSpecifier.from(h.toString()).equals(h), "from(toString()) == h");
    }
}
