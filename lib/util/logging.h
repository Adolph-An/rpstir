#ifndef _UTILS_LOGGING_H
#define _UTILS_LOGGING_H

#include <syslog.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "gettext_include.h"

/* LOG_LEVEL_TEXT[0] == "EMERG", etc. (RFC 5424) */
extern const char *LOG_LEVEL_TEXT[];

#define OPEN_LOG(ident, facility) \
    do { \
        openlog(PACKAGE_NAME "-" ident, LOG_PID | LOG_PERROR, (facility)); \
    } while (false)

#define CLOSE_LOG() \
    do { \
        closelog(); \
    } while (false)

/**
    Currently a no-op because syslog() doesn't need flushing, but could be used
    later if we switch away from syslog.
*/
#define FLUSH_LOG() \
    do { \
    } while (false)

/** Don't read or write this directly, use SET_LOG_LEVEL() below. */
volatile sig_atomic_t LOG_LEVEL;

#define SET_LOG_LEVEL(level) \
    do { \
        LOG_LEVEL = (level); \
    } while (false)

#define LOG(priority, format, ...) \
    do { \
        if ((priority) <= LOG_LEVEL) \
        { \
            if (LOG_LEVEL >= LOG_DEBUG) \
            { \
	      syslog((priority), _("%s: %s:%d in %s(): " format),	\
                       LOG_LEVEL_TEXT[priority],                        \
		     __FILE__, __LINE__, __func__, ## __VA_ARGS__);	\
            } \
            else \
            { \
                syslog((priority), "%s: " format,                 \
                       LOG_LEVEL_TEXT[priority], ## __VA_ARGS__); \
            } \
        } \
    } while (false)


#define ERROR_BUF_SIZE 256
#define ERR_LOG(err, errorbuf, format, ...) \
    do { \
        if (strerror_r((err), (errorbuf), ERROR_BUF_SIZE) == 0) \
        { \
            LOG(LOG_ERR, format ": %s", ## __VA_ARGS__, (errorbuf)); \
        } \
        else \
        { \
            LOG(LOG_ERR, format ": error code %d", ## __VA_ARGS__, (err)); \
        } \
    } while (false)

/**
    These macros help transition from the problematic fatal() functions used in
    some of the older code. They are not intended to be used by new code.
*/
#define FATAL(format, ...) \
    do { \
        fprintf(stderr, format "\n", ## __VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (false)
#define DONE(format, ...) \
    do { \
        fprintf(stderr, format "\n", ## __VA_ARGS__); \
        exit(EXIT_SUCCESS); \
    } while (false)


#endif
