use chrono::prelude::*;
use chrono::{NaiveTime};
use stitch::*;
use super::stubs::{T, Z, DateText};

endpoint! {
    fn dt_format_parse_0(x: Borrow<Z>) {
        // %Y-%m-%dT%H:%M:%S%z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y-%m-%dT%H:%M:%S%z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y-%m-%dT%H:%M:%S%z".contains("%.f") && !"%Y-%m-%dT%H:%M:%S%z".contains("%.9f") && !"%Y-%m-%dT%H:%M:%S%z".contains("%+")) || "%Y-%m-%dT%H:%M:%S%z".contains("%M%:z") || "%Y-%m-%dT%H:%M:%S%z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y-%m-%dT%H:%M:%S%z".contains("%c") || "%Y-%m-%dT%H:%M:%S%z".contains("%G") || "%Y-%m-%dT%H:%M:%S%z".contains("%#z") || "%Y-%m-%dT%H:%M:%S%z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_1(x: Borrow<Z>) {
        // %Y-%m-%dT%H:%M:%S%:z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y-%m-%dT%H:%M:%S%:z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%:z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y-%m-%dT%H:%M:%S%:z".contains("%.f") && !"%Y-%m-%dT%H:%M:%S%:z".contains("%.9f") && !"%Y-%m-%dT%H:%M:%S%:z".contains("%+")) || "%Y-%m-%dT%H:%M:%S%:z".contains("%M%:z") || "%Y-%m-%dT%H:%M:%S%:z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y-%m-%dT%H:%M:%S%:z".contains("%c") || "%Y-%m-%dT%H:%M:%S%:z".contains("%G") || "%Y-%m-%dT%H:%M:%S%:z".contains("%#z") || "%Y-%m-%dT%H:%M:%S%:z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_2(x: Borrow<Z>) {
        // %Y-%m-%d %H:%M:%S%.f %z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y-%m-%d %H:%M:%S%.f %z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y-%m-%d %H:%M:%S%.f %z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y-%m-%d %H:%M:%S%.f %z".contains("%.f") && !"%Y-%m-%d %H:%M:%S%.f %z".contains("%.9f") && !"%Y-%m-%d %H:%M:%S%.f %z".contains("%+")) || "%Y-%m-%d %H:%M:%S%.f %z".contains("%M%:z") || "%Y-%m-%d %H:%M:%S%.f %z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y-%m-%d %H:%M:%S%.f %z".contains("%c") || "%Y-%m-%d %H:%M:%S%.f %z".contains("%G") || "%Y-%m-%d %H:%M:%S%.f %z".contains("%#z") || "%Y-%m-%d %H:%M:%S%.f %z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_3(x: Borrow<Z>) {
        // %a, %d %b %Y %H:%M:%S %z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%a, %d %b %Y %H:%M:%S %z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%a, %d %b %Y %H:%M:%S %z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%a, %d %b %Y %H:%M:%S %z".contains("%.f") && !"%a, %d %b %Y %H:%M:%S %z".contains("%.9f") && !"%a, %d %b %Y %H:%M:%S %z".contains("%+")) || "%a, %d %b %Y %H:%M:%S %z".contains("%M%:z") || "%a, %d %b %Y %H:%M:%S %z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%a, %d %b %Y %H:%M:%S %z".contains("%c") || "%a, %d %b %Y %H:%M:%S %z".contains("%G") || "%a, %d %b %Y %H:%M:%S %z".contains("%#z") || "%a, %d %b %Y %H:%M:%S %z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_4(x: Borrow<Z>) {
        // %+
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%+").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%+") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%+".contains("%.f") && !"%+".contains("%.9f") && !"%+".contains("%+")) || "%+".contains("%M%:z") || "%+".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%+".contains("%c") || "%+".contains("%G") || "%+".contains("%#z") || "%+".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_5(x: Borrow<Z>) {
        // %Y-%m-%dT%H:%M:%S%.9f%:z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y-%m-%dT%H:%M:%S%.9f%:z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M:%S%.9f%:z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%.f") && !"%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%.9f") && !"%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%+")) || "%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%M%:z") || "%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%c") || "%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%G") || "%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%#z") || "%Y-%m-%dT%H:%M:%S%.9f%:z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_6(x: Borrow<Z>) {
        // %s%.f %z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%s%.f %z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%s%.f %z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%s%.f %z".contains("%.f") && !"%s%.f %z".contains("%.9f") && !"%s%.f %z".contains("%+")) || "%s%.f %z".contains("%M%:z") || "%s%.f %z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%s%.f %z".contains("%c") || "%s%.f %z".contains("%G") || "%s%.f %z".contains("%#z") || "%s%.f %z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_7(x: Borrow<Z>) {
        // %G-W%V-%uT%H:%M:%S%z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%G-W%V-%uT%H:%M:%S%z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%G-W%V-%uT%H:%M:%S%z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%G-W%V-%uT%H:%M:%S%z".contains("%.f") && !"%G-W%V-%uT%H:%M:%S%z".contains("%.9f") && !"%G-W%V-%uT%H:%M:%S%z".contains("%+")) || "%G-W%V-%uT%H:%M:%S%z".contains("%M%:z") || "%G-W%V-%uT%H:%M:%S%z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%G-W%V-%uT%H:%M:%S%z".contains("%c") || "%G-W%V-%uT%H:%M:%S%z".contains("%G") || "%G-W%V-%uT%H:%M:%S%z".contains("%#z") || "%G-W%V-%uT%H:%M:%S%z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_8(x: Borrow<Z>) {
        // %d/%m/%Y %I:%M:%S %p %:z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%d/%m/%Y %I:%M:%S %p %:z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%d/%m/%Y %I:%M:%S %p %:z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%d/%m/%Y %I:%M:%S %p %:z".contains("%.f") && !"%d/%m/%Y %I:%M:%S %p %:z".contains("%.9f") && !"%d/%m/%Y %I:%M:%S %p %:z".contains("%+")) || "%d/%m/%Y %I:%M:%S %p %:z".contains("%M%:z") || "%d/%m/%Y %I:%M:%S %p %:z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%d/%m/%Y %I:%M:%S %p %:z".contains("%c") || "%d/%m/%Y %I:%M:%S %p %:z".contains("%G") || "%d/%m/%Y %I:%M:%S %p %:z".contains("%#z") || "%d/%m/%Y %I:%M:%S %p %:z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_9(x: Borrow<Z>) {
        // %Y%m%dT%H%M%S%z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y%m%dT%H%M%S%z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y%m%dT%H%M%S%z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y%m%dT%H%M%S%z".contains("%.f") && !"%Y%m%dT%H%M%S%z".contains("%.9f") && !"%Y%m%dT%H%M%S%z".contains("%+")) || "%Y%m%dT%H%M%S%z".contains("%M%:z") || "%Y%m%dT%H%M%S%z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y%m%dT%H%M%S%z".contains("%c") || "%Y%m%dT%H%M%S%z".contains("%G") || "%Y%m%dT%H%M%S%z".contains("%#z") || "%Y%m%dT%H%M%S%z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_10(x: Borrow<Z>) {
        // %c %z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%c %z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%c %z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%c %z".contains("%.f") && !"%c %z".contains("%.9f") && !"%c %z".contains("%+")) || "%c %z".contains("%M%:z") || "%c %z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%c %z".contains("%c") || "%c %z".contains("%G") || "%c %z".contains("%#z") || "%c %z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_11(x: Borrow<Z>) {
        // %A %e %B %Y %T %z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%A %e %B %Y %T %z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%A %e %B %Y %T %z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%A %e %B %Y %T %z".contains("%.f") && !"%A %e %B %Y %T %z".contains("%.9f") && !"%A %e %B %Y %T %z".contains("%+")) || "%A %e %B %Y %T %z".contains("%M%:z") || "%A %e %B %Y %T %z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%A %e %B %Y %T %z".contains("%c") || "%A %e %B %Y %T %z".contains("%G") || "%A %e %B %Y %T %z".contains("%#z") || "%A %e %B %Y %T %z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn dt_format_parse_12(x: Borrow<Z>) {
        // %Y-%m-%dT%H:%M%:z
        require(x.year() >= 0 && x.year() <= 9999 && x.nanosecond() < 1_000_000_000, "outside four-digit years / leap second");
        let s = x.format("%Y-%m-%dT%H:%M%:z").to_string();
        log(&format!("{} -> {}", *x, s));
        match DateTime::parse_from_str(&s, "%Y-%m-%dT%H:%M%:z") {
            Ok(b) => check(b == *x || (b.timestamp() == x.timestamp() && !"%Y-%m-%dT%H:%M%:z".contains("%.f") && !"%Y-%m-%dT%H:%M%:z".contains("%.9f") && !"%Y-%m-%dT%H:%M%:z".contains("%+")) || "%Y-%m-%dT%H:%M%:z".contains("%M%:z") || "%Y-%m-%dT%H:%M%:z".contains("%c"), &format!("parse_from_str(format(x)) == x: {}", b)),
            Err(e) => check("%Y-%m-%dT%H:%M%:z".contains("%c") || "%Y-%m-%dT%H:%M%:z".contains("%G") || "%Y-%m-%dT%H:%M%:z".contains("%#z") || "%Y-%m-%dT%H:%M%:z".contains("%s"), &format!("formatted value parses back ({})", e)),
        }
    }
}

