use chrono::prelude::*;
use chrono::{Duration, FixedOffset, NaiveDate, NaiveDateTime};
use stitch::*;
use super::stubs::{D, N, Z, U, DateText};

const MONTHS: [&str; 12] = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
const DAYS: [&str; 7] = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
fn off_str(secs: i32, colon: bool) -> String {
    let sign = if secs < 0 { '-' } else { '+' };
    let a = secs.abs();
    if colon { format!("{}{:02}:{:02}", sign, a / 3600, a / 60 % 60) } else { format!("{}{:02}{:02}", sign, a / 3600, a / 60 % 60) }
}

endpoint! {
    fn spec_Y(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%Y").to_string();
        let want: String = if (0..=9999).contains(&x.year()) { format!("{:04}", x.year()) } else { format!("{:+}", x.year()) };
        log(&format!("%Y: {} want {} ({})", got, want, *x));
        check(got == want, "%Y formats as expected");
    }
}

endpoint! {
    fn spec_C(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%C").to_string();
        let want: String = format!("{:02}", x.year().div_euclid(100));
        log(&format!("%C: {} want {} ({})", got, want, *x));
        check(got == want, "%C formats as expected");
    }
}

endpoint! {
    fn spec_y(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%y").to_string();
        let want: String = format!("{:02}", x.year().rem_euclid(100));
        log(&format!("%y: {} want {} ({})", got, want, *x));
        check(got == want, "%y formats as expected");
    }
}

endpoint! {
    fn spec_m(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%m").to_string();
        let want: String = format!("{:02}", x.month());
        log(&format!("%m: {} want {} ({})", got, want, *x));
        check(got == want, "%m formats as expected");
    }
}

endpoint! {
    fn spec_b(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%b").to_string();
        let want: String = MONTHS[x.month0() as usize][..3].to_string();
        log(&format!("%b: {} want {} ({})", got, want, *x));
        check(got == want, "%b formats as expected");
    }
}

endpoint! {
    fn spec_B(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%B").to_string();
        let want: String = MONTHS[x.month0() as usize].to_string();
        log(&format!("%B: {} want {} ({})", got, want, *x));
        check(got == want, "%B formats as expected");
    }
}

endpoint! {
    fn spec_h(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%h").to_string();
        let want: String = MONTHS[x.month0() as usize][..3].to_string();
        log(&format!("%h: {} want {} ({})", got, want, *x));
        check(got == want, "%h formats as expected");
    }
}

endpoint! {
    fn spec_d(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%d").to_string();
        let want: String = format!("{:02}", x.day());
        log(&format!("%d: {} want {} ({})", got, want, *x));
        check(got == want, "%d formats as expected");
    }
}

endpoint! {
    fn spec_e(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%e").to_string();
        let want: String = format!("{:>2}", x.day());
        log(&format!("%e: {} want {} ({})", got, want, *x));
        check(got == want, "%e formats as expected");
    }
}

endpoint! {
    fn spec_a(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%a").to_string();
        let want: String = DAYS[x.weekday().num_days_from_monday() as usize][..3].to_string();
        log(&format!("%a: {} want {} ({})", got, want, *x));
        check(got == want, "%a formats as expected");
    }
}

endpoint! {
    fn spec_A(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%A").to_string();
        let want: String = DAYS[x.weekday().num_days_from_monday() as usize].to_string();
        log(&format!("%A: {} want {} ({})", got, want, *x));
        check(got == want, "%A formats as expected");
    }
}

endpoint! {
    fn spec_w(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%w").to_string();
        let want: String = x.weekday().num_days_from_sunday().to_string();
        log(&format!("%w: {} want {} ({})", got, want, *x));
        check(got == want, "%w formats as expected");
    }
}

endpoint! {
    fn spec_u(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%u").to_string();
        let want: String = x.weekday().number_from_monday().to_string();
        log(&format!("%u: {} want {} ({})", got, want, *x));
        check(got == want, "%u formats as expected");
    }
}

