// This is where the server will be set up 
// Troubleshooting the server
// 1. Error binding name to stream socket: Address already in use 
// Run: lsof -i :3820
// Run: kill -9 <PID>

// basic functionality for connecting to the server

#include "yashd.h"


// shared job control and pipe handling functionality from yashTestd
extern void handle_pipes(char **cmd_args_left, char **cmd_args_mand); // function to handle piping command
extern void handle_redirection(char **cmd_args); // function to handle file redirection commands
extern void handle_job_control(char *command); // function to handle job control (fg bg)
extern void setup_signal_handler(); // function to set up signal handlers 
extern void sigint_handler(int signo); // signal handler for sigint_handler
extern void sigtstp_handler(int signo); // signal handler for sigint_handler
extern void sigchld_handler(int signo); // signal handler for sigint_handler


// function prototypes
void reusePort(int sock);
void* serveClient(void *args);
void sig_pipe(int n);
void sig_chld(int n);
void daemon_init(const char * const path, uint mask);
void *logRequest(void *args);
void cleanup(char *buf);
void reusePort(int sock);
void handle_command(int psd, char *command);
void handle_control_signal(int psd, char signal);
int handle_pipe(char **cmd_left, char **cmd_right);
void add_job(pid_t, char *command, int is_running);
void print_jobs();
void remove_newLine(char *line);
void fg_job(int job_id);
void bg_job(int job_id);
void apply_redirections(char **cmd_args);
void run_in_background(char **cmd_args);



/* Initialize path variables */
static char* server_path = "./tmp/yashd";
static char* log_path = "./tmp/yashd.log";
static char* pid_path = "./tmp/yashd.pid";

int BUFFER_SIZE = 1024;
pid_t pid;
int pipefd_stdin[2];

/* Initialize mutex lock */
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


/* create thread argument struct for logRequest() thread */
typedef struct LogRequestArgs {
  char request[200];
  struct sockaddr_in from;
} LogRequestArgs;

/* create thread argument struct for client thread */
typedef struct {
    int psd;
    struct sockaddr_in from;
} ClientArgs;

/**
 * @brief  If we are waiting reading from a pipe and
 *  the interlocutor dies abruptly (say because
 *  of ^C or kill -9), then we receive a SIGPIPE
 *  signal. Here we handle that.
 */
void sig_pipe(int n) 
{
   perror("Broken pipe signal");
}

void sig_chld(int n)
{
  int status;
  fprintf(stderr, "Child terminated\n");
  wait(&status); /* So no zombies */
}

/**
 * @brief Initializes the current program as a daemon, by changing working 
 *  directory, umask, and eliminating control terminal,
 *  setting signal handlers, saving pid, making sure that only
 *  one daemon is running. Modified from R.Stevens.
 * @param[in] path is where the daemon eventually operates
 * @param[in] mask is the umask typically set to 0
 */
void daemon_init(const char * const path, uint mask)
{
  pid_t pid;
  char buff[256];
  int fd;
  int k;

  /* put server in background (with init as parent) */
  if ( ( pid = fork() ) < 0 ) {
    perror("daemon_init: Fork Failed");
    exit(1);
        // this changed from zero to one in my code to accuratley relfect the occurrence 
    // of an error when a fork fails. and 1 communicates that the os and 
    // any calling porcesses that the program did not complete.
  } 
  if (pid > 0) /* The parent */
    exit(0); 

  /* the child */

  /* Close all file descriptors that are open */
  for (k = getdtablesize()-1; k>0; k--)
      close(k);

  /* Redirecting stdin, stdout, and stdout to /dev/null */
  if ((fd = open(" /dev/null", O_RDWR)) < 0) {
    perror("rror opening Open /dev/null");
    exit(1);
  }

  dup2(fd, STDIN_FILENO);      /* detach stdin */
  dup2(fd, STDOUT_FILENO);     /* detach stdout */
  dup2(fd, STDERR_FILENO);     /* detach stderr */
  close (fd);
  /* From this point on printf and scanf have no effect */

  /* Establish handlers for signals */
  if (signal(SIGCHLD, sig_chld) < 0 ) {
    perror("Error setting SIGCHLD handler");
    exit(1);
  }
  if (signal(SIGPIPE, sig_pipe) < 0 ) {
    perror("Error setting SIGPIPE handler pipe");
    exit(1);
  }

  /* Detach controlling terminal by becoming sesion leader */
  setsid();
  /* Change directory to specified directory */
  chdir(path); 
  /* Set umask to mask (usually 0) */
  umask(mask); 

  /* Put self in a new process group */
  pid = getpid();
  setpgrp(); /* GPI: modified for linux */

  /* Make sure only one server is running */
  if ((k = open(pid_path, O_RDWR | O_CREAT, 0666) ) < 0 ) {
    perror("ERror opening PID file");
    exit(1);

  }
    
  if (lockf(k, F_TLOCK, 0) != 0) {
    perror("Another instance is alreadying running ");
    exit(0);
  }
    

  /* Save server's pid without closing file (so lock remains)*/
  pid = getpid();
  sprintf(buff, "%6d", pid);
  write(k, buff, strlen(buff));

  //return;
}

