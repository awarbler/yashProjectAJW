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

typedef struct job {
  pid_t pid;
  int job_id;
  char *command;
  int is_running;

 } job_t;

 job_t jobs[20];
 int job_count = 0;
 pid_t fg_pid = -1; // foreground job process id


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
    
    /** get port information and prints it out */
    length = sizeof(server);
    if ( getsockname (sd, (struct sockaddr *)&server,&length) ) {
      perror("Error getting socket name");
      exit(0);
    }
    printf("Server Port is: %d\n", ntohs(server.sin_port));
    
    // Listen for incoming connections
    /** accept TCP connections from clients and thread a process to serve each request */
    //listen(sd,4); // changed to max clients 
    if (listen(sd, MAX_CLIENTS) < 0){
      // set socke to listen for incoming connections 
      perror("listen failed "); // print error msg if listen fails
      close(sd); // close the socket if listen fails
      exit(EXIT_FAILURE); //  exit the program with failure status 
    }

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
      pthread_t thread;
      if (pthread_create(&thread, NULL, serveClient, (void *)clientArgs) != 0) {
        perror("Error creating client thread");
        free(clientArgs);
        close(psd);
        continue;
      }
      printf("Thread created to serve client\n");

      pthread_detach(thread); // Detach the thread to unblock

    } 
    // Destroy the mutex
    pthread_mutex_destroy(&lock);
    //  Close the server socket when done
    close(sd);
    return 0;
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
    for(;;) {

      // infinite loop to keep the server running
      cleanup(buffer);

      // printf("\n...server is waiting...\n");
      // receive command from the client 
      bytesRead = recv(psd, buffer, sizeof(buffer), 0);

      if (bytesRead <= 0) {
        // read data from the client
        printf("Closed connection with %d\n", psd);
        close(psd);
        pthread_exit(NULL);
      }
      
      printf("%s",buffer);
 
      // Handle: CMD<blank><Command_String>\n
      if (strncmp(buffer, "CTL ", 4) == 0) { 
        // checking to see if the message is a control signal
          handle_control_signal(psd, buffer[4]); // handle the control signal contrl c
          
      } else if (strncmp(buffer, "CMD ", 4) == 0) { 
        // check if the message is a command 
        // Extract the command string
        char *command = buffer + 4; // extract the command string skip cmd
        command[strcspn(command, "\n")] = '\0'; // Remove the newline character
        
        // Create pipes for communication between parent and child and handle command 
        if (pipe(pipefd_stdout) == -1 || pipe(pipefd_stdin) == -1) {
          perror("pipe");
          exit(EXIT_FAILURE);
        }
        printf("Command: %s-%s\n", buffer, command); //  print the command for debugging purposes
        // Allocate memory for LogRequestArgs
        LogRequestArgs *logArgs = malloc(sizeof(LogRequestArgs));
        if (logArgs == NULL) { // check if memory allocation failed 
          perror("Error allocating memory for log request"); //  print error message 
          continue; // continue to the next iteration to accept new commands
        }

        // Copy the request and client information to the struc
        strncpy(logArgs->request, command, sizeof(logArgs->request) - 1); // copy the command to the log request 
        logArgs->from = from; // set the client address
        //  handle command execution create new thread for logging 
        handle_command(psd, command); // function to handle command execution 
        
        // Create a new thread for logging
        if (pthread_create(&p, NULL, logRequest, (void *)logArgs) != 0) { // create a new thread for logging
          perror("Error creating log request thread"); //print error
          free(logArgs); // Free the allocated memory if thread creation fails
        } else {
          pthread_detach(p); // Detach the thread to allow it to run independently
          }
  
        // Fork a child process to execute the command
        if ((pid = fork()) == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

          // child process execute the command
          if (pid == 0) {
            // Child process
            close(pipefd_stdout[0]); // Close the read end of the stdout pipe
            close(pipefd_stdin[1]);  // Close the write end of the stdin pipe

            dup2(pipefd_stdout[1], STDOUT_FILENO); // Redirect stdout to the write end of the stdout pipe
            dup2(pipefd_stdout[1], STDERR_FILENO); // Redirect stderr to the write end of the stdout pipe
            dup2(pipefd_stdin[0], STDIN_FILENO);   // Redirect stdin to the read end of the stdin pipe

            close(pipefd_stdout[1]); //  close the write end of stdout pipe after redirection 
            close(pipefd_stdin[0]); // close the read end of stdin pipe after redirection

            /*We need to toeknize the command into an argument array before paassing it to execvp */
            // tokenize the command string into arguments
            char *args[10];
            int i = 0;
            args[i] = strtok(command, " "); // split the command into tokens by space
            while (args[i] != NULL && i < 9)
            {
             /* code */
             i++;
             args[i] = strtok(NULL, " "); // continue splitting into tokens 
            }

            // execute the command 
            if (execvp(args[0], args) == -1) {
             //perror("execvp failed");
             _exit(EXIT_FAILURE);
            }

            printf("Line 411 \n# "); // todo testing to see if this is the one causing problems
            //Send # to indicate the end of the command output
            //exit(EXIT_SUCCESS);
          } else {
            // Parent process
            fg_pid = pid; // set the foreground job
            close(pipefd_stdout[1]); // Close the write end of the stdout pipe
            close(pipefd_stdin[0]);  // Close the read end of the stdin pipe
            commandRunning = 1;

            // Read the child's output from the pipe and send it to the client socket
            while ((bytesRead = read(pipefd_stdout[0], buffer, sizeof(buffer))) > 0) {
              send(psd, buffer, bytesRead, 0);
            }
            //send(psd, "\n# ", sizeof("\n# "), 0);

            close(pipefd_stdout[0]);
            send(psd, "\n#436s ", 3, 0);

            // Wait for the child process to finish
            waitpid(pid, NULL, 0);
            commandRunning = 0;
            fg_pid = -1; // clear foreground job
        }
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

  if ((pid = fork()) == 0) {
    dup2(pipefd[1], STDOUT_FILENO); // redirect stdout to pipe 
    close(pipefd[0]);
    close(pipefd[1]);

    char *args[] = {"/bin/sh", "-c", command, NULL};
    execvp(args[0], args); // execute the command
    perror("Execvp failed");
    exit(0);

  } 
    waitpid(pid, NULL, 0);
    
  }

void handle_control_signal(int psd, char controlChar) {
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

}

int handle_pipe(char **cmd_left, char **cmd_right) {
    int pipe_fd[2];
    pid_t pid1, pid2;

    // create a pipe 
    if (pipe(pipe_fd) == -1) {
        perror("Pipe failed");
        return -1;
    }

    // fork the first child process to execute cmd1
    if (( pid1 == fork()) == 0) {
        
        dup2(pipe_fd[1], STDOUT_FILENO); // redirect stdout to pipe write end
        close(pipe_fd[0]); // close read end 
        close(pipe_fd[1]);

        apply_redirections(cmd_left);
        // close read end 
        if (execvp(cmd_left[0], cmd_left) == -1){
          perror("Execution failed for the left command");
          exit(EXIT_FAILURE);
        } 
        
    }
    // second child process to execute cmd1
    if (( pid2 == fork()) == 0) {
        dup2(pipe_fd[0], STDIN_FILENO); // redirect stdout to pipe write end
        close(pipe_fd[1]); // close read end 
        close(pipe_fd[0]);

        apply_redirections(cmd_right);
        // close read end 
        if (execvp(cmd_right[0], cmd_right) == -1){
          perror("Execution failed for the cmd_right command");
          exit(EXIT_FAILURE);
        } 
    }
    // parent closes pipe and waits for both children to finish 
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    return 0;

}

void apply_redirections(char **cmd_args){
    int i = 0;
    int in_fd = -1, out_fd = -1, err_fd = -1;

    while (cmd_args[i] != NULL){
        /* code */
        if (strcmp(cmd_args[i], "<") == 0){

            in_fd = open(cmd_args[i + 1], O_RDONLY); // input redirection 
            if (in_fd < 0 ){
                cmd_args[i] = NULL;
                //cmd_args[i - 1] = NULL;
                perror("yash");
                exit(EXIT_FAILURE); // exit child process on failure
                //return;
            }
            
            dup2(in_fd, STDIN_FILENO); // replace stdin with the file 
            close(in_fd);

            // shift arguements left to remove redirecton operator and file name 
            // doing this because err1.txt and err2.txt are not getting created for redirection
            for (int j = i; cmd_args[j + 2] != NULL; j++){
                cmd_args[j] = cmd_args[j+ 2];
            }
            // shift remaing arguments 
            cmd_args[i] = NULL; // remove < 
            //cmd_args[i + 1] = NULL; // remove the file name 
            i--; // adjust index to recheck this position 

        } 
        else if (strcmp(cmd_args[i], ">") == 0) { 
            // output direction 
            out_fd = open(cmd_args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
            if (out_fd < 0) {
                perror("yash");
                exit(EXIT_FAILURE);
                //return;
            }
            // debugging 
            printf("redirection output file : %s\n" , cmd_args[i+1]);

            dup2(out_fd,STDOUT_FILENO);
            close(out_fd);

            // shift arguements left to remove redirecton operator and file name 
            // doing this because err1.txt and err2.txt are not getting created for redirection
            for (int j = i; cmd_args[j + 2] != NULL; j++){
                cmd_args[j] = cmd_args[j+ 2];
            }

            cmd_args[i] = NULL;
            // cmd_args[i + 1] = NULL;
            i--;
        } 
        else if (strcmp(cmd_args[i], "2>") == 0){
            // error redirection 
            err_fd = open(cmd_args[i + 1],O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
            if (err_fd < 0) {
                perror("yash");
                exit (EXIT_FAILURE);
                //return;
            }
            //printf("redirection output file : %s\n" , cmd_args[i+1]);
            dup2(err_fd,STDERR_FILENO);
            close(err_fd);

            // shift arguements left to remove redirecton operator and file name 
            // doing this because err1.txt and err2.txt are not getting created for redirection
            for (int j = i; cmd_args[j + 2] != NULL; j++){
                cmd_args[j] = cmd_args[j+ 2];
            }
            cmd_args[i] = NULL;
            //cmd_args[i + 1] = NULL;
            i--;
        }
        i++;
    }
}

void fg_job(int job_id) {
    // Used from from Dr.Y book Page-45-49 
    int status;

    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == job_id) {
            fg_pid = jobs[i].pid;
          
            printf("%s\n", jobs[i].command); // prints teh command when bringing to the foreground

            fflush(stdout); //ensure teh command is printed immediately 
            // brings the job to the foreground
            kill(-jobs[i].pid, SIGCONT);// wait for the process to cont. the stopped proces
    
            waitpid(jobs[i].pid, &status, WUNTRACED); // wait for the process to finish or be stopped again 

            fg_pid = -1; // no longer a fg process 

            // update job status if it was stopped again 
            if (WIFSTOPPED(status)){
                jobs[i].is_running = 0; // mark a stopped 

            } else {
                // if the job finished remove it from the job list 
                for (int j = i; j < job_count - 1; j++) {
                    jobs[j] = jobs[j + 1 ];
                }
                job_count--;
            }
            
             return;
        }
    }  
    printf("Job not found\n");

}

void bg_job(int job_id) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == job_id && !jobs[i].is_running) {
          // resume the jobs in the backgroun 
          kill(-jobs[i].pid, SIGCONT);
          jobs[i].is_running = 1;
          printf("[%d] + %s &\n", jobs[i].job_id, jobs[i].command); // prints teh command when bringing to the foreground
          return;
        }
    }
    printf("Job not found or already running\n");
}

void setup_signal_handler() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &sa, NULL);
}