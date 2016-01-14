#include "main.h"

#include "util/logging.h"
#include "config/config.h"

extern void usage(
    const char *);

struct name_list {
    char *namep;
    int state;
    struct name_list *nextp;
};

/*
 * static void free_name_list(struct name_list *rootlistp) { struct name_list
 * *currp, *nextp; for (currp = rootlistp; currp; currp = nextp) {
 * free(currp->namep); nextp = currp->nextp; if (currp > rootlistp)
 * free(currp); } }
 */

static char *makeCDStr(
    unsigned int *retlenp,
    char *dir)
{
    char *buf;
    char *ret;

    *retlenp = 0;
    buf = (char *)calloc(PATH_MAX + 6, sizeof(char));
    if (buf == NULL)
        return (NULL);
    if (dir == NULL)
    {
        ret = getcwd(buf + 2, PATH_MAX + 1);
        if (ret == NULL)
        {
            free((void *)buf);
            return (NULL);
        }
    }
    else
    {
        strncpy(buf + 2, dir, PATH_MAX + 1);
    }
    buf[0] = 'C';
    buf[1] = ' ';
    (void)strncat(buf, "\r\n", 2);
    *retlenp = strlen(buf);
    return (buf);
}


int main(
    int argc,
    char *argv[])
{

    /*
     * tflag = tcp, uflag = udp, nflag = do nothing, just print what you would
     * have done, {w,e,i}flag = {warning,error, information} flags for what
     * will be sent, ch is for getopt
     */

    int tflag,
        uflag,
        nflag,
        fflag,
        sflag,
        ch;
    int portno;
    unsigned int retlen;
    FILE *fp;
    char *sendStr;
    char *topDir = NULL;
    struct write_port wport;
    char flags;                 /* our warning flags bit fields */
    char **my_argv;             /* either real or from script file */
    int my_argc;                /* either real or from script file */
    const char *WHITESPACE = "\n\r\t ";
    char *inputLogFile = NULL;

    tflag = uflag = nflag = fflag = sflag = ch = 0;
    portno = retlen = 0;
    flags = 0;

    memset((char *)&wport, '\0', sizeof(struct write_port));

    OPEN_LOG("rsync_aur", LOG_DAEMON);

    if (!my_config_load())
    {
        LOG(LOG_ERR, "can't load configuration");
        exit(EXIT_FAILURE);
    }

    if (argc == 2 && *argv[1] != '-')   // process a script file as command
                                        // line
    {
        char *buf = NULL;
        char **expanded_argv = NULL;
        int fd,
            bufsize,
            i;

        /*
         * Read file into buffer and parse as if it were a long command line.
         */
        if ((fd = open(argv[1], O_RDONLY)) < 0 ||
            (bufsize = lseek(fd, 0, SEEK_END)) <= 0 ||
            (buf = (char *)calloc(1, bufsize + 6)) == 0 ||
            lseek(fd, 0, SEEK_SET) != 0 ||
            read(fd, buf, bufsize + 4) != bufsize ||
            split_string(buf, WHITESPACE, &my_argv, &my_argc) != 0)
        {
            fprintf(stderr, "failed to open/parse %s\n", argv[1]);
            exit(EXIT_FAILURE);
        }
        /*
         * Prepend executable name to my_argv and increment my_argc
         */
        expanded_argv =
            (char **)realloc(my_argv, sizeof(char *) * (my_argc + 1));
        if (!expanded_argv)
        {
            fprintf(stderr, "out of memory\n");
            exit(EXIT_FAILURE);
        }
        my_argv = expanded_argv;
        my_argc++;
        for (i = argc; i > 0; i--)      /* shift right by one position */
            my_argv[i] = my_argv[i - 1];
        my_argv[0] = argv[0];

        /*
         * Intentionally leak buf & my_argv: they've become the "command
         * line".
         */
    }
    else if (argc > 2 && *argv[1] != '-')       // more than one script file?
    {
        fprintf(stderr, "Too many script files: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }
    else                        // normal command line
    {
        my_argv = argv;
        my_argc = argc;
    }

    while ((ch = getopt(my_argc, my_argv, "tuf:d:nweish")) != -1)
    {
        switch (ch)
        {
        case 't':              /* TCP flag */
            tflag = 1;
            portno = CONFIG_RPKI_PORT_get();
            break;
        case 'u':              /* UDP flag */
            uflag = 1;
            portno = CONFIG_RPKI_PORT_get();
            break;
        case 'n':              /* do nothing flag - print what messages would
                                 * have been sent */
            nflag = 1;
            break;
        case 'w':              /* create warning message(s) */
            flags = flags | WARNING_FLAG;
            break;
        case 'e':              /* create error message(s) */
            flags = flags | ERROR_FLAG;
            break;
        case 'i':              /* create information message(s) */
            flags = flags | INFO_FLAG;
            break;
        case 'f':              /* input rsync log file to read and parse */
            fflag = 1;
            inputLogFile = strdup(optarg);
            break;
        case 'd':
            topDir = strdup(optarg);
            break;
        case 's':              /* synchronize with rcli */
            sflag = 1;
            break;
        case 'h':              /* help */
            myusage(argv[0]);
            exit(EXIT_SUCCESS);
            break;
        default:
            myusage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    /*
     * test for necessary flags
     */
    if (!fflag)
    {
        fprintf(stderr,
                "please specify rsync logfile with -f. Or -h for help\n");
        exit(EXIT_FAILURE);
    }

    /*
     * test for conflicting flags here...
     */
    if (tflag && uflag)
    {
        fprintf(stderr,
                "choose either tcp or udp, not both. or -h for help\n");
        exit(EXIT_FAILURE);
    }

    if (!tflag && !uflag && !nflag)
    {                           /* if nflag then we don't care */
        fprintf(stderr,
                "must choose tcp or udp, or specify -n. -h for help\n");
        exit(EXIT_FAILURE);
    }

    /*
     * open input rsync log file...
     */
    fp = fopen(inputLogFile, "r");
    if (!fp)
    {
        LOG(LOG_ERR, "failed to open %s", inputLogFile);
        exit(EXIT_FAILURE);
    }
    LOG(LOG_INFO, "Opened rsync log file: %s", inputLogFile);
    FLUSH_LOG();
    free(inputLogFile);
    inputLogFile = NULL;

    /*
     * setup sockets...
     */
    if (!nflag)
    {
        if (tflag)
        {
            if (tcpsocket(&wport, portno) != TRUE)
            {
                LOG(LOG_ERR, "tcpsocket failed...");
                exit(EXIT_FAILURE);
            }
            LOG(LOG_INFO, "Established connection to port %d", portno);
        }
        else if (uflag)
        {
            if (udpsocket(&wport, portno) != TRUE)
            {
                LOG(LOG_ERR, "udpsocket failed...");
                exit(EXIT_FAILURE);
            }
        }
    }
    else
    {
        wport.out_desc = STDOUT_FILENO;
        wport.protocol = LOCAL;
    }

    /*
     * set the global pointer to the wport struct here - don't know if this
     * will cause a fault or not. Can't remember. Doing this to be able to
     * communicate with the server through the descriptor after a sigint or
     * other signal has been caught.
     */
    global_wport = &wport;

    if (setup_sig_catchers() != TRUE)
    {
        LOG(LOG_ERR, "failed to setup signal catchers... bailing.");
        exit(EXIT_FAILURE);
    }

  /****************************************************/
    /*
     * Make the Start String
     */
    /*
     * send the Start String
     */
    /*
     * free it
     */
  /****************************************************/
    sendStr = makeStartStr(&retlen);
    if (!sendStr)
    {
        LOG(LOG_ERR, "failed to make Start String... bailing...");
        exit(EXIT_FAILURE);
    }

    outputMsg(&wport, sendStr, retlen);
    retlen = 0;
    free(sendStr);

  /****************************************************/
    /*
     * Make the Directory String
     */
    /*
     * send the Directory String
     */
    /*
     * free it
     */
  /****************************************************/
    sendStr = makeCDStr(&retlen, topDir);
    if (!sendStr)
    {
        LOG(LOG_ERR, "failed to make Directory String... bailing...");
        exit(EXIT_FAILURE);
    }

    outputMsg(&wport, sendStr, retlen);
    retlen = 0;
    free(sendStr);

  /****************************************************/
    /*
     * do the main parsing and sending of the file loop
     */
  /****************************************************/

    /*
     * Process entire log file, one directory block at a time.
     */
    while (1)
    {
        const char DELIMS[] = " \r\n\t";
        long this_dirblock_pos;
        long next_dirblock_pos;
        const int NORMAL_PASS = 0;
        const int MANIFEST_PASS = 1;
        const int NUM_PASSES = 2;
        int pass_num;

        /*
         * Find the end of the directory block (actually, where the next one
         * begins).
         */
        this_dirblock_pos = ftell(fp);
        next_dirblock_pos = next_dirblock(fp);
        if (next_dirblock_pos < 0)
        {
            LOG(LOG_ERR,
                    "Error while trying to find a block of directories.");
            break;
        }
        if (this_dirblock_pos == next_dirblock_pos)     /* end of file */
            break;

        /*
         * Do two passes: first for non-manifests, second for manifests.
         */
        for (pass_num = 0; pass_num < NUM_PASSES; pass_num++)
        {
            fseek(fp, this_dirblock_pos, SEEK_SET);
            while (ftell(fp) < next_dirblock_pos)
            {                   /* per available line */
                char line[PATH_MAX + 40];
                char fullpath[PATH_MAX];
                char *fullpath_start;

                /*
                 * Get next line.
                 */
                if (!fgets(line, PATH_MAX + 40, fp))
                    break;      /* Stop searching; it's the end of file. */

                if (!exists_non_delimiter(line, DELIMS))
                    continue;   /* Skip blank lines. */

                /*
                 * Get second field.
                 */
                fullpath_start = start_of_next_field(line, DELIMS);
                if (!fullpath_start)
                {
                    LOG(LOG_ERR, "Malformed rsync log file line: %s",
                            line);
                    break;
                }
                if (!this_field(fullpath, PATH_MAX, fullpath_start, DELIMS))
                {
                    LOG(LOG_ERR, "Insufficient buffer to hold path: %s",
                            fullpath_start);
                    break;
                }

                /*
                 * Create/send socket message.
                 */
                retlen = 0;
                if (!
                    (sendStr =
                     getMessageFromString(line, (unsigned int)strlen(line),
                                          &retlen, flags)))
                {
                    LOG(LOG_DEBUG, "Ignoring: %s", line);
                    continue;
                }
                if (pass_num == NORMAL_PASS && !is_manifest(fullpath))
                {
                    outputMsg(&wport, sendStr, retlen);
                }
                else if (pass_num == MANIFEST_PASS && is_manifest(fullpath))
                {
                    outputMsg(&wport, sendStr, retlen);
                }
                free(sendStr);
            }                   /* per available line */
        }                       /* two passes */
    }                           /* Process entire logfile, one directory block
                                 * at a time. */

    free(topDir);

    char c;
    if (sflag)
    {
        outputMsg(&wport, "Y \r\n", 4);
        if (recv(wport.out_desc, &c, 1, MSG_WAITALL) != 1
            || (c != 'Y' && c != 'y'))
        {
            LOG(LOG_ERR, "failed to synchronize with rcli, bailing");
            exit(EXIT_FAILURE);
        }
    }

  /****************************************************/
    /*
     * Make the End String
     */
    /*
     * send the End String
     */
    /*
     * free it
     */
  /****************************************************/
    sendStr = makeEndStr(&retlen);
    if (!sendStr)
    {
        LOG(LOG_ERR, "failed to make End String... bailing...");
        exit(EXIT_FAILURE);
    }
    outputMsg(&wport, sendStr, retlen);
    free(sendStr);

    /*
     * close descriptors etc.
     */
    if (wport.protocol != LOCAL)
    {
        close(wport.out_desc);
        LOG(LOG_DEBUG, "closed the descriptor %d", wport.out_desc);
    }

    config_unload();

    CLOSE_LOG();

    return (0);
}