int main(int argc, char **argv ) {
    int   sd, psd; // socket descriptors for server client 
    struct   sockaddr_in server, from; // structures to hold server and client address info // length of client address structure 
    struct  hostent *hp;
    //socklen_t client_len = sizeof(server); 
    //pthread_t threads[MAX_CLIENTS]; // array to hold thread ids for the client handlers
    int client_count = 0; // counter for connected clients 
    socklen_t fromlen; //  basically this is the size of the client address info 
    //struct sockaddr_in from;
    socklen_t length;


    // set up the signal handler from yashtestd to manage signals (cntrl c z )
    setup_signal_handler();

    // TO-DO: Initialize the daemon
    // daemon_init(server_path, 0);
    
    hp = gethostbyname("localhost");
    bcopy( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("(TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    /** Construct name of socket */
    memset(&server, 0, sizeof(server)); //  clears server address structure
    server.sin_family = AF_INET;  // set address family to ipv4   
    server.sin_addr.s_addr = htonl(INADDR_ANY); //  set address family
    server.sin_port = htons(3820);  // set port number convert to network byte order 
    

    /** Create socket on which to send and receive */
    sd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP); 
    if (sd < 0) { //  check if socket create failed 
	    perror("Opening stream socket"); //  print an error message if socket creation fails
	    exit(-1);//  exit the program with a -1
    }
    
 
    /** this allow the server to re-start quickly instead of waiting
	  for TIME_WAIT which can be as large as 2 minutes */
    reusePort(sd);

    // bind the socket to the port 
    if (bind(sd, (struct sockaddr *) &server, sizeof(server) ) < 0 ) { // bind socket 
      close(sd); // close the socket if bind fails 
      perror("Error binding name to stream socket");
      exit(-1); // exit the proram with failure status 
    }
        
    // Listen for incoming connections
    /** accept TCP connections from clients and thread a process to serve each request */
    //listen(sd,4); // changed to max clients 
    if (listen(sd, MAX_CLIENTS) < 0){
      // set socke to listen for incoming connections 
      perror("listen failed "); // print error msg if listen fails
      close(sd); // close the socket if listen fails
      exit(EXIT_FAILURE); //  exit the program with failure status 
    }
    
    /** get port information and prints it out */
    length = sizeof(server);
    if ( getsockname (sd, (struct sockaddr *)&server,&length) ) {
      perror("Error getting socket name");
      exit(0);
    }
    printf("Server Port is: %d\n", ntohs(server.sin_port));


    fromlen = sizeof(from); //  the client_len

    printf("Server is ready to receive\n");

    // Accepting incoming connections in an infinite loop
    // for(;;){ //  changed from a for loop to a while loop 
    while ((psd  = accept(sd, (struct sockaddr *)&from, &fromlen)) >= 0) { 
      //  psd client socket &from is the client address 

      if (client_count >= MAX_CLIENTS) {
        // check if max number of clients have been reached
        printf("Max clients reached, refuing connection\n"); // print msg indiciatin mayx clients reached
        close(psd); // close the client socket 
        continue;
      }
      if (psd < 0) {
        perror("accepting connection");
        continue;
      }

      // Allocate memory for ClientArgs
      ClientArgs *clientArgs = malloc(sizeof(ClientArgs));
      if (clientArgs == NULL) {
          perror("Error allocating memory for client arguments");
          close(psd);
          continue;
      }
      printf("Memory allocated for client arguments\n");

      // Set the client arguments
      clientArgs->psd = psd;
      clientArgs->from = from;

      // Create a new thread to serve the client
      pthread_t thread; // declare a thread id

      // create a thread for client 
      if (pthread_create(&thread, NULL, serveClient, (void *)clientArgs) != 0) {
        perror("Error creating client thread"); // print an error message 
        free(clientArgs); // free allocated memory if therad creation fails
        close(psd); // close the client socket 
        continue;
      }
      printf("Thread created to serve client\n");

      pthread_detach(thread); // Detach the thread to unblock

    } 
    // Destroy the mutex
    pthread_mutex_destroy(&lock);
    //  Close the server socket when done
    close(sd);
    return 0; // return success
}

void *logRequest(void *args) {
    LogRequestArgs *logArgs = (LogRequestArgs *)args;
    char *request = logArgs->request;
    struct sockaddr_in from = logArgs->from;

    time_t now;
    struct tm *timeinfo;
    char timeString[80];
    FILE *logFile;

    time(&now);
    timeinfo = localtime(&now);
    strftime(timeString, sizeof(timeString), "%b %d %H:%M:%S", timeinfo);

    pthread_mutex_lock(&lock); // wrap CS in lock ...
    
    printf("Opening log file at path: %s\n", log_path);
    
    logFile = fopen(log_path, "a");
    if (logFile == NULL) {
        perror("Error opening log file");
        pthread_mutex_unlock(&lock); // Unlock the mutex before returning
        return NULL;
    }

    // Debugging statement to check if writing to the file
    printf("Writing to log file: %s\n", request);


    fprintf(logFile,"%s yashd[%s:%d]: %s\n", timeString, inet_ntoa(from.sin_addr), ntohs(from.sin_port), request);
    fclose(logFile);

    pthread_mutex_unlock(&lock); // ... unlock

    pthread_exit(NULL);
 }

void cleanup(char *buf)
{
    //int i;
    //for(i=0; i<BUFFER_SIZE; i++) buf[i]='\0';
    memset(buf, 0, BUFFER_SIZE);
}

void* serveClient(void *args) {
    ClientArgs *clientArgs = (ClientArgs *)args; //  cast argument to clientArgs struct
    int psd = clientArgs->psd; // Get the clients socket descriptor 
    struct sockaddr_in from = clientArgs->from;
    char buffer[BUFFER_SIZE]; // buffer to store received data 
    memset(buffer, 0, BUFFER_SIZE);
    int bytesRead;
    int pipefd_stdout[2], pipefd_stdin[2];
    pthread_t p;
    pid_t pid;
    int commandRunning = 0;
    char *command;

    // Send initial prompt to the client
    if (send(psd, "\n# ", sizeof("\n# "), 0) < 0) {
      perror("sending stream message");
    }
        
    /**  get data from  client and send it back */
    // receive command from the client 
    while (( bytesRead = recv(psd, buffer, sizeof(buffer)-1, 0)) > 0) {
      
      // infinite loop to keep the server running
      cleanup(buffer);
      buffer[bytesRead] = '\0';// null terminate the buffer to make it a valid string

      // Handle: CMD<blank><Command_String>\n
      if (strncmp(buffer, "CMD ", 4) == 0) { 
        // check if the message is a command 
        char *command = buffer + 4; // extract the command string skip cmd
        command[strcspn(command, "\n")] = '\0'; // Remove the newline character

        // Allocate memory for LogRequestArgs
        LogRequestArgs *logArgs = malloc(sizeof(LogRequestArgs));
        if (logArgs == NULL) { // check if memory allocation failed 
          perror("Error allocating memory for log request"); //  print error message 
          continue; // continue to the next iteration to accept new commands
        }

        // Copy the request and client information to the struc
        strncpy(logArgs->request, command, sizeof(logArgs->request) - 1); // copy the command to the log request 
        
        logArgs->from = from; // set the client address

        // Create a new thread for logging
        if (pthread_create(&p, NULL, logRequest, (void *)logArgs) != 0) { // create a new thread for logging
          perror("Error creating log request thread"); //print error
          free(logArgs); // Free the allocated memory if thread creation fails
        } else {
          pthread_detach(p); // Detach the thread to allow it to run independently
          }
        
        handle_command(psd, command); // handle the command
     
      } else  if (strncmp(buffer, "CTL ", 4) == 0) { 
        // checking to see if the message is a control signal
        handle_control(psd, buffer[4]); // handle the control signal contrl c
          
      } 
      else { ///  handles the plain text 
        write(psd, buffer, bytesRead); 

      }

      if (bytesRead <= 0) {
        // read data from the client
        printf("Closed connection with %d\n", psd);
        close(psd);
        pthread_exit(NULL);
      }
      
      printf("%s",buffer);

      printf("Line 411 \n# "); // todo testing to see if this is the one causing problems
      close(pipefd_stdout[0]);
      send(psd, "\n#436s ", 3, 0);

      // Wait for the child process to finish
      waitpid(pid, NULL, 0);
  }
  //send(psd, "\n# ", 3, 0);
    
        // Send # to indicate the end of the command output
        if (send(psd, "s\n# " , sizeof("s\n#"), 0) <0 ) {
          perror("sending stream message");
        } else {
          // Handle plain text input
          if (commandRunning) {
              // Write the plain text to the stdin of the running command
              write(pipefd_stdin[1], buffer, bytesRead);
              send(psd, "\n#499s ", 3, 0);
          } else {
              // If no command is running, send an error message
              const char *errorMsg = "\n#502s "; // Error: No command is currently running.
              send(psd, errorMsg, strlen(errorMsg), 0);
          }
        }
    pthread_exit(NULL);

}

void reusePort(int s)
{
    int one=1;
    
    if ( setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *) &one,sizeof(one)) == -1 )
	{
	    printf("error in setsockopt,SO_REUSEPORT \n");
	    exit(-1);
	}
}