endpoint! {
    fn spec_G(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%G").to_string();
        let want: String = if (0..=9999).contains(&x.iso_week().year()) { format!("{:04}", x.iso_week().year()) } else { format!("{:+}", x.iso_week().year()) };
        log(&format!("%G: {} want {} ({})", got, want, *x));
        check(got == want, "%G formats as expected");
    }
}

endpoint! {
    fn spec_g(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%g").to_string();
        let want: String = format!("{:02}", x.iso_week().year().rem_euclid(100));
        log(&format!("%g: {} want {} ({})", got, want, *x));
        check(got == want, "%g formats as expected");
    }
}

endpoint! {
    fn spec_V(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%V").to_string();
        let want: String = format!("{:02}", x.iso_week().week());
        log(&format!("%V: {} want {} ({})", got, want, *x));
        check(got == want, "%V formats as expected");
    }
}

endpoint! {
    fn spec_j(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%j").to_string();
        let want: String = format!("{:03}", x.ordinal());
        log(&format!("%j: {} want {} ({})", got, want, *x));
        check(got == want, "%j formats as expected");
    }
}

endpoint! {
    fn spec_H(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%H").to_string();
        let want: String = format!("{:02}", x.hour());
        log(&format!("%H: {} want {} ({})", got, want, *x));
        check(got == want, "%H formats as expected");
    }
}

endpoint! {
    fn spec_k(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%k").to_string();
        let want: String = format!("{:>2}", x.hour());
        log(&format!("%k: {} want {} ({})", got, want, *x));
        check(got == want, "%k formats as expected");
    }
}

endpoint! {
    fn spec_I(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%I").to_string();
        let want: String = format!("{:02}", x.hour12().1);
        log(&format!("%I: {} want {} ({})", got, want, *x));
        check(got == want, "%I formats as expected");
    }
}

endpoint! {
    fn spec_l(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%l").to_string();
        let want: String = format!("{:>2}", x.hour12().1);
        log(&format!("%l: {} want {} ({})", got, want, *x));
        check(got == want, "%l formats as expected");
    }
}

endpoint! {
    fn spec_P(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%P").to_string();
        let want: String = if x.hour() < 12 { "am" } else { "pm" }.to_string();
        log(&format!("%P: {} want {} ({})", got, want, *x));
        check(got == want, "%P formats as expected");
    }
}

endpoint! {
    fn spec_p(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%p").to_string();
        let want: String = if x.hour() < 12 { "AM" } else { "PM" }.to_string();
        log(&format!("%p: {} want {} ({})", got, want, *x));
        check(got == want, "%p formats as expected");
    }
}

endpoint! {
    fn spec_M(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%M").to_string();
        let want: String = format!("{:02}", x.minute());
        log(&format!("%M: {} want {} ({})", got, want, *x));
        check(got == want, "%M formats as expected");
    }
}

endpoint! {
    fn spec_S(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%S").to_string();
        let want: String = format!("{:02}", x.second());
        log(&format!("%S: {} want {} ({})", got, want, *x));
        check(got == want, "%S formats as expected");
    }
}

endpoint! {
    fn spec_f(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%f").to_string();
        let want: String = format!("{:09}", x.nanosecond() % 1_000_000_000);
        log(&format!("%f: {} want {} ({})", got, want, *x));
        check(got == want, "%f formats as expected");
    }
}

endpoint! {
    fn spec_3f(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%3f").to_string();
        let want: String = format!("{:03}", x.nanosecond() % 1_000_000_000 / 1_000_000);
        log(&format!("%3f: {} want {} ({})", got, want, *x));
        check(got == want, "%3f formats as expected");
    }
}

endpoint! {
    fn spec_6f(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%6f").to_string();
        let want: String = format!("{:06}", x.nanosecond() % 1_000_000_000 / 1_000);
        log(&format!("%6f: {} want {} ({})", got, want, *x));
        check(got == want, "%6f formats as expected");
    }
}

