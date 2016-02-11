#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <mysql.h>

#include "rpki/scm.h"
#include "rpki/scmf.h"
#include "rpki/sqhl.h"
#include "rpki/err.h"
#include "config/config.h"
#include "util/logging.h"
#include "util/stringutils.h"


/****************
 * This is the garbage collector client, which tracks down all the
 * objects whose state has been changed due to the passage of time
 * and updates its state accordingly.
 **************/

static char *prevTimestamp,
   *currTimestamp;
static char *theIssuer,
   *theAKI;                     // for passing to callback
static unsigned int theID;      // for passing to callback
static sqlcountfunc countHandler;       // used by countCurrentCRLs
static scmtab *certTable,
   *crlTable,
   *gbrTable,
   *roaTable,
   *manifestTable;

/**
 * @brief
 *     callback function for searchscm() that records the timestamps
 */
static err_code
handleTimestamps(
    scmcon *conp,
    scmsrcha *s,
    ssize_t numLine)
{
    UNREFERENCED_PARAMETER(conp);
    UNREFERENCED_PARAMETER(numLine);
    currTimestamp = (char *)s->vec[0].valptr;
    prevTimestamp = (char *)s->vec[1].valptr;
    return 0;
}

/**
 * @brief
 *     callback for countCurrentCRLs() search
 *
 * check if count == 0, and if so then do the setting of certs' flags
 */
static err_code
handleIfStale(
    scmcon *conp,
    scmsrcha *s,
    ssize_t cnt)
{
    UNREFERENCED_PARAMETER(s);
    char msg[600];
    char escaped_aki[2 * strlen(theAKI) + 1];
    char escaped_issuer[2 * strlen(theIssuer) + 1];
    if (cnt > 0)
        return 0;               // exists another crl that is current
    mysql_escape_string(escaped_aki, theAKI, strlen(theAKI));
    mysql_escape_string(escaped_issuer, theIssuer, strlen(theIssuer));
    xsnprintf(msg, 600,
              "update %s set flags = flags + %d where aki=\"%s\" and issuer=\"%s\"",
              certTable->tabname, SCM_FLAG_STALECRL, escaped_aki,
              escaped_issuer);
    addFlagTest(msg, SCM_FLAG_STALECRL, 0, 1);
    addFlagTest(msg, SCM_FLAG_CA, 1, 1);
    xsnprintf(msg + strlen(msg), 600, ";");
    return statementscm_no_data(conp, msg);
}

/**
 * @brief
 *     callback for countCurrentCRLs() search
 *
 * check if count > 0, and if so then remove unknown flag from cert
 */
static err_code
handleIfCurrent(
    scmcon *conp,
    scmsrcha *s,
    ssize_t cnt)
{
    char msg[128];
    UNREFERENCED_PARAMETER(s);
    if (cnt == 0)
        return 0;               // exists another crl that is current
    xsnprintf(msg, 128, "update %s set flags = flags - %d where local_id=%d;",
              certTable->tabname, SCM_FLAG_STALECRL, theID);
    return statementscm_no_data(conp, msg);
}

/**
 * @brief
 *     callback function for stale crl search
 *
 * checks stale crls to see if another crl exists that is more recent;
 * if not, it sets all certs covered by this crl to have status
 * stale_crl
 */
static scmsrcha *cntSrch = NULL;

static err_code
countCurrentCRLs(
    scmcon *conp,
    scmsrcha *s,
    ssize_t numLine)
{
    UNREFERENCED_PARAMETER(numLine);
    if (cntSrch == NULL)
    {
        cntSrch = newsrchscm(NULL, 1, 0, 1);
        /** @bug ignores error code without explanation */
        addcolsrchscm(cntSrch, "local_id", SQL_C_ULONG, 8);
    }
    theIssuer = (char *)s->vec[0].valptr;
    theAKI = (char *)s->vec[1].valptr;
    char escaped_aki[2 * strlen(theAKI) + 1];
    char escaped_issuer[2 * strlen(theIssuer) + 1];
    mysql_escape_string(escaped_aki, theAKI, strlen(theAKI));
    mysql_escape_string(escaped_issuer, theIssuer, strlen(theIssuer));
    if (s->nused > 2)
    {
        theID = *((unsigned int *)s->vec[2].valptr);
    }
    xsnprintf(cntSrch->wherestr, WHERESTR_SIZE,
              "issuer=\"%s\" and aki=\"%s\" and next_upd>=\"%s\"",
              escaped_issuer, escaped_aki, currTimestamp);
    return searchscm(conp, crlTable, cntSrch, countHandler, NULL,
                     SCM_SRCH_DOCOUNT, NULL);
}

