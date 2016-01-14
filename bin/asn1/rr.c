#include <sys/types.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>
#include <stdio.h>
#include "casn/asn.h"
#include "util/logging.h"

#define MSG_OK "RR finished OK. Wrote %d bytes"
#define MSG_PARAM "Invalid parameter %s"
#define MSG_MEM "Can't get memory"
#define MSG_EXTRA "Extra '}'"
#define MSG_OPEN "Can't open %s"
#define MSG_OVERFLOW "Area %s overflowed"

/**
Function: Transfers the contents of asn_area to output.  Note the setting of
asn_area.next to zero to force putout to go elsewhere
**/
static void
dump_asn(
    );

static void
putasn(
    uchar);

static void
putout(
    uchar);

char buf[512];
char *hash_start;

static char *
cvt_int(
    char *);

static char *
cvt_obj_id(
    char *,
    char *);

/*
 * Function: Converts string pointed to by c and puts it in right place
 *
 * IF string is decimal, convert it to a hex string
 * See if there is an odd number of nibbles
 * IF so. put out the first nibble as a number
 * FOR each byte pair
 *     Convert byte pairs to a byte
 *     Write the byte to output
 */
static char *
cvt_out(
    char *);

/*
 * Function: Converts text PM-request/response to true form
 *
 * Inputs: Standard input is an input file
 *
 * Outputs: Standard output is result
 *
 * 1. IF not at top level, skip to first blank
 *    Starting with no left margin, WHILE forever
 *         Skip white space
 *         IF at line end
 *             Get another line
 *             Go to first non-blank
 *             IF no left margin, set it to greater of this or min
 *             IF at EOF OR line starts before left margin
 *                 IF translating dot notation, put it out
 *                 IF had a start for this line, set the length
 *                 IF not at top level, return current pointer
 *                 Break out of WHILE
 * 2.      IF not in a comment
 *             IF no left margin, set it to this
 *             IF char is numeric,
 *                 IF string is decimal or hex, output data to appropriate
 *                   place
 *                 ELSE (dot notation) append to dot_buf
 *             ELSE IF char is quote, put data in appropriate place
 *             ELSE IF char is '{'
 *                 IF this is the first one, get space
 *                 Set the offset in varfld
 *             ELSE IF char is '}'
 *                 Set the length in varfld
 *                 Write varfld to output
 *             ELSE IF char is '/' OR '-', note half in comment
 *             ELSE IF starting a hash here, mark that
 *             ELSE
 *                 IF not at a reserved word, skip it
 *                 ELSE
 *                     IF had an ASN.1 item started, set its length
 *                     IF there's a tag, print it
 *                     ELSE convert the next word from hex
 *                     Put out zero length
 *                     IF it's constructed
 *                         Call this function for the next level
 *                         Set the length of this item
 *                         IF should be at a higher level, return current
 *                           pointer
 *                         IF at end of line, break out of WHILE
 *                         Continue in WHILE
 * 3.      ELSE IF half in a comment
 *                 IF char is second half, note fully in comment
 *                 ELSE IF char is non-whitespace,
 *                     Note not in comment
 *                     Back up one char to repeat
 *         ELSE IF fully in comment
 *             IF char is firts exit char, note half out of comment
 *             ELSE IF char is non-whitespace, note fully out of comment
 *         ELSE IF half out of comment
 *             IF char is final exit char, note out of comment
 *             ELSE IF char is non-whitespace, note fully in comment
 *         IF at a non-null, go to next char
 * 4. IF have anything in asn_area, put that out
 *    IF have any vararea, write it to output
 *    IF -r switch is set, put length in proper field
 */
static char *
do_it(
    char *,
    int,
    int);

static char *
getbuf(
    char *,
    int);

struct varfld {
    /* offset of field in vararea */
    ushort offset;
    /*
     * length of variable-length field.  If field is not present, lth
     * is zero
     */
    ushort lth;
} varfld;

int bytes;
int dflag;
int aflag;
int req;
int linenum;

static int
adj_asn(
    int);

int
set_asn_lth(
    uchar *,
    uchar *);

/* s1 is target */
static int
wdcmp(
    char *s1,
    char *s2);

static int
write_out(
    char *,
    int);

static int
write_varfld(
    struct varfld *);

