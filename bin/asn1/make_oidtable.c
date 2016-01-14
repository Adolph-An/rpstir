#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "util/logging.h"

struct oidtable {
    char *oid;
    char *label;
};

#define MSG_OK "Finished OK"
#define MSG_OPEN "Can't open %s"
#define MSG_LINE "Error in this line: %s"
#define MSG_USAGE "Usage: tablefilename, file.h ..."

static int diff_oid(
    char *o1,
    char *o2)
{
    int x1,
        x2;
    char *c1,
       *c2;
    for (c1 = o1, c2 = o2; 1; c1++, c2++)
    {
        sscanf(c1, "%d", &x1);
        sscanf(c2, "%d", &x2);
        if (x1 > x2)
            return 1;
        if (x1 < x2)
            return -1;
        while (*c1 && *c1 != '.')
            c1++;
        while (*c2 && *c2 != '.')
            c2++;
        if (!*c1)
        {
            if (!*c2)
                return 0;
            return -1;
        }
        else if (!*c2)
            return 1;
    }
    return 0;                   // never happens
}

int main(
    int argc,
    char **argv)
{
    char *outfile = *(++argv),
        linebuf[512];
    struct oidtable *oidtable;
    int numoids = 16,
        oidnum;
    FILE *str;
    if (argc < 2)
        FATAL(MSG_USAGE);

    oidtable = (struct oidtable *)calloc(numoids, sizeof(struct oidtable));
    for (argv++, oidnum = 0; argv && *argv; argv++)
    {
        // int linenum = 1;
        if (!(str = fopen(*argv, "r")))
            FATAL(MSG_OPEN, *argv);
        char *c;
        while (fgets(linebuf, 512, str))
        {
            // fprintf(stderr, "%d %s", linenum++, linebuf);
            if (strncmp(linebuf, "#define ", 8))
                continue;
            char *l,
               *o;
            if (linebuf[8] == '_')
                continue;
            for (c = &linebuf[8]; *c && *c <= ' '; c++);
            if (!*c)
                FATAL(MSG_LINE, linebuf);
            for (l = c; *c && *c > ' '; c++);
            if (!*c)
                FATAL(MSG_LINE, linebuf);
            *c++ = 0;
            while (*c && *c <= ' ')
                c++;
            if (!*c || *c != '"')
                continue;
            c++;
            if (!*c || *c < '0' || *c > '9')
                FATAL(MSG_LINE, linebuf);
            int j;
            if (sscanf(c, "%d.", &j) < 1 || j > 2)
                continue;
            o = c;
            for (o = c; *c && *c != '.' && *c != '"'; c++);
            if (!*c)
                FATAL(MSG_LINE, linebuf);
            if (*c != '.')
                continue;       // just one segment
            while (*c && *c != '"')
                c++;
            if (!*c)
                FATAL(MSG_LINE, linebuf);
            *c = 0;
            if (oidnum >= numoids)
                oidtable = (struct oidtable *)realloc(oidtable,
                                                      (numoids +=
                                                       16) *
                                                      sizeof(struct oidtable));
            // fprintf(stderr, "oidnum %d numoids %d\n", oidnum, numoids);
            struct oidtable *odp = &oidtable[oidnum];
            odp->label = (char *)calloc(1, strlen(l) + 2);
            strcpy(odp->label, l);
            odp->oid = (char *)calloc(1, strlen(o) + 2);
            strcpy(odp->oid, o);
            oidnum++;
        }
        fclose(str);
    }
    int i;
    for (i = 1; i < oidnum;)
    {
        struct oidtable *lodp = &oidtable[i - 1],
            *hodp = &oidtable[i];
        if (diff_oid(lodp->oid, hodp->oid) < 0) // need to swap
        {
            struct oidtable tod;
            tod.oid = hodp->oid;
            tod.label = hodp->label;
            hodp->oid = lodp->oid;
            hodp->label = lodp->label;
            lodp->oid = tod.oid;
            lodp->label = tod.label;
            i = 1;
        }
        else
            i++;
    }
    if (!(str = fopen(outfile, "w")))
        FATAL(MSG_OPEN, outfile);
    char lastoid[256];
    memset(lastoid, 0, 256);
    for (i = 0; i < oidnum; i++)
    {
        struct oidtable *curr_oidp = &oidtable[i];      // eliminate
                                                        // duplicates
        if (strlen(lastoid) == strlen(curr_oidp->oid)
            && !strcmp(lastoid, curr_oidp->oid))
            continue;
        fprintf(str, "%s %s\n", curr_oidp->oid, curr_oidp->label);
        strcpy(lastoid, curr_oidp->oid);
    }
    fclose(str);
    return 0;
}
