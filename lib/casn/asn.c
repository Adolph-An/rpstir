/**************************************************************************
 * FILE:        asn.c
 * AUTHORs:     Charles Gardiner (gardiner@bbn.com),
 *              John Lowry (jlowry@bbn.com)
 *
 * DESCRIPTION: Routines to support Distinguished Encoding of ASN.1
 *              defined structures.
 *
 *************************************************************************/

#include "asn.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

uchar
asn_typ(
    uchar **);

uchar *
asn_set(
    struct asn *);

int
count_sub_asns(
    uchar **);

struct typnames typnames[] = {
    {ASN_BOOLEAN, "boo"},       /* 1 */
    {ASN_INTEGER, "int"},       /* 2 */
    {ASN_BITSTRING, "bit"},     /* 3 */
    {ASN_BITSTRING | ASN_CONSTRUCTED, "biw"},   /* bit string wrapper */
    {ASN_OCTETSTRING, "oct"},   /* 4 */
    {ASN_OCTETSTRING | ASN_CONSTRUCTED, "ocw"}, /* octet string wrapper */
    {ASN_NULL, "nul"},          /* 5 */
    {ASN_OBJ_ID, "oid"},        /* 6 new preferred nickname */
    {ASN_OBJ_ID, "obj"},        /* 6 */
    {7, "obd"},
    {8, "ext"},
    {ASN_REAL, "rea"},          /* 9 */
    {ASN_ENUMERATED, "enu"},    /* 10 */
    {ASN_UTF8_STRING, "utf"},   /* 12 */
    {ASN_NUMERIC_STRING, "num"},        /* 0x12 */
    {ASN_PRINTABLE_STRING, "prt"},      /* 0x13 */
    {ASN_T61_STRING, "t61"},    /* 0x14 */
    {ASN_VIDEOTEX_STRING, "vtx"},       /* 0x15 */
    {ASN_IA5_STRING, "ia5"},    /* 0x16 */
    {ASN_UTCTIME, "utc"},       /* 0x17 */
    {ASN_GENTIME, "gen"},       /* 0x18 */
    {ASN_GRAPHIC_STRING, "grs"},        /* 0x19 */
    {ASN_VISIBLE_STRING, "vst"},        /* 0x1A */
    {ASN_GENERAL_STRING, "gns"},        /* 0x1B */
    {ASN_UNIVERSAL_STRING, "unv"},      /* 0x1C */
    {ASN_BMP_STRING, "bmp"},    /* 0x1E */
    {ASN_SEQUENCE, "seq"},      /* 0x30 */
    {ASN_SET, "set"},           /* 0x31 */
    {ASN_APPL_SPEC, "app"},     /* 0x40 */
    {ASN_CONT_SPEC, "ctx"},     /* 0x80 */
    {ASN_PRIV_SPEC, "pri"},     /* 0xC0 */
    {0, "oth"},
    {0, 0},
};

int set_asn_lth(
    uchar *,
    uchar *);

uchar *asn_set(
    struct asn *asnp)
{
    uchar *from = asnp->stringp;
    int ansr = -1;
    int lth;
    asn_typ(&from);
    if (((lth = *from++) & ASN_INDEF))
    {
        if ((ansr = (lth &= (uchar) ~ ASN_INDEF)))
        {
            for (lth = 0; ansr--; lth = (lth << 8) + *from++);
        }
    }
    if (!(asnp->lth = lth) && !ansr)
        asnp->level |= ASN_INDEF_FLAG;
    else
        asnp->level &= ~ASN_INDEF_FLAG;
    return from;
}

uchar asn_typ(
    uchar **s)
{
    uchar typ;
    uchar *from = *s;
    if (((typ = *from++) & ASN_XT_TAG) == ASN_XT_TAG)
    {
        while ((*from & ASN_INDEF))
            from++;
        from++;
    }
    *s = from;
    return typ;
}

/**
Function: Counts number of ASN.1 items in string pointed to by from
Inputs: Pointer to ASN.1-encoded string
Outputs: Count of number of items
Procedure: Calls the recursive version
**/
int count_asns(
    unsigned char *from)
{
    return (1 + count_sub_asns(&from));
}

/**
Function: Counts ASN.1 items in recursive fashion
Inputs: Pointer to address of start of item
Outputs: 'from' pointer set to address of next item
         Returns count of items
Procedure:
1. DO
        Set up local asn for current item
        Count it
        IF current item has indefinite length
            IF item is constructed
                DO
                    Add contents of subordinate items to count
                UNTIL remaining data is double null
            ELSE scan forward to double null
2.      ELSE
            IF have no end pointer yet, set end pointer
            IF item is primitive, advance pointer by its length
            IF no end pointer, return count
   WHILE have an end pointer AND haven't reached it
3. Set 'from' pointer
   Return count
**/
int count_sub_asns(
    uchar **from)
{
    int count = 0;
    uchar *c = *from;
    uchar *e = (uchar *)0;
    struct asn asn;
    memset(&asn, 0, sizeof(asn));
    do                          /* step 1 */
    {
        asn.stringp = c;
        c = asn_set(&asn);
        count++;
        if ((asn.level & ASN_INDEF_FLAG))
        {
            if ((*asn.stringp & ASN_CONSTRUCTED))
            {
                do
                {
                    count += count_sub_asns(&c);
                }
                while (*c || c[1]);
            }
            else
                while (*c || c[1])
                    c++;
            c += 2;
        }
        else                    /* step 2 */
        {
            if (!e)
                e = &c[asn.lth];
            if (!(*asn.stringp & ASN_CONSTRUCTED))
                c += asn.lth;
        }
    }
    while (e && c < e);
    *from = c;                  /* step 3 */
    return count;
}