struct name_tab {
    char *name;
    unsigned chunk;
    unsigned limit;
    /* pointer to a general name area */
    char *area;
    /* size of the area */
    unsigned size;
    /* offset to next free part of area */
    unsigned next;
};

/* for varareas */
struct name_tab genarea = {"genarea", 1024, 0x4000, NULL, 0, 0};
/* for all output, to avoid having to do lseek() with -r option */
struct name_tab out_area = {"out_area", 1024, 0x20000, NULL, 0, 0};
struct name_tab asn_area = {"asn_area", 1024, 0x20000, NULL, 0, 0};

/* in asn.c */
extern struct typnames typnames[];

/*
 * 1. Scan argvs to see if -r flag is set
 *    IF standard input and output have been redirected, run with them
 *    Scan argvs and at any file name
 *         Append .raw and open that as standard input
 *         Append .req and open that as standard output
 *         Convert the input file
 *         Close both standard input and output
 * 2. Exit with OK message
 */
int main(
    int argc,
    char **argv)
{
    char *c,
      **p;
    int fd;

    if (argc < 2 && isatty(0) && isatty(1))
    {
        printf("usage: rr [-d] f1 f2 f3 ...\n");
        printf("Convert each raw-format ASN.1 file fi.raw to fi.req\n");
        printf("-d  converts output of 'dump -b' back to ASN.1\n");
        exit(1);
    }

    for (p = &argv[1]; p < &argv[argc]; p++)
    {
        if (*(c = *p) == '-')
        {
            if (*(++c) == 'd')
                dflag = 1;
            else
                FATAL(MSG_PARAM, *p);
        }
    }
    if (!isatty(0) && !isatty(1))
    {
        do_it(buf, 0, 0);
        write(1, out_area.area, out_area.next);
        out_area.next = 0;
    }
    for (p = &argv[1]; p < &argv[argc]; p++)
    {
        if (*(c = *p) == '-');
        else
        {
            strcat(strcpy(buf, *p), ".raw");
            if ((fd = open(buf, O_RDONLY)) < 0 || dup2(fd, 0) < 0)
                FATAL(MSG_OPEN, buf);
            strcat(strcpy(buf, *p), ".req");
            if ((fd = open(buf, (O_WRONLY | O_CREAT | O_TRUNC), 0777)) < 0 ||
                dup2(fd, 1) < 0)
                FATAL(MSG_OPEN, buf);
            *buf = 0;
            do_it(buf, 0, 0);
            write(1, out_area.area, out_area.next);
            out_area.next = 0;
        }
    }
    DONE(MSG_OK, bytes);
    return 0;
}