/**
 * @brief
 *     callback function for stale manifest search
 *
 * marks accordingly all objects referenced by manifest that is stale
 */
static char staleManStmt[MANFILES_SIZE];
static char *staleManFiles[10000];
static int numStaleManFiles = 0;

static err_code
handleStaleMan2(
    scmcon *conp,
    scmtab *tab,
    char *files)
{
    char escaped_files[2 * strlen(files) + 1];
    mysql_escape_string(escaped_files, files, strlen(files));
    xsnprintf(staleManStmt, MANFILES_SIZE,
              "update %s set flags=flags+%d where (flags%%%d)<%d and \"%s\" regexp binary filename;",
              tab->tabname, SCM_FLAG_STALEMAN,
              2 * SCM_FLAG_STALEMAN, SCM_FLAG_STALEMAN, escaped_files);
    return statementscm_no_data(conp, staleManStmt);
}

static err_code
handleStaleMan(
    scmcon *conp,
    scmsrcha *s,
    ssize_t numLine)
{
    UNREFERENCED_PARAMETER(numLine);
    UNREFERENCED_PARAMETER(conp);
    int len = *((unsigned int *)s->vec[1].valptr);
    staleManFiles[numStaleManFiles] = malloc(len + 1);
    memcpy(staleManFiles[numStaleManFiles], (char *)s->vec[0].valptr, len);
    staleManFiles[numStaleManFiles][len] = 0;
    numStaleManFiles++;
    return 0;
}

/*
 * callback function for non-stale manifest search that marks accordingly
 * all objects referenced by manifest that is non-stale
 */
static err_code
handleFreshMan2(
    scmcon *conp,
    scmtab *tab,
    char *files)
{
    char escaped_files[2 * strlen(files) + 1];
    mysql_escape_string(escaped_files, files, strlen(files));
    xsnprintf(staleManStmt, MANFILES_SIZE,
              "update %s set flags=flags-%d where (flags%%%d)>=%d and \"%s\" regexp binary filename;",
              tab->tabname, SCM_FLAG_STALEMAN,
              2 * SCM_FLAG_STALEMAN, SCM_FLAG_STALEMAN, escaped_files);
    return statementscm_no_data(conp, staleManStmt);
}