void handle_command(int psd, char *command) {
  int pipefd[2];
  pid_t pid;
  pipe(pipefd);

  // create a pipe to communicate between parent and child process 
  if(pipe(pipefd) == -1) {
    perror("Pipe failed");
    return; // exit function if pipe creation fails
  }

  // Fork a child process to execute the command
  if ((pid = fork()) == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
  }

  //  Child Process 

  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO); // redirect stdout to pipe 
    close(pipefd[0]);
    close(pipefd[1]);

    char *args[] = {"/bin/sh", "-c", command, NULL};
    execvp(args[0], args); // execute the command
    perror("Execvp failed");
    exit(0);

  } else {
    // parent process 
    // close the write end of the pipe child will write to it
    close(pipefd[1]); // 

    // read the childs output from the pipe and send it to the client 
    char buffer [BUFFER_SIZE];
    ssize_t bytesRead; 
    while ((bytesRead = read(pipefd[0], buffer , sizeof(buffer))) > 0)
    {
      send(psd, buffer, bytesRead, 0);
    }
    close(pipefd[0]); // close the read end of the pipe
    waitpid(pid, NULL, 0); 
    
  }
    
    
  }

/*void handle_control_signal(int psd, char controlChar) {
  // Handle: CTL<blank><char[c|z|d]>\n
  if (controlChar == 'c') {
    // Send SIGINT
    if(fg_pid > 0) {
      printf("caught Ctrl-C\n");
      kill(pid, SIGINT);
    }
    
  } else if (controlChar == 'z') {
      // Send SIGTSTP
      if (fg_pid > 0){
        printf("caught Ctrl-Z\n");
        kill(pid, SIGTSTP);

      }
  } else if (controlChar == 'd') {
      // Send EOF signal
      printf("caught Ctrl-D\n");
      // Close the write end of the pipe to signal EOF to the child process
      close(pipefd_stdin[1]);
    }
    send(psd, "\n#522s ", 3, 0);

}*/

void handle_control(int psd, char control) {
  // handle control signals 
  printf("Handling control signal: %c\n", control); // print the control signal for debugging 
}