endpoint! {
    fn spec_9f(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%9f").to_string();
        let want: String = format!("{:09}", x.nanosecond() % 1_000_000_000);
        log(&format!("%9f: {} want {} ({})", got, want, *x));
        check(got == want, "%9f formats as expected");
    }
}

endpoint! {
    fn spec_R(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%R").to_string();
        let want: String = format!("{:02}:{:02}", x.hour(), x.minute());
        log(&format!("%R: {} want {} ({})", got, want, *x));
        check(got == want, "%R formats as expected");
    }
}

endpoint! {
    fn spec_T(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%T").to_string();
        let want: String = format!("{:02}:{:02}:{:02}", x.hour(), x.minute(), x.second());
        log(&format!("%T: {} want {} ({})", got, want, *x));
        check(got == want, "%T formats as expected");
    }
}

endpoint! {
    fn spec_s(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%s").to_string();
        let want: String = x.timestamp().to_string();
        log(&format!("%s: {} want {} ({})", got, want, *x));
        check(got == want, "%s formats as expected");
    }
}

endpoint! {
    fn spec_z(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%z").to_string();
        let want: String = off_str(x.offset().local_minus_utc(), false);
        log(&format!("%z: {} want {} ({})", got, want, *x));
        check(got == want, "%z formats as expected");
    }
}

endpoint! {
    fn spec_colon_z(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%:z").to_string();
        let want: String = off_str(x.offset().local_minus_utc(), true);
        log(&format!("%:z: {} want {} ({})", got, want, *x));
        check(got == want, "%:z formats as expected");
    }
}

endpoint! {
    fn spec_percent(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%%").to_string();
        let want: String = "%".to_string();
        log(&format!("%%: {} want {} ({})", got, want, *x));
        check(got == want, "%% formats as expected");
    }
}

endpoint! {
    fn spec_t(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%t").to_string();
        let want: String = "\t".to_string();
        log(&format!("%t: {} want {} ({})", got, want, *x));
        check(got == want, "%t formats as expected");
    }
}

endpoint! {
    fn spec_n(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%n").to_string();
        let want: String = "\n".to_string();
        log(&format!("%n: {} want {} ({})", got, want, *x));
        check(got == want, "%n formats as expected");
    }
}

endpoint! {
    fn spec_F(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%F").to_string();
        let want: String = format!("{}-{:02}-{:02}", if (0..=9999).contains(&x.year()) { format!("{:04}", x.year()) } else { format!("{:+}", x.year()) }, x.month(), x.day());
        log(&format!("%F: {} want {} ({})", got, want, *x));
        check(got == want, "%F formats as expected");
    }
}

endpoint! {
    fn spec_D(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%D").to_string();
        let want: String = format!("{:02}/{:02}/{:02}", x.month(), x.day(), x.year().rem_euclid(100));
        log(&format!("%D: {} want {} ({})", got, want, *x));
        check(got == want, "%D formats as expected");
    }
}

endpoint! {
    fn spec_x(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%x").to_string();
        let want: String = format!("{:02}/{:02}/{:02}", x.month(), x.day(), x.year().rem_euclid(100));
        log(&format!("%x: {} want {} ({})", got, want, *x));
        check(got == want, "%x formats as expected");
    }
}

endpoint! {
    fn spec_X(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%X").to_string();
        let want: String = format!("{:02}:{:02}:{:02}", x.hour(), x.minute(), x.second());
        log(&format!("%X: {} want {} ({})", got, want, *x));
        check(got == want, "%X formats as expected");
    }
}

endpoint! {
    fn spec_r(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%r").to_string();
        let want: String = format!("{:02}:{:02}:{:02} {}", x.hour12().1, x.minute(), x.second(), if x.hour() < 12 { "AM" } else { "PM" });
        log(&format!("%r: {} want {} ({})", got, want, *x));
        check(got == want, "%r formats as expected");
    }
}

endpoint! {
    fn spec_dashunder_d(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%-d").to_string();
        let want: String = x.day().to_string();
        log(&format!("%-d: {} want {} ({})", got, want, *x));
        check(got == want, "%-d formats as expected");
    }
}

