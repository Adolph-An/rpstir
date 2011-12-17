
/*
  $Id: query.c 857 2009-09-30 15:27:40Z dmontana $
*/

/*************************
 * The code for setting up socket connections between the server
 *  and the clients
 *************************/

#include <unistd.h>    // include close function for sockets

/******
 * Get a server side listening socket, can call getServerSocket on it
 *    repeatedly to get clients
 * Returns -1 if error
 ******/
int getListenerSocket(void);

/******
 * Get a connection to a client via a listening socket
 * Returns -1 if error
 ******/
int getServerSocket(int listenSock);

/******
 * Get a client side socket
 * Argument hostname: host name of the server
 * Returns -1 if error
 ******/
int getClientSocket(char *hostName);