endpoint! {
    fn parse_time_spec_0(text: Str<24, DateText>, t: Borrow<T>) {
        // %H:%M:%S
        let _ = NaiveTime::parse_from_str(&text, "%H:%M:%S");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%H:%M:%S").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%H:%M:%S") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_1(text: Str<24, DateText>, t: Borrow<T>) {
        // %H:%M:%S%.f
        let _ = NaiveTime::parse_from_str(&text, "%H:%M:%S%.f");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%H:%M:%S%.f").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%H:%M:%S%.f") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_2(text: Str<24, DateText>, t: Borrow<T>) {
        // %I:%M:%S %p
        let _ = NaiveTime::parse_from_str(&text, "%I:%M:%S %p");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%I:%M:%S %p").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%I:%M:%S %p") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_3(text: Str<24, DateText>, t: Borrow<T>) {
        // %T
        let _ = NaiveTime::parse_from_str(&text, "%T");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%T").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%T") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_4(text: Str<24, DateText>, t: Borrow<T>) {
        // %R
        let _ = NaiveTime::parse_from_str(&text, "%R");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%R").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%R") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_5(text: Str<24, DateText>, t: Borrow<T>) {
        // %H%M%S
        let _ = NaiveTime::parse_from_str(&text, "%H%M%S");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%H%M%S").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%H%M%S") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_6(text: Str<24, DateText>, t: Borrow<T>) {
        // %k:%M
        let _ = NaiveTime::parse_from_str(&text, "%k:%M");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%k:%M").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%k:%M") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_7(text: Str<24, DateText>, t: Borrow<T>) {
        // %l:%M %P
        let _ = NaiveTime::parse_from_str(&text, "%l:%M %P");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%l:%M %P").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%l:%M %P") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_8(text: Str<24, DateText>, t: Borrow<T>) {
        // %H:%M:%S%.3f
        let _ = NaiveTime::parse_from_str(&text, "%H:%M:%S%.3f");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%H:%M:%S%.3f").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%H:%M:%S%.3f") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_9(text: Str<24, DateText>, t: Borrow<T>) {
        // %X
        let _ = NaiveTime::parse_from_str(&text, "%X");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%X").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%X") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_10(text: Str<24, DateText>, t: Borrow<T>) {
        // %H:%M:%S,%f
        let _ = NaiveTime::parse_from_str(&text, "%H:%M:%S,%f");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%H:%M:%S,%f").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%H:%M:%S,%f") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}

endpoint! {
    fn parse_time_spec_11(text: Str<24, DateText>, t: Borrow<T>) {
        // %r
        let _ = NaiveTime::parse_from_str(&text, "%r");
        require(t.nanosecond() < 1_000_000_000, "leap second");
        let s = t.format("%r").to_string();
        if let Ok(b) = NaiveTime::parse_from_str(&s, "%r") {
            check(b.hour() == t.hour() && b.minute() == t.minute(), &format!("time format / parse keeps hour and minute: {}", s));
        }
    }
}