char *do_it(
    char *c,
    int min,
    int level)
{
    char *b,
       *lmarg,
        quote,
        dot_buf[80],
       *edot;
    int val,
        start = -1;
    /* contains up to 3 chars, 2 entry & 1 exit */
    char comment[4];
    ushort lth;
    struct typnames *tpnmp = (struct typnames *)0;
    *(edot = dot_buf) = 0;
    memset(comment, 0, sizeof(comment));
    if (level)
        while (*c > ' ')
            c++;
    /* step 1 */
    for (lmarg = (char *)0; 1;)
    {
        while (*c && *c <= ' ')
            c++;
        if (!*c)
        {
            if (comment[0] && comment[0] == comment[1])
                memset(comment, 0, 3);
            for (c = getbuf(buf, sizeof(buf)); c && *c && *c <= ' '; c++);
            if (!lmarg)
                lmarg = (c > &buf[min]) ? c : &buf[min];
            if (!c || c < lmarg || (c == lmarg && start >= 0))
            {
                if (edot > dot_buf)
                {
                    cvt_obj_id(dot_buf, edot);
                    *(edot = dot_buf) = 0;
                }
                if (start >= 0 && c <= lmarg)
                    start = adj_asn(start);
                if (c < lmarg)
                {
                    if (level)
                        return c;
                    break;
                }
            }
        }
        /* step 2 */
        if (!comment[0])
        {
            if (aflag && *c == '(')
            {
                while (*c != ')')
                    // skip lth field
                    c++;
                // go to next char
                for (c++; *c == ' '; c++);
            }
            if (*c >= '0' && *c <= '9')
            {
                for (b = c; *b >= '0' && *b <= '9'; b++);
                if (*b != '.')
                    c = cvt_out(c);
                else
                    while (*c > ' ')
                        *edot++ = *c++;
            }
            else if (*c == '"' || *c == '\'')
                for (quote = *c++; *c != quote; putout(*c++))
                {
                    if (*c == '\\')
                        c++;
                }
            else if (*c == '{')
            {
                if (!genarea.area)
                {
                    if (!
                        (genarea.area =
                         calloc((genarea.size = genarea.chunk), 1)))
                        FATAL(MSG_MEM);
                    if (bytes & 1)
                        bytes += write_out("", 1);
                }
                if (genarea.next & 1)
                    genarea.next++;
                varfld.offset = genarea.next;
            }
            else if (*c == '}')
            {
                if (level)
                    return c;
                if (asn_area.next)
                    dump_asn();
                varfld.lth = genarea.next - varfld.offset;
                bytes += write_varfld(&varfld);
            }
            else if (*c == '/' || *c == '-')
                comment[0] = *c;
            else if (!wdcmp("sth", c))
            {
                hash_start = &asn_area.area[asn_area.next];
                c += 3;
            }
            else if (*c)
            {
                if (!lmarg)
                    lmarg = c;
                for (tpnmp = typnames; tpnmp->name && wdcmp(tpnmp->name, c);
                     tpnmp++);
                if (!tpnmp->name)
                    while (*c > ' ')
                        c++;
                else
                {
                    if (start >= 0)
                        start = adj_asn(start);
                    start = asn_area.next;
                    if (tpnmp->typ)
                    {
                        putasn(tpnmp->typ);
                        if ((tpnmp->typ & 0xC0))
                        {
                            for (b = &c[3]; *c && *c++ != '+';);
                            if (*c == '0' && (*(++c) | 0x20) == 'x')
                            {
                                c++;
                                val = *c - '0' - ((*c > '9') ? 7 : 0) -
                                    ((*c >= 'a') ? 0x20 : 0);
                                val <<= 4;
                                *c++ = '0';
                                val |= *c - '0' - ((*c > '9') ? 7 : 0) -
                                    ((*c >= 'a') ? 0x20 : 0);
                                *c++ = 'x';
                            }
                            else
                                for (val = 0; *c >= '0' && *c <= '9'; val =
                                     (val * 10) + *c++ - '0');
                            asn_area.area[asn_area.next - 1] |= (char)val;
                            for (c = cvt_out(&c[-2]); c > b; *(--c) = ' ');
                        }
                    }
                    else
                    {
                        for (b = c; *b > ' '; b++);
                        while (*b && *b <= ' ')
                            b++;
                        for (b = cvt_out(b); b > c; *(--b) = ' ');
                    }
                    putasn((char)0);
                    if ((asn_area.area[start] & ASN_CONSTRUCTED))
                    {
                        if (asn_area.area[start] ==
                            (ASN_CONSTRUCTED | ASN_BITSTRING)
                            || asn_area.area[start] ==
                            (ASN_CONSTRUCTED | ASN_OCTETSTRING))
                            asn_area.area[start] &= ~(ASN_CONSTRUCTED);
                        c = do_it(++c, &lmarg[1] - buf, level + 1);
                        start = adj_asn(start);
                        if (c < lmarg && level)
                            return c;
                        if (!c)
                            break;
                        continue;
                    }
                    else
                    {
                        while (*c > ' ')
                            c++;
                        if (tpnmp->typ == ASN_INTEGER)
                            c = cvt_int(c);
                    }
                }
            }
        }
        /* step 3 */
        else if (!comment[1])
        {
            if ((comment[0] == '/' && (*c == '*' || *c == '/')) ||
                (comment[0] == '-' && *c == '-'))
                comment[1] = *c;
            else
                comment[0] = 0;
        }
        else if (!comment[2] && comment[1] != '/' && *c == comment[1])
            comment[2] = *c;
        else if (comment[2] && *c == comment[0])
            memset(comment, 0, 3);
        else
            comment[2] = 0;
        if (*c)
            c++;
    }
    if (edot > dot_buf)
        cvt_obj_id(dot_buf, edot);
    if (asn_area.next)
        /* step 4 */
        dump_asn();
    if (genarea.area)
    {
        if (genarea.next & 1)
            genarea.next++;
        bytes += write_out(genarea.area, genarea.next);
    }
    if (req)
    {
        lth = bytes - 4;
        out_area.area[3] = (char)(lth & 0xFF);
        out_area.area[2] = (char)((lth >> 8) & 0xFF);
    }
    return c;
}