int main(
    int argc,
    char **argv)
{
    scm *scmp = NULL;
    scmcon *connect = NULL;
    scmtab *metaTable = NULL;
    scmsrcha srch;
    scmsrch srch1[4];
    char msg[WHERESTR_SIZE];
    unsigned long blah = 0;
    err_code status;
    int i;

    // initialize
    (void)argc;
    (void)argv;                // silence compiler warnings
    (void)setbuf(stdout, NULL);
    OPEN_LOG("garbage", LOG_USER);
    if (!my_config_load())
    {
        LOG(LOG_ERR, "can't load configuration");
        exit(EXIT_FAILURE);
    }
    scmp = initscm();
    checkErr(scmp == NULL, "Cannot initialize database schema\n");
    connect = connectscm(scmp->dsn, msg, WHERESTR_SIZE);
    checkErr(connect == NULL, "Cannot connect to database: %s\n", msg);
    certTable = findtablescm(scmp, "certificate");
    checkErr(certTable == NULL, "Cannot find table certificate\n");
    crlTable = findtablescm(scmp, "crl");
    checkErr(crlTable == NULL, "Cannot find table crl\n");
    gbrTable = findtablescm(scmp, "ghostbusters");
    checkErr(gbrTable == NULL, "Cannot find table ghostbusters\n");
    roaTable = findtablescm(scmp, "roa");
    checkErr(roaTable == NULL, "Cannot find table roa\n");
    manifestTable = findtablescm(scmp, "manifest");
    checkErr(manifestTable == NULL, "Cannot find table manifest\n");
    srch.vec = srch1;
    srch.sname = NULL;
    srch.ntot = 4;
    srch.where = NULL;
    srch.context = &blah;

    // find the current time and last time garbage collector ran
    metaTable = findtablescm(scmp, "metadata");
    checkErr(metaTable == NULL, "Cannot find table metadata\n");
    srch.nused = 0;
    srch.vald = 0;
    srch.wherestr = NULL;
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "current_timestamp", SQL_C_CHAR, 24);
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "gc_last", SQL_C_CHAR, 24);
    status = searchscm(connect, metaTable, &srch, NULL, handleTimestamps,
                       SCM_SRCH_DOVALUE_ALWAYS, NULL);
    if (status != 0)
    {
        fprintf(stderr, "Error searching for timestamps: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }

    // check for expired certs
    /** @bug ignores error code without explanation */
    certificate_validity(scmp, connect);

    // check for revoked certs
    status = iterate_crl(scmp, connect, revoke_cert_by_serial);
    if (status != 0 && status != ERR_SCM_NODATA)
    {
        fprintf(stderr, "Error checking for revoked certificates: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }

    // do check for stale crls (next update after last time and before this)
    // if no new crl replaced it (if count = 0 for crls with same issuer and
    // aki
    // and next update after this), update state of any certs covered by crl
    // to be unknown
    srch.nused = 0;
    srch.vald = 0;
    xsnprintf(msg, WHERESTR_SIZE, "next_upd<=\"%s\"", currTimestamp);
    srch.wherestr = msg;
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "issuer", SQL_C_CHAR, SUBJSIZE);
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "aki", SQL_C_CHAR, SKISIZE);
    countHandler = handleIfStale;
    status = searchscm(connect, crlTable, &srch, NULL, countCurrentCRLs,
                       SCM_SRCH_DOVALUE_ALWAYS, NULL);
    free(srch1[0].valptr);
    free(srch1[1].valptr);
    if (status != 0 && status != ERR_SCM_NODATA)
    {
        fprintf(stderr, "Error searching for CRLs: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }

    // now check for stale and then non-stale manifests
    // note: by doing non-stale test after stale test, those objects that
    // are referenced by both stale and non-stale manifests, set to not stale
    srch.nused = 0;
    srch.vald = 0;
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "files", SQL_C_BINARY, MANFILES_SIZE);
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "fileslen", SQL_C_ULONG, sizeof(unsigned int));
    numStaleManFiles = 0;
    status = searchscm(connect, manifestTable, &srch, NULL, handleStaleMan,
                       SCM_SRCH_DOVALUE_ALWAYS, NULL);
    if (status != 0 && status != ERR_SCM_NODATA)
    {
        fprintf(stderr, "Error searching for manifests: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < numStaleManFiles; i++)
    {
        /** @bug ignores error code without explanation */
        handleStaleMan2(connect, certTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleStaleMan2(connect, crlTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleStaleMan2(connect, gbrTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleStaleMan2(connect, roaTable, staleManFiles[i]);
        free(staleManFiles[i]);
    }
    srch.vald = 0;
    xsnprintf(msg, WHERESTR_SIZE, "next_upd>\"%s\"", currTimestamp);
    numStaleManFiles = 0;
    status = searchscm(connect, manifestTable, &srch, NULL, handleStaleMan,
                       SCM_SRCH_DOVALUE_ALWAYS, NULL);
    if (status != 0 && status != ERR_SCM_NODATA)
    {
        fprintf(stderr, "Error searching for manifests: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < numStaleManFiles; i++)
    {
        /** @bug ignores error code without explanation */
        handleFreshMan2(connect, certTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleFreshMan2(connect, crlTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleFreshMan2(connect, gbrTable, staleManFiles[i]);
        /** @bug ignores error code without explanation */
        handleFreshMan2(connect, roaTable, staleManFiles[i]);
        free(staleManFiles[i]);
    }
    free(srch1[0].valptr);

    // check all certs in state unknown to see if now crl with issuer=issuer
    // and aki=ski and nextUpdate after currTime;
    // if so, set state !unknown
    srch.nused = 0;
    srch.vald = 0;
    msg[0] = 0;
    addFlagTest(msg, SCM_FLAG_STALECRL, 1, 0);
    srch.wherestr = msg;
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "issuer", SQL_C_CHAR, 512);
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "aki", SQL_C_CHAR, 128);
    /** @bug ignores error code without explanation */
    addcolsrchscm(&srch, "local_id", SQL_C_ULONG, 8);
    countHandler = handleIfCurrent;
    status = searchscm(connect, certTable, &srch, NULL, countCurrentCRLs,
                       SCM_SRCH_DOVALUE_ALWAYS, NULL);
    free(srch1[0].valptr);
    free(srch1[1].valptr);
    free(srch1[2].valptr);
    if (status != 0 && status != ERR_SCM_NODATA)
    {
        fprintf(stderr, "Error searching for certificates: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }

    // write timestamp into database
    xsnprintf(msg, WHERESTR_SIZE, "update %s set gc_last=\"%s\";",
              metaTable->tabname, currTimestamp);
    status = statementscm_no_data(connect, msg);
    if (status != 0)
    {
        fprintf(stderr, "Error updating timestamp: %s\n",
                err2string(status));
        exit(EXIT_FAILURE);
    }

    config_unload();
    CLOSE_LOG();
    return 0;
}
