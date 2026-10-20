/*
 * timeutil.c - calendar arithmetic and strftime, independent of the C
 * library's time zone handling (which is unreliable on the Amiga).
 *
 * ShTm conventions: year is the full year (2026), mon 0..11, mday 1..31,
 * hour 0..23, min, sec, wday 0..6 (0 = Sunday), yday 0..365.
 */
#include <string.h>
#include "sh.h"
#include "utils.h"

static const char *const wday_name[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const mon_name[12] = {
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

static long floordiv(long a, long b)
{
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* days since 1970-01-01 of a proleptic Gregorian date (m 1..12) */
static long days_from_civil(long y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L;
}

static void civil_from_days(long z, long *py, int *pm, int *pd)
{
    long era, doe, yoe, y, doy, mp;
    z += 719468L;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *pd = (int)(doy - (153 * mp + 2) / 5 + 1);
    *pm = (int)(mp < 10 ? mp + 3 : mp - 9);
    *py = y + (*pm <= 2);
}

static int is_leap(long y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

void sh_gmtime(long t, ShTm *tm)
{
    long days = floordiv(t, 86400L);
    long secs = t - days * 86400L;
    long y;
    int m, d;
    civil_from_days(days, &y, &m, &d);
    tm->year = (int)y;
    tm->mon = m - 1;
    tm->mday = d;
    tm->hour = (int)(secs / 3600);
    tm->min = (int)(secs / 60 % 60);
    tm->sec = (int)(secs % 60);
    tm->wday = (int)((days % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
    tm->yday = (int)(days - days_from_civil(y, 1, 1));
}

void sh_localtime(long t, ShTm *tm)
{
    sh_gmtime(t + os_tz_offset(), tm);
}

/* broken-down UTC -> epoch; fields may be out of range */
long sh_timegm(const ShTm *tm)
{
    long y = tm->year;
    long mon = tm->mon;
    long days;
    y += floordiv(mon, 12);
    mon -= floordiv(mon, 12) * 12;
    days = days_from_civil(y, (int)mon + 1, 1) + tm->mday - 1;
    return days * 86400L + (long)tm->hour * 3600L + (long)tm->min * 60L + tm->sec;
}

long sh_mktime(const ShTm *tm)
{
    return sh_timegm(tm) - os_tz_offset();
}

/* ---- strftime ---- */

typedef struct OBuf { char *buf; size_t n, len; } OBuf;

static void ob_putc(OBuf *o, int c)
{
    if (o->len + 1 < o->n) o->buf[o->len] = (char)c;
    o->len++;
}

static void ob_puts(OBuf *o, const char *s)
{
    while (*s) ob_putc(o, *s++);
}

/* pad: '0' zero, ' ' space, '-' none */
static void ob_num(OBuf *o, long v, int width, int pad)
{
    char tmp[24];
    int i = 0, neg = v < 0;
    unsigned long u = neg ? 0UL - (unsigned long)v : (unsigned long)v;
    do {
        tmp[i++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg) {
        if (pad == '0') {
            ob_putc(o, '-');
            width--;
        } else {
            tmp[i++] = '-';
        }
    }
    if (pad != '-')
        for (; width > i; width--) ob_putc(o, pad);
    while (i > 0) ob_putc(o, tmp[--i]);
}

/* ISO 8601 week number and year */
static int iso_week(const ShTm *tm, long *isoyear)
{
    int wd = (tm->wday + 6) % 7;            /* Monday = 0 */
    int wk = (tm->yday - wd + 10) / 7;
    long y = tm->year;
    if (wk < 1) {
        long py = y - 1;
        int pyday = tm->yday + 365 + is_leap(py);
        wk = (pyday - wd + 10) / 7;
        y = py;
    } else if (wk == 53) {
        int ylen = 365 + is_leap(y);
        if (tm->yday - wd + 3 >= ylen) {    /* Thursday falls in next year */
            wk = 1;
            y++;
        }
    }
    *isoyear = y;
    return wk;
}

static void fmt(OBuf *o, const char *f, const ShTm *tm, long off, int utc)
{
    for (; *f; f++) {
        int pad = 0, c;
        if (*f != '%') {
            ob_putc(o, *f);
            continue;
        }
        f++;
        if (*f == '-' || *f == '_' || *f == '0' || *f == '^' || *f == '#') {
            pad = *f == '_' ? ' ' : *f == '-' ? '-' : *f == '0' ? '0' : 0;
            f++;
        }
        c = (unsigned char)*f;
#define P(dflt) (pad ? pad : (dflt))
        switch (c) {
        case 0: ob_putc(o, '%'); f--; break;
        case '%': ob_putc(o, '%'); break;
        case 'n': ob_putc(o, '\n'); break;
        case 't': ob_putc(o, '\t'); break;
        case 'a': { char s[4]; memcpy(s, wday_name[tm->wday % 7], 3); s[3] = 0; ob_puts(o, s); break; }
        case 'A': ob_puts(o, wday_name[tm->wday % 7]); break;
        case 'b': case 'h': { char s[4]; memcpy(s, mon_name[tm->mon % 12], 3); s[3] = 0; ob_puts(o, s); break; }
        case 'B': ob_puts(o, mon_name[tm->mon % 12]); break;
        case 'c': fmt(o, "%a %b %e %H:%M:%S %Y", tm, off, utc); break;
        case 'C': ob_num(o, floordiv(tm->year, 100), 2, P('0')); break;
        case 'd': ob_num(o, tm->mday, 2, P('0')); break;
        case 'D': case 'x': fmt(o, "%m/%d/%y", tm, off, utc); break;
        case 'e': ob_num(o, tm->mday, 2, P(' ')); break;
        case 'F': ob_num(o, tm->year, 4, '0'); fmt(o, "-%m-%d", tm, off, utc); break;
        case 'G': case 'g': case 'V': {
            long iy;
            int w = iso_week(tm, &iy);
            if (c == 'V') ob_num(o, w, 2, P('0'));
            else if (c == 'G') ob_num(o, iy, 4, P('0'));
            else ob_num(o, ((iy % 100) + 100) % 100, 2, P('0'));
            break;
        }
        case 'H': ob_num(o, tm->hour, 2, P('0')); break;
        case 'k': ob_num(o, tm->hour, 2, P(' ')); break;
        case 'I': ob_num(o, (tm->hour + 11) % 12 + 1, 2, P('0')); break;
        case 'l': ob_num(o, (tm->hour + 11) % 12 + 1, 2, P(' ')); break;
        case 'j': ob_num(o, tm->yday + 1, 3, P('0')); break;
        case 'm': ob_num(o, tm->mon + 1, 2, P('0')); break;
        case 'M': ob_num(o, tm->min, 2, P('0')); break;
        case 'N': ob_puts(o, "000000000"); break;
        case 'p': ob_puts(o, tm->hour < 12 ? "AM" : "PM"); break;
        case 'P': ob_puts(o, tm->hour < 12 ? "am" : "pm"); break;
        case 'r': fmt(o, "%I:%M:%S %p", tm, off, utc); break;
        case 'R': fmt(o, "%H:%M", tm, off, utc); break;
        case 's': ob_num(o, sh_timegm(tm) - off, 1, '-'); break;
        case 'S': ob_num(o, tm->sec, 2, P('0')); break;
        case 'T': case 'X': fmt(o, "%H:%M:%S", tm, off, utc); break;
        case 'u': ob_num(o, tm->wday == 0 ? 7 : tm->wday, 1, '-'); break;
        case 'w': ob_num(o, tm->wday, 1, '-'); break;
        case 'U': ob_num(o, (tm->yday + 7 - tm->wday) / 7, 2, P('0')); break;
        case 'W': ob_num(o, (tm->yday + 7 - (tm->wday + 6) % 7) / 7, 2, P('0')); break;
        case 'y': ob_num(o, ((tm->year % 100) + 100) % 100, 2, P('0')); break;
        case 'Y': ob_num(o, tm->year, 1, '-'); break;
        case 'z': case 'Z': {
            long a;
            if (c == 'Z' && (utc || off == 0)) {
                ob_puts(o, "UTC");
                break;
            }
            a = off < 0 ? -off : off;
            ob_putc(o, off < 0 ? '-' : '+');
            ob_num(o, a / 3600, 2, '0');
            ob_num(o, a / 60 % 60, 2, '0');
            break;
        }
        default:
            ob_putc(o, '%');
            ob_putc(o, c);
            break;
        }
#undef P
    }
}

size_t sh_strftime_tz(char *buf, size_t n, const char *f, const ShTm *tm, long off, int utc)
{
    OBuf o;
    o.buf = buf;
    o.n = n;
    o.len = 0;
    fmt(&o, f, tm, off, utc);
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return o.len < n ? o.len : n - 1;
}

size_t sh_strftime(char *buf, size_t n, const char *f, const ShTm *tm)
{
    return sh_strftime_tz(buf, n, f, tm, os_tz_offset(), 0);
}
