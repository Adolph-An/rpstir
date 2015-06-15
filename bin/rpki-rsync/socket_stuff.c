#include "main.h"


/**************************************************
 * int tcpsocket(struct write_port *, int portno) *
 *                                                *
 * returns TRUE if write_port structure is        *
 * succesfully setup. False if not.               *
 *                                                *
 * does all of the standard stuff to setup an     *
 * AF_INET, SOCK_STREAM socket, and fills in the  *
 * sockaddr_in structure with the local host from *
 * gethostbyname. This latter part is left in     *
 * so we can latter modify to specify remote host *
 * connections.                                   *
 *                                                *
 * Once everything is setup, it's copied into the *
 * write_port structure and the connect() is      *
 * made using the write_port descriptor.          *
 **************************************************/

/*
 * $Id$ 
 */

int tcpsocket(
    struct write_port *wport,
    int portno)
{
    /*
     * set the wport file descriptor to the socket we've created 
     */
    wport->out_desc = socket(AF_INET, SOCK_STREAM, 0);
    if ((wport->out_desc) < 0)
    {
        perror("failed to create socket");
        return (FALSE);
    }

    memset(&(wport->server_addr), '\0', sizeof(struct sockaddr_in));
    wport->server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wport->server_addr.sin_family = AF_INET;
    wport->server_addr.sin_port = htons(portno);

    /*
     * set the protocol for this structure. This will be used in our generic
     * write routine to determine if we need {write,send}, or sendto 
     */
    wport->protocol = TCP;

    if (connect
        (wport->out_desc, (const struct sockaddr *)&(wport->server_addr),
         sizeof(wport->server_addr)) < 0)
    {
        perror("failed on connect()");
        return (FALSE);
    }

    return (TRUE);
}

/**************************************************
 * int udpsocket(struct write_port *, int portno) *
 *                                                *
 * returns TRUE if write_port structure is        *
 * succesfully setup. False if not.               *
 *                                                *
 * does all of the standard stuff to setup an     *
 * AF_INET, SOCK_DGRAM  socket, and fills in the  *
 * sockaddr_in structure with "localhost" from    *
 * gethostbyname. This latter part is left in     *
 * so we can latter modify to specify remote host *
 * connections.                                   *
 *                                                *
 * Once everything is setup, it's copied into the *
 * write_port structure. We have a copy of the of *
 * the sockaddr_in with the server we want to     *
 * send messages to so sendto() can be conveyed   *
 * the correct data.                              *
 **************************************************/
int udpsocket(
    struct write_port *wport,
    int portno)
{

    /*
     * set the file wport desc to the socket we created 
     */
    wport->out_desc = socket(AF_INET, SOCK_DGRAM, 0);
    if ((wport->out_desc) < 0)
    {
        perror("failed to create socket");
        return (FALSE);
    }

    memset(&(wport->server_addr), '\0', sizeof(struct sockaddr_in));
    wport->server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    wport->server_addr.sin_family = AF_INET;
    wport->server_addr.sin_port = htons(portno);

    /*
     * set the to_length that will be used in sendto() calls 
     */
    wport->to_length = sizeof(struct sockaddr_in);

    /*
     * set the protocol so we know to do sendto rather than {write,send} 
     */
    wport->protocol = UDP;

    return (TRUE);
}

ssize_t outputMsg(
    struct write_port *wport,
    char *str,
    unsigned int len)
{
    ssize_t ret = -1;
    char *str_copy = NULL;

    /*
     * Log a copy of str without newline(s) 
     */
    str_copy = strdup(str);
    if (str_copy)
    {
        rstrip(str_copy, "\r\n");
        LOG(LOG_INFO, "Sending %s", str_copy);
        FLUSH_LOG();
        free(str_copy);
    }

    if (wport->protocol == LOCAL)
    {
        ret = write(wport->out_desc, (const void *)str, len);
        if (ret < 0)
            perror("write()");
        else if (ret != (ssize_t)len)
            LOG(LOG_ERR, "Wrote %zd bytes instead of %u", ret, len);
        return (ret);
    }
    else if (wport->protocol == TCP)
    {
        /*
         * send it 
         */
        ret = write(wport->out_desc, (const void *)str, len);
        if (ret < 0)
            perror("write()");
        else if (ret != (ssize_t)len)
            LOG(LOG_ERR, "Wrote %zd bytes instead of %u", ret, len);
        return (ret);
    }
    else if (wport->protocol == UDP)
    {
        /*
         * send it 
         */
        ret = sendto(wport->out_desc, (const void *)str, len, 0,
                     (struct sockaddr *)&(wport->server_addr),
                     wport->to_length);
        if (ret < 0)
            perror("sendto()");
        else if (ret != (ssize_t)len)
            LOG(LOG_ERR, "Sent %zd bytes instead of %u", ret, len);
    }
    else
    {
        LOG(LOG_ERR, "unknown protocol specification: %d",
                wport->protocol);
    }
    return (ret);
}
