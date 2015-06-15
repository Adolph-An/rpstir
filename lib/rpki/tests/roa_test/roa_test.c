/*
 * $Id$ 
 */

#include "util/gettext_include.h"

#include "rpki/cms/roa_utils.h"

#include "rpki/scm.h"
#include "rpki/scmf.h"
#include "rpki/sqhl.h"
#include "rpki/err.h"

static unsigned char *myreadfile(
    char *fn,
    int *stap)
{
    struct stat mystat;
    char *outptr = NULL;
    char *ptr;
    int outsz = 0;
    int sta;
    int fd;
    int rd;

    if (stap == NULL)
        return (NULL);
    if (fn == NULL || fn[0] == 0)
    {
        *stap = ERR_SCM_INVALARG;
        return (NULL);
    }
    fd = open(fn, O_RDONLY);
    if (fd < 0)
    {
        *stap = ERR_SCM_COFILE;
        return (NULL);
    }
    memset(&mystat, 0, sizeof(mystat));
    if (fstat(fd, &mystat) < 0 || mystat.st_size == 0)
    {
        (void)close(fd);
        *stap = ERR_SCM_COFILE;
        return (NULL);
    }
    ptr = (char *)calloc(mystat.st_size, sizeof(char));
    if (ptr == NULL)
    {
        (void)close(fd);
        *stap = ERR_SCM_NOMEM;
        return (NULL);
    }
    rd = read(fd, ptr, mystat.st_size);
    (void)close(fd);
    if (rd != mystat.st_size)
    {
        free((void *)ptr);
        ptr = NULL;
        *stap = ERR_SCM_COFILE;
    }
    else
        *stap = 0;
    if (strstr(fn, ".pem") == NULL)
        return ((unsigned char *)ptr);  /* not a PEM file, just plain DER */
    sta =
        decode_b64((unsigned char *)ptr, mystat.st_size,
                   (unsigned char **)&outptr, &outsz, "ROA");
    free((void *)ptr);
    if (sta < 0)
    {
        if (outptr != NULL)
        {
            free((void *)outptr);
            outptr = NULL;
        }
    }
    return ((unsigned char *)outptr);
}

int main(
    int argc,
    char **argv)
{
    struct CMS roa;
    struct CMS roa2;
    unsigned char *blob = NULL;
    FILE *fp = NULL;
    scmcon *conp;
    scm *scmp;
    X509 *cert;
    char filename_der[16] = "";
    char filename_pem[16] = "";
    char errmsg[1024];
    char *filename_cnf = NULL;
    char *ski;
    char *fn = NULL;
    int sta = 0;

    CMS(&roa, (ushort) 0);
    CMS(&roa2, (ushort) 0);
    if (argc < 2)
        filename_cnf = "roa.cnf";
    else
        filename_cnf = argv[1];
    strncpy(filename_der, "mytest.roa.der", sizeof(filename_der) - 1);
    strncpy(filename_pem, "mytest.roa.pem", sizeof(filename_pem) - 1);
    sta = roaFromConfig(filename_cnf, 0, &roa);
    if (sta < 0)
    {
        (void)fprintf(stderr, _("roaFromConfig(%s) failed with error %s (%d)\n"),
                      filename_cnf, err2string(sta), sta);
        return sta;
    }
    sta = roaToFile(&roa, filename_pem, FMT_PEM);
    delete_casn(&roa.self);
    if (sta < 0)
    {
        (void)fprintf(stderr, _("roaToFile(%s) failed with error %s (%d)\n"),
                      filename_pem, err2string(sta), sta);
        return sta;
    }
    sta = roaFromFile(filename_pem, FMT_PEM, cTRUE, &roa2);
    if (sta < 0)
    {
        (void)fprintf(stderr, _("roaFromFile(%s) failed with error %s (%d)\n"),
                      filename_pem, err2string(sta), sta);
        return sta;
    }
    ski = (char *)roaSKI(&roa2);
    if (ski == NULL || ski[0] == 0)
    {
        (void)fprintf(stderr, _("ROA has NULL SKI\n"));
        return -2;
    }
    scmp = initscm();
    if (scmp == NULL)
    {
        delete_casn(&roa2.self);
        free(ski);
        (void)fprintf(stderr,
                      _("Internal error: cannot initialize database schema\n"));
        return -3;
    }
    memset(errmsg, 0, 1024);
    conp = connectscm(scmp->dsn, errmsg, 1024);
    if (conp == NULL)
    {
        (void)fprintf(stderr, _("Cannot connect to DSN %s: %s\n"),
                      scmp->dsn, errmsg);
        delete_casn(&roa2.self);
        free(ski);
        freescm(scmp);
        return -4;
    }
    cert = (X509 *) roa_parent(scmp, conp, ski, &fn, &sta);
    disconnectscm(conp);
    freescm(scmp);
    free(ski);
    if (cert == NULL)
    {
        (void)fprintf(stderr,
                      _("ROA certificate has no parent in DB: error %s (%d)\n"),
                      err2string(sta), sta);
        delete_casn(&roa2.self);
        return sta;
    }
    blob = myreadfile(fn, &sta);
    if (blob == NULL)
    {
        (void)fprintf(stderr,
                      _("Cannot read certificate from %s: error %s (%d)\n"), fn,
                      err2string(sta), sta);

        X509_free(cert);
        delete_casn(&roa2.self);
        return (sta);
    }
    sta = roaValidate2(&roa2);
    free((void *)blob);
    X509_free(cert);
    if (sta < 0)
    {
        (void)fprintf(stderr,
                      _("ROA failed semantic validation: error %s (%d)\n"),
                      err2string(sta), sta);
        delete_casn(&roa2.self);
        return sta;
    }
    fp = fopen("roa.txt", "a");
    if (fp == NULL)
    {
        (void)fprintf(stderr, _("Cannot open roa.txt\n"));
        delete_casn(&roa2.self);
        return -5;
    }
    sta = roaGenerateFilter(&roa2, NULL, fp, NULL, 0);
    delete_casn(&roa2.self);
    (void)fclose(fp);
    if (sta < 0)
    {
        (void)fprintf(stderr,
                      _("Cannot generate ROA filter output: error %s (%d)\n"),
                      err2string(sta), sta);
        return sta;
    }
    return 0;
}
