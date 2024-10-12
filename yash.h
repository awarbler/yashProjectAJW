// this is the header file

// include a guard to prevent mutiple inclusions of the header file

#ifndef YASHD_H // If yashd_h is not defined
#define YASHD_H // define yashd_h

// including standard libraries for various functionalitites for the client
#include <stdio.h> // standard input output library 
#include <stdlib.h> // standard library includes functions malloc() free() 
#include <pthread.h> // pthread header
#include <netinet/in.h> // for sockets /* inet_addr() */
#include <arpa/inet.h>      // internet operations inetpton()
#include <unistd.h>         // posix api for close () 
#include <signal.h>         // signal handling sigint sigtstp 
#include <errno.h>          // error handling macros

// server headers
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

// function prototype for handle_client
// this function is responsible for handling communication with a connected client in a seperate thread 
// this protoype declares teh function so that it can be used in mutiple source files where the header is included
// this fullfills the requirement of using a mult threaded server to handle multiple clients concurrently. 
void *handle_client(void *arg);

#endif // end of the include. 