endpoint! {
    fn spec_dash_m(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%-m").to_string();
        let want: String = x.month().to_string();
        log(&format!("%-m: {} want {} ({})", got, want, *x));
        check(got == want, "%-m formats as expected");
    }
}

endpoint! {
    fn spec_dash_H(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%-H").to_string();
        let want: String = x.hour().to_string();
        log(&format!("%-H: {} want {} ({})", got, want, *x));
        check(got == want, "%-H formats as expected");
    }
}

endpoint! {
    fn spec_under_d(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%_d").to_string();
        let want: String = format!("{:>2}", x.day());
        log(&format!("%_d: {} want {} ({})", got, want, *x));
        check(got == want, "%_d formats as expected");
    }
}

endpoint! {
    fn spec_zero_e(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%0e").to_string();
        let want: String = format!("{:02}", x.day());
        log(&format!("%0e: {} want {} ({})", got, want, *x));
        check(got == want, "%0e formats as expected");
    }
}

endpoint! {
    fn spec_U(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%U").to_string();
        let want: String = format!("{:02}", (x.ordinal() + 6 - x.weekday().num_days_from_sunday()) / 7);
        log(&format!("%U: {} want {} ({})", got, want, *x));
        check(got == want, "%U formats as expected");
    }
}

endpoint! {
    fn spec_W(x: Borrow<Z>) {
        require(x.nanosecond() < 1_000_000_000, "leap second");
        let got = x.format("%W").to_string();
        let want: String = format!("{:02}", (x.ordinal() + 6 - x.weekday().num_days_from_monday()) / 7);
        log(&format!("%W: {} want {} ({})", got, want, *x));
        check(got == want, "%W formats as expected");
    }
}