int decode_asn(
    struct asn **asnpp,
    struct asn *easnp,
    uchar *from,
    ulong nbytes,
    ushort level)
{
    struct asn *curr_asnp;
    uchar typ;
    int ansr;
    int did;
    int indef;
    /* step 1 */
    for (did = 0, curr_asnp = *asnpp;
         !nbytes || nbytes > (ulong)did;
         curr_asnp++)
    {
        if (curr_asnp >= easnp)
            return -1;
        *asnpp = curr_asnp;
        typ = *(curr_asnp->stringp = from);
        curr_asnp->level = level;
        from = asn_set(curr_asnp);
        did += from - curr_asnp->stringp;
        if (!(curr_asnp->level & ASN_INDEF_FLAG))
        {
            indef = 0;
            if (nbytes && did + curr_asnp->lth > nbytes)
                return -did;
        }
        else
            indef = 1;
        if ((nbytes && nbytes < (ulong)did) ||
            (typ == ASN_INTEGER && curr_asnp->lth > 1 && ((!*from &&
                                                           !(from[1] & 0x80))
                                                          || (*from == 0xFF
                                                              && (from[1] &
                                                                  0x80))))
            || (typ == ASN_NULL && curr_asnp->lth))
            return -1;
        if ((typ & ASN_CONSTRUCTED))    /* step 2 */
        {
            ansr = 0;
            if ((curr_asnp->lth || (curr_asnp->level & ASN_INDEF_FLAG)))
            {
                (*asnpp)++;
                if (!curr_asnp->lth && !*from && !from[1])
                    ansr = 0;
                else if ((ansr = decode_asn(asnpp, easnp, from,
                                            (ulong) curr_asnp->lth,
                                            level + 1)) < 0)
                    return ansr;
            }
            from += ansr;
            if (indef && !*from && !from[1])
            {
                curr_asnp->lth = (from += 2) - curr_asnp->stringp - 2;
                ansr += 2;
            }
            if (nbytes && (ulong)did + (ulong)ansr > nbytes)
            {
                *asnpp = curr_asnp;
                return -1;
            }
            curr_asnp = *asnpp;
        }
        else if (typ == ASN_NULL)
            ansr = 0;
        else if (!indef)
            from += (ansr = curr_asnp->lth);    /* step 3 */
        else if (*curr_asnp->stringp)
        {                       /* step 4 */
            while (*from || from[1])
                from++;
            from += 2;
            curr_asnp->lth = ansr = from - curr_asnp->stringp - 2;
        }
        if ((ulong)(did += ansr) > nbytes && nbytes)
            return -1;
        if ((!nbytes || indef) && !*from && !from[1])
            break;              /* step 5 */
    }
    if (!level)
    {
        (++(*asnpp))->stringp = (uchar *) 0;
        (*asnpp)->level = (*asnpp)->lth = 0;
    }
    return did;                 /* step 6 */
}

int make_asn_table(
    struct asn **asnbase,
    uchar *c,
    ulong lth)
{
    struct asn *asnp;
    int count = count_asns(c) + 1;
    if (!(*asnbase = (struct asn *)calloc(count, sizeof(struct asn))))
        return 0;
    (asnp = *asnbase)->stringp = c;
    if ((count = decode_asn(&asnp, &asnp[count], c, lth, 0)) < 0)
        count = (c - asnp->stringp);
    return count;
}

int put_asn_lth(
    uchar *to,
    ulong lth)
{
    uchar *c = to;
    ulong tmp = 0;
    if (lth < 128)
        *to = (uchar) lth;
    else
    {
        for (tmp = lth; tmp; tmp >>= 8, c++);
        for (*to = ASN_INDEF + (tmp = (c - to)); c > to; *c-- = (lth & 0xFF),
             lth >>= 8);
    }
    return tmp + 1;
}

int set_asn_lth(
    uchar *s,
    uchar *e)
{
    uchar *c;
    ulong lth;
    ulong tmp;
    int bwd;
    asn_typ(&s);
    if ((bwd = (int)*s++) & ASN_INDEF)
    {
        bwd &= ~ASN_INDEF;
        memcpy(s, &s[bwd], ((e -= bwd) - s));
    }
    else
        bwd = 0;
    if ((lth = e - s) >= 128)
    {
        for (c = e, tmp = lth; tmp; tmp >>= 8, e++);
        while (c > s)
            *(--e) = *(--c);
    }
    return put_asn_lth(--s, lth) - 1 - bwd;
}