int adj_asn(
    int start)
{
    putasn((char)0);
    putasn((char)0);
    asn_area.next -= 2;
    asn_area.next += set_asn_lth((uchar *) & asn_area.area[start],
                                 (uchar *) & asn_area.area[asn_area.next]);
    return -1;
}

char *cvt_int(
    char *c)
{
    ulong uval;
    long val;
    char *b = 0,
        sign,
        valbuf[32];

    memset(valbuf, 0, 32);
    while (*c && *c <= ' ')
        c++;
    if (*c == '0' && c[1] == 'x')
        return cvt_out(c);
    if (*c == '-')
        sign = *c++;
    else
        sign = 0;
    for (uval = 0; *c >= '0' && *c <= '9';
         uval = (uval * 10) + *c++ - '0');
    if (!sign)
    {
        if ((uval & 0x80000000))
            sprintf(valbuf, "0x00%08lX", uval);
        else
        {
            sprintf(valbuf, "0x%08lX", uval);
            if (uval < 128)
                b = &valbuf[8];
            else if (uval < 32768)
                b = &valbuf[6];
            else if (uval < 8388608)
                b = &valbuf[4];
        }
    }
    else
    {
        val = uval;
        val = -val;
        sprintf(valbuf, "0x%08lX", val);
        if (val < 128 && val >= -128)
            b = &valbuf[8];
        else if (val < 32768 && val >= -32768)
            b = &valbuf[6];
        else if (val < 8388608 && val >= -8388608)
            b = &valbuf[4];
    }
    if (b)
        memmove(&valbuf[2], b, strlen(b) + 1);
    b = cvt_out(valbuf);
    return c;
}

char *cvt_obj_id(
    char *from,
    char *to)
{
    uchar *b,
        locbuf[20],
       *e = &locbuf[sizeof(locbuf)];
    long val,
        tmp;
    /* do first field */
    for (val = 0; from < to && *from != '.'; val = (val * 10) + *from++ - '0');
    val *= 40;
    for (from++, tmp = 0; from < to && *from != '.'; tmp = (tmp * 10) + *from++
         - '0');
    val += tmp;
    for (b = e, tmp = val; val; val >>= 7)
        *(--b) = (uchar) (val & 0x7F) | ((tmp != val) ? 0x80 : 0);
    while (b < e)
        putout(*b++);
    /* now do next fields */
    for (from++; from < to; from++)
    {
        for (val = 0; from < to && *from != '.';
             val = (val * 10) + *from++ - '0');
        if (!val)
            *(b = &e[-1]) = 0;
        else
            for (b = e, tmp = val; val; val >>= 7)
                *(--b) = (uchar) (val & 0x7F) | ((tmp != val) ? 0x80 : 0);
        while (b < e)
            putout(*b++);
    }
    return from;
}

char *cvt_out(
    char *c)
{
    uchar val;
    char *a,
       *b,
        valbuf[8];
    long lval;

    if (*c != '0' && (c[1] | 0x20) != 'x')
    {
        memset(valbuf, 0, sizeof(valbuf));
        for (lval = 0; *c >= '0' && *c <= '9';
             lval = (lval * 10) + *c++ - '0');
        a = c;
        c = valbuf;
        sprintf(valbuf, "0x%lX", lval);
    }
    else
        a = (char *)0;
    for (c += 2, b = c; (*b >= '0' && *b <= '9') || ((*b | 0x20) >= 'a' &&
                                                     (*b | 0x20) <= 'f'); b++);
    if ((((int)(c - b)) & 1))
    {
        if (*c > '9')
            val = (*c++ | 0x20) - 0x27 - '0';
        else
            val = *c++ - '0';
        putout(val);
    }
    while (c < b)
    {
        if (*c > '9')
            val = (*c++ | 0x20) - 0x27 - '0';
        else
            val = *c++ - '0';
        val <<= 4;
        if (*c > '9')
            val += (*c++ | 0x20) - 0x27 - '0';
        else
            val += *c++ - '0';
        putout(val);
    }
    return (a) ? a : c;
}

void dump_asn(
    )
{
    char *c,
       *e;
    for (c = asn_area.area, e = &c[asn_area.next], asn_area.next = 0; c < e;
         putout(*c++));
}

