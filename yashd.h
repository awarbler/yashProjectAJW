// this is the header file

// include a guard to prevent mutiple inclusions of the header file

#ifndef YASHD_H // If yashd_h is not defined
#define YASHD_H // define yashd_h

#define MAX_INPUT 200
#define MAX_ARGS 100
#define MAX_JOBS 20
#define MAX_CLIENTS 10

// both 

// including standard libraries for various functionalitites for the client
#include <stdio.h>          // standard input output library 
#include <stdlib.h>         // standard library includes functions malloc() free() 
#include <string.h>         // 
#include <pthread.h>        // pthread header
#include <netinet/in.h>     // for sockets /* inet_addr() */
#include <arpa/inet.h>      // internet operations inetpton()
#include <unistd.h>         // posix api for close () 
#include <signal.h>         // signal handling sigint sigtstp 
#include <errno.h>          // error handling macros

#include <sys/types.h>      //  Data types used in system calls
#include <sys/stat.h>       // File status macros 
#include <sys/wait.h>       // Wait for process termination
#include <sys/file.h>       // file control operations 
#include <sys/socket.h>     //  socket definitions
#include <fcntl.h>          // file control operations open 
#include <netdb.h>          //  definitions for network database opeations 
#include <ctype.h>          // Character handling functions
#include <time.h>           // Time functions

// Structure for client arguments to be passed to threads
typedef struct {
    int psd;                // Client socket descriptor
    struct sockaddr_in from;// Client address information 
} ClientArgs;

// Shared function Prototypes 


void *handle_client(void *arg); //  Function to set up signal handlers from yashTested
void handle_command(int psd, char *cmd); // Function to handle command execution 
void handle_controll(int psd, char control); // Function to handle control signal

// logging related function  
void *logRequest(void *args);

// Job Control and Piping 
void handle_pipe(char **cmd_args_left, char **cmd_args_right); //function to handle piping commands
void handle_redirection(char **cmd_args);

#endif // end of the include. 