endpoint! {
    fn parse_ndt_spec_0(text: Str<48, DateText>) {
        // %Y-%m-%d
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%d") {
            let s = v.format("%Y-%m-%d").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%d");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_1(text: Str<48, DateText>) {
        // %Y-%m-%d %H:%M:%S
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%d %H:%M:%S") {
            let s = v.format("%Y-%m-%d %H:%M:%S").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%d %H:%M:%S");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_2(text: Str<48, DateText>) {
        // %Y-%m-%dT%H:%M:%S%.f
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%dT%H:%M:%S%.f") {
            let s = v.format("%Y-%m-%dT%H:%M:%S%.f").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%.f");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_3(text: Str<48, DateText>) {
        // %Y-%m-%dT%H:%M:%S%.3f
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%dT%H:%M:%S%.3f") {
            let s = v.format("%Y-%m-%dT%H:%M:%S%.3f").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%.3f");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_4(text: Str<48, DateText>) {
        // %Y-%m-%dT%H:%M:%S%.9f
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%dT%H:%M:%S%.9f") {
            let s = v.format("%Y-%m-%dT%H:%M:%S%.9f").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%.9f");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_5(text: Str<48, DateText>) {
        // %G-W%V-%u
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%G-W%V-%u") {
            let s = v.format("%G-W%V-%u").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%G-W%V-%u");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_6(text: Str<48, DateText>) {
        // %Y-%j
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%j") {
            let s = v.format("%Y-%j").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%j");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_7(text: Str<48, DateText>) {
        // %d/%m/%Y %I:%M:%S %p
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%d/%m/%Y %I:%M:%S %p") {
            let s = v.format("%d/%m/%Y %I:%M:%S %p").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%d/%m/%Y %I:%M:%S %p");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_8(text: Str<48, DateText>) {
        // %A, %d %B %Y
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%A, %d %B %Y") {
            let s = v.format("%A, %d %B %Y").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%A, %d %B %Y");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_9(text: Str<48, DateText>) {
        // %a %b %e %T %Y
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%a %b %e %T %Y") {
            let s = v.format("%a %b %e %T %Y").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%a %b %e %T %Y");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_10(text: Str<48, DateText>) {
        // %F %T
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%F %T") {
            let s = v.format("%F %T").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%F %T");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_11(text: Str<48, DateText>) {
        // %D %r
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%D %r") {
            let s = v.format("%D %r").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%D %r");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_12(text: Str<48, DateText>) {
        // %Y%m%d%H%M%S
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y%m%d%H%M%S") {
            let s = v.format("%Y%m%d%H%M%S").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y%m%d%H%M%S");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_13(text: Str<48, DateText>) {
        // %C%y-%m-%d
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%C%y-%m-%d") {
            let s = v.format("%C%y-%m-%d").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%C%y-%m-%d");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_14(text: Str<48, DateText>) {
        // %s
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%s") {
            let s = v.format("%s").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%s");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_15(text: Str<48, DateText>) {
        // %Y-%m-%d %H:%M:%S.%f
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%m-%d %H:%M:%S.%f") {
            let s = v.format("%Y-%m-%d %H:%M:%S.%f").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%m-%d %H:%M:%S.%f");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_16(text: Str<48, DateText>) {
        // %+
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%+") {
            let s = v.format("%+").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%+");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_17(text: Str<48, DateText>) {
        // %c
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%c") {
            let s = v.format("%c").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%c");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_18(text: Str<48, DateText>) {
        // %x %X
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%x %X") {
            let s = v.format("%x %X").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%x %X");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_19(text: Str<48, DateText>) {
        // %h %d %Y %R
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%h %d %Y %R") {
            let s = v.format("%h %d %Y %R").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%h %d %Y %R");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_20(text: Str<48, DateText>) {
        // %e-%b-%Y
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%e-%b-%Y") {
            let s = v.format("%e-%b-%Y").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%e-%b-%Y");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_21(text: Str<48, DateText>) {
        // %-d/%-m/%Y %-H:%M
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%-d/%-m/%Y %-H:%M") {
            let s = v.format("%-d/%-m/%Y %-H:%M").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%-d/%-m/%Y %-H:%M");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_22(text: Str<48, DateText>) {
        // %_m/%_d/%Y
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%_m/%_d/%Y") {
            let s = v.format("%_m/%_d/%Y").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%_m/%_d/%Y");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_23(text: Str<48, DateText>) {
        // %Y-%U-%w
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%U-%w") {
            let s = v.format("%Y-%U-%w").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%U-%w");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_24(text: Str<48, DateText>) {
        // %Y-%W-%u
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y-%W-%u") {
            let s = v.format("%Y-%W-%u").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y-%W-%u");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_25(text: Str<48, DateText>) {
        // %Y.%m.%d %H:%M:%S%.6f
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%Y.%m.%d %H:%M:%S%.6f") {
            let s = v.format("%Y.%m.%d %H:%M:%S%.6f").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%Y.%m.%d %H:%M:%S%.6f");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_ndt_spec_26(text: Str<48, DateText>) {
        // %v %T
        if let Ok(v) = NaiveDateTime::parse_from_str(&text, "%v %T") {
            let s = v.format("%v %T").to_string();
            let again = NaiveDateTime::parse_from_str(&s, "%v %T");
            check(again.is_ok(), &format!("re-formatted value parses: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_0(text: Str<32, DateText>) {
        // %Y-%m-%d
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%Y-%m-%d") {
            let s = v.format("%Y-%m-%d").to_string();
            check(NaiveDate::parse_from_str(&s, "%Y-%m-%d").ok() == Some(v) || "%Y-%m-%d".contains("%y") || "%Y-%m-%d".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_1(text: Str<32, DateText>) {
        // %Y/%j
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%Y/%j") {
            let s = v.format("%Y/%j").to_string();
            check(NaiveDate::parse_from_str(&s, "%Y/%j").ok() == Some(v) || "%Y/%j".contains("%y") || "%Y/%j".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_2(text: Str<32, DateText>) {
        // %G-W%V-%u
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%G-W%V-%u") {
            let s = v.format("%G-W%V-%u").to_string();
            check(NaiveDate::parse_from_str(&s, "%G-W%V-%u").ok() == Some(v) || "%G-W%V-%u".contains("%y") || "%G-W%V-%u".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_3(text: Str<32, DateText>) {
        // %d %B %Y
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%d %B %Y") {
            let s = v.format("%d %B %Y").to_string();
            check(NaiveDate::parse_from_str(&s, "%d %B %Y").ok() == Some(v) || "%d %B %Y".contains("%y") || "%d %B %Y".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_4(text: Str<32, DateText>) {
        // %a, %d %b %Y
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%a, %d %b %Y") {
            let s = v.format("%a, %d %b %Y").to_string();
            check(NaiveDate::parse_from_str(&s, "%a, %d %b %Y").ok() == Some(v) || "%a, %d %b %Y".contains("%y") || "%a, %d %b %Y".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_5(text: Str<32, DateText>) {
        // %Y%m%d
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%Y%m%d") {
            let s = v.format("%Y%m%d").to_string();
            check(NaiveDate::parse_from_str(&s, "%Y%m%d").ok() == Some(v) || "%Y%m%d".contains("%y") || "%Y%m%d".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_6(text: Str<32, DateText>) {
        // %m/%d/%y
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%m/%d/%y") {
            let s = v.format("%m/%d/%y").to_string();
            check(NaiveDate::parse_from_str(&s, "%m/%d/%y").ok() == Some(v) || "%m/%d/%y".contains("%y") || "%m/%d/%y".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_7(text: Str<32, DateText>) {
        // %e %b %Y
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%e %b %Y") {
            let s = v.format("%e %b %Y").to_string();
            check(NaiveDate::parse_from_str(&s, "%e %b %Y").ok() == Some(v) || "%e %b %Y".contains("%y") || "%e %b %Y".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_8(text: Str<32, DateText>) {
        // %Y-W%U-%w
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%Y-W%U-%w") {
            let s = v.format("%Y-W%U-%w").to_string();
            check(NaiveDate::parse_from_str(&s, "%Y-W%U-%w").ok() == Some(v) || "%Y-W%U-%w".contains("%y") || "%Y-W%U-%w".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn parse_date_spec_9(text: Str<32, DateText>) {
        // %C%y%m%d
        if let Ok(v) = NaiveDate::parse_from_str(&text, "%C%y%m%d") {
            let s = v.format("%C%y%m%d").to_string();
            check(NaiveDate::parse_from_str(&s, "%C%y%m%d").ok() == Some(v) || "%C%y%m%d".contains("%y") || "%C%y%m%d".contains("%U"), &format!("format / parse round trip: {}", s));
        }
    }
}

endpoint! {
    fn date_and_hms_opt(x: Borrow<D>, h: Range<0, 25>, m: Range<0, 61>, s: Range<0, 61>, sub: Range<0, 2000000000>) -> Option<New<N>> {
        let (h, m, s, sub) = (*h as u32, *m as u32, *s as u32, *sub as u32);
        let r = x.and_hms_opt(h, m, s);
        if let Some(v) = &r { check(v.date() == *x && v.hour() == h && v.minute() == m && v.second() == s, "and_hms_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_and_hms_milli_opt(x: Borrow<D>, h: Range<0, 25>, m: Range<0, 61>, s: Range<0, 61>, sub: Range<0, 2000000000>) -> Option<New<N>> {
        let (h, m, s, sub) = (*h as u32, *m as u32, *s as u32, *sub as u32);
        let r = x.and_hms_milli_opt(h, m, s, sub / 1_000_000);
        if let Some(v) = &r { check(v.date() == *x && v.hour() == h && v.minute() == m && v.second() == s, "and_hms_milli_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_and_hms_micro_opt(x: Borrow<D>, h: Range<0, 25>, m: Range<0, 61>, s: Range<0, 61>, sub: Range<0, 2000000000>) -> Option<New<N>> {
        let (h, m, s, sub) = (*h as u32, *m as u32, *s as u32, *sub as u32);
        let r = x.and_hms_micro_opt(h, m, s, sub / 1_000);
        if let Some(v) = &r { check(v.date() == *x && v.hour() == h && v.minute() == m && v.second() == s, "and_hms_micro_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn date_and_hms_nano_opt(x: Borrow<D>, h: Range<0, 25>, m: Range<0, 61>, s: Range<0, 61>, sub: Range<0, 2000000000>) -> Option<New<N>> {
        let (h, m, s, sub) = (*h as u32, *m as u32, *s as u32, *sub as u32);
        let r = x.and_hms_nano_opt(h, m, s, sub);
        if let Some(v) = &r { check(v.date() == *x && v.hour() == h && v.minute() == m && v.second() == s, "and_hms_nano_opt keeps the fields"); }
        r.map(New::new)
    }
}

endpoint! {
    fn utc_ymd_hms(y: Range<-300000, 300000>, mo: Range<0, 13>, d: Range<0, 32>, h: Range<0, 25>) -> Option<New<Z>> {
        let r = Utc.ymd_opt(*y as i32, *mo as u32, *d as u32).single().and_then(|dt| dt.and_hms_opt(*h as u32, 0, 0));
        if let Some(v) = &r { check(v.year() == *y as i32 && v.hour() == *h as u32, "Utc.ymd_opt().and_hms_opt() keeps the fields"); }
        r.map(|v| New::new(v.with_timezone(&FixedOffset::east(0))))
    }
}

endpoint! {
    fn utc_yo_isoywd(y: Range<-300000, 300000>, o: Range<0, 367>, w: Range<0, 54>) {
        let _ = Utc.yo_opt(*y as i32, *o as u32);
        let _ = Utc.isoywd_opt(*y as i32, *w as u32, Weekday::Wed);
        let _ = FixedOffset::east(3600).yo_opt(*y as i32, *o as u32).single().map(|d| d.succ_opt());
    }
}

endpoint! {
    fn datetime_from_str(text: Str<48, DateText>, fmt: Str<24>) {
        if let Ok(v) = Utc.datetime_from_str(&text, &fmt) { let _ = v.to_rfc3339(); }
        if let Ok(v) = FixedOffset::east(-3600).datetime_from_str(&text, &fmt) { let _ = v.naive_utc(); }
    }
}

endpoint! {
    fn dt_parts(x: Borrow<Z>) {
        check(x.date().naive_local() == x.naive_local().date(), "date() is the local date");
        check(x.time() == x.naive_local().time(), "time() is the local time");
        check(x.timestamp_millis() == x.timestamp() * 1000 + x.timestamp_subsec_millis() as i64, "timestamp_millis");
        let utc = x.with_timezone(&Utc);
        check(utc.naive_utc() == x.naive_utc(), "naive_utc");
    }
}

endpoint! {
    fn date_tz_ops(x: Borrow<Z>, n: Range<-1000, 1000>) {
        let d = x.date();
        let _ = d.succ_opt();
        let _ = d.pred_opt();
        let _ = d.checked_add_signed(Duration::days(*n));
        let _ = d.and_hms_opt(23, 59, 59);
        let _ = d.to_string();
        let _ = d.signed_duration_since(d);
    }
}

endpoint! {
    fn ndt_parts(x: Borrow<N>, y: Borrow<N>) {
        check(NaiveDateTime::new(x.date(), x.time()) == *x, "new(date, time) == x");
        check((*x < *y) == (x.signed_duration_since(*y) < Duration::zero()), "ordering agrees with signed_duration_since");
        check(x.signed_duration_since(*y) == -y.signed_duration_since(*x) || x.signed_duration_since(*y) == Duration::min_value(), "signed_duration_since is antisymmetric");
    }
}

endpoint! {
    fn duration_boundaries(a: Borrow<U>) {
        check(Duration::zero().is_zero() && !Duration::seconds(1).is_zero(), "is_zero");
        check(*a <= Duration::max_value() && *a >= Duration::min_value(), "within bounds");
        check(a.checked_add(&Duration::zero()) == Some(*a), "d + 0 == d");
        check(a.checked_sub(&*a) == Some(Duration::zero()), "d - d == 0");
    }
}