char *getbuf(
    char *to,
    int lth)
{
    char *b,
       *c,
       *e;
    int col;
    if (!fgets(to, lth, stdin))
        return (char *)0;
    linenum++;
    for (b = to; b < &to[lth] && *b && *b != '\n'; b++);
    if (b >= &to[lth])
        FATAL(MSG_OVERFLOW, "input buffer");
    if (*b == '\n')
        *b = 0;
    e = b;
    if (!dflag)
    {
        for (b = to; b < e;)
        {
            if (*b == '\t')
            {
                col = b - to;
                col = 7 - (col & 0x7);
                for (c = e, e = &c[col]; c > b; c[col] = *c, c--);
                for (c += col + 1; b < c; *b++ = ' ');
            }
            else
                b++;
        }
    }
    else
    {
        e[-16] = 0;
        if (*to <= ' ')
            for (b = to; *b && *b <= ' '; b++);
        else
            b = to;
        while (*b > ' ')
            b++;
        while (*b && *b <= ' ')
            b++;
        *to = '0';
        to[1] = 'x';
        strcpy(&to[2], b);
        for (b = &to[2]; *b;)
        {
            if (*b <= ' ')
                strcpy(b, &b[1]);
            else
                b++;
        }
    }
    return to;
}

int numconv(
    char *c)
{
    int val = 0;
    if (*c == '0' && (*(++c) | 0x20) == 'x')
    {
        for (c++;
             (*c >= '0' && *c <= '9') || ((*c | 0x20) >= 'a'
                                          && (*c | 0x20) <= 'f');
             val = (val << 4) + (*c++ | 0x20) - 0x27 - '0');
    }
    return val;
}

void putasn(
    uchar val)
{
    if (!asn_area.area && !(asn_area.area = calloc((asn_area.size =
                                                    asn_area.chunk), 1)))
        FATAL(MSG_MEM);
    if (asn_area.next >= asn_area.size)
    {
        if (!(asn_area.area = realloc(asn_area.area, (asn_area.size +=
                                                      asn_area.chunk))))
            FATAL(MSG_MEM);
        if (asn_area.size >= asn_area.limit)
            FATAL(MSG_OVERFLOW, asn_area.name);
    }
    asn_area.area[asn_area.next++] = val;
}

void putout(
    uchar val)
{
    if (asn_area.next)
        putasn(val);
    else if (!genarea.area)
        bytes += write_out((char *)&val, 1);
    else
    {
        if (genarea.next >= genarea.size)
        {
            if (!(genarea.area = realloc(genarea.area, (genarea.size +=
                                                        genarea.chunk))))
                FATAL(MSG_MEM);
            if (genarea.size >= genarea.limit)
                FATAL(MSG_EXTRA);
        }
        genarea.area[genarea.next++] = val;
    }
}

int wdcmp(
    char *s1,
    char *s2)
{
    for (; *s1 > '+' && *s1 == *s2; s1++, s2++);
    if (*s1 > '+' || (*s1 != '+' && *s2 > '+'))
        return -1;
    return 0;
}

int write_out(
    char *c,
    int lth)
{
    char *b,
       *e;
    if (!out_area.area && !(out_area.area = calloc(out_area.chunk, 1)))
        FATAL(MSG_MEM);
    while (out_area.next + lth > out_area.size)
    {
        if ((out_area.size += out_area.chunk) > out_area.limit)
            FATAL(MSG_OVERFLOW, out_area.name);
        if (!(out_area.area = realloc(out_area.area, out_area.size +=
                                      out_area.chunk)))
            FATAL(MSG_MEM);
    }
    for (b = &out_area.area[out_area.next], e = &b[lth]; b < e; *b++ = *c++);
    out_area.next = b - out_area.area;
    return lth;
}

int write_varfld(
    struct varfld *varfldp)
{
    char c = (char)((varfldp->offset >> 8) & 0xFF);
    write_out(&c, 1);
    c = (char)(varfldp->offset & 0xFF);
    write_out(&c, 1);
    c = (char)((varfldp->lth >> 8) & 0xFF);
    write_out(&c, 1);
    c = (char)(varfldp->lth & 0xFF);
    write_out(&c, 1);
    return sizeof(struct varfld);
}
