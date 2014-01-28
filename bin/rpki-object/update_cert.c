/*
 * $Id: update_cert.c 453 2008-05-28 15:30:40Z cgardiner $ 
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "util/cryptlib_compat.h"
#include "rpki-asn1/certificate.h"
#include <rpki-asn1/roa.h>
#include <rpki-asn1/keyfile.h>
#include <casn/casn.h>
#include <casn/asn.h>
#include <time.h>
#include <rpki-object/signature.h>
#include <util/logging.h>

#define MSG_OPEN "Couldn't open %s"
#define MSG_USAGE "Usage: startdelta, enddelta, certfile(s)"
#define MSG_IN "Error in %s"
#define MSG_ADJUST_TIME "Error adjusting time in %s by %s"

int main(
    int argc,
    char **argv)
{
    OPEN_LOG("update_cert", LOG_USER);
    struct Certificate cert;
    Certificate(&cert, (ushort) 0);
    if (argc < 4)
        FATAL(MSG_USAGE);
    int i;
    for (i = 3; i < argc; i++)
    {
        if (get_casn_file(&cert.self, argv[i], 0) < 0)
            FATAL(MSG_OPEN, argv[1]);
        struct CertificateToBeSigned *ctftbsp = &cert.toBeSigned;

        long now = time((time_t *) 0);
        clear_casn(&ctftbsp->validity.notBefore.self);
        clear_casn(&ctftbsp->validity.notAfter.self);
        if (adjustTime(&ctftbsp->validity.notBefore.utcTime, now, argv[1]) < 0)
            FATAL(MSG_ADJUST_TIME, "notBefore", argv[1]);
        if (adjustTime(&ctftbsp->validity.notAfter.utcTime, now, argv[2]) < 0)
            FATAL(MSG_ADJUST_TIME, "notAfter", argv[2]);
        char *issuerkeyfile = (char *)calloc(1, strlen(argv[i]) + 8);
        strcpy(issuerkeyfile, argv[i]);
        char *a = strchr(issuerkeyfile, (int)'.');
        strcpy(&a[-1], ".p15");
        if (!set_signature(&cert.toBeSigned.self, &cert.signature,
                           issuerkeyfile, "label", "password", false))
        {
            FATAL(MSG_IN, "set_signature");
        }
        put_casn_file(&cert.self, argv[i], 0);
        fprintf(stderr, "Finished %s\n", argv[i]);
    }
    fprintf(stderr, "Finished all OK\n");
    return 0;
}
