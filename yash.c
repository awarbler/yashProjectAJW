// this is where the client will be set up 
/*
 Step 1 Include necessary headers and global variables from the book 
 Step 2: Set up the Test function to send a command to the server 
 step 3: set up the signal handlers - ctrl c sigint control z sigtstp
 step 4: Establish a connection to the server 
 step 5: set up the command input loop  
 step 6: close the connection 
 step 7 main function 
*/
// Step 1 Include necessary headers and global variables from book
#include "yashd.h"

#define PORT 3820           // port number to connect to the server
#define BUFFER_SIZE 1024    // buffer size for communication


// GLOBAL VARIABLES 
int sockfd = 0; // declare globally to use in signal handler
promptMode = 0;
int clientAlive = 1;


// Signal Handlers
void sig_handler(int signo);
void sigtstp_handler(int sig);

// function prtotypes
void cleanup(char *buf); 
void send_command_to_server(const char *command);
void* communication_thread(void *args);
void handle_plain_text();
void handle_fg_bg_jobs(const char *command);


/* Initialize mutex lock */
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void cleanup(char *buf)
{
    //int i;
    //for(i=0; i<BUFFER_SIZE; i++) buf[i]='\0';
    memset(buf, 0, BUFFER_SIZE);
}

// Signal handler to manage ctrl c and ctrl z signals 
void sig_handler(int signo) {
    char ctl_msg[BUFFER_SIZE] = {0};    // Buffer for control message

    if (signo == SIGINT) {
        snprintf(ctl_msg, sizeof(ctl_msg), "CTL c\n"); // Send ctl c for ctrl-c
    } else if (signo == SIGTSTP) {
        snprintf(ctl_msg, sizeof(ctl_msg), "CTL z\n"); // Send ctl z for ctrl-z
    }

    // Send the control message to the server
    if (send(sockfd, ctl_msg, strlen(ctl_msg), 0) < 0) {
        perror("Failed to send control signal");
    }

    printf(" Control signal sent to server. \n");
}

// A Test function to send a command to the server 
void send_command_to_server(const char *command) {

    char message[BUFFER_SIZE] = {0}; // Buffer for the message to be sent 
    int valread;

    // lock mutex if needed for shared resources I moved because I need to wrap the send
    pthread_mutex_lock(&lock); // wrap cs in a lock 

    // buffer for receiving response
    char buf[BUFFER_SIZE] = {0};

    // Format the message to be sent to the server
    // snprintf formats teh msg with cmd premix to adhere protocol 
    snprintf(message, sizeof(message), "CMD %s\n", command);
    
    // Send the command to the server
    if (send(sockfd, message, strlen(message), 0) < 0) {
        perror("Failed to send command");
        pthread_mutex_unlock(&lock); // unlock
        return;
    }

    // unlock mutex after the command is sent 
    pthread_mutex_unlock(&lock); // unlock 

    // debuggin output: check what is being sent
    printf("Client sending command: %s\n", command);

    // rec response from server 
    while ((valread = recv(sockfd, buf, BUFFER_SIZE -1 , 0)) > 0)
    {
        /* code */
        buf[valread] = '\0'; // ensures null terminated staring 
        
        printf("%s", buf); // print the servers response
        fflush(stdout);
        
        // break the lop when the server sends the prompt #
        if (strstr(buf, "\n#") != NULL) {
            break;
        }
    }

    //int valread = recv(sockfd, buf, BUFFER_SIZE, 0);
    if (valread == 0) {
        printf("Server closed the connection\n");
    } else if (valread < 0) {
        perror("Error receiving response");
    } 
}

// added from gabs work 
// Communication thread to handle communication with the server
void* communication_thread(void *args){
    // buffer for receiving response
    char buf[BUFFER_SIZE] = {0};
    char msg_buf[BUFFER_SIZE] = {0};
    int bytesRead;
    int message_len = 0;

    while(1) {

        cleanup(buf); // clear the buffer

        // read response from the server
        bytesRead = recv(sockfd, buf, BUFFER_SIZE -1, 0);
        if (bytesRead < 0) {
            perror("Error receiving response from server");
            pthread_exit(NULL);
        } else if (bytesRead > 0) {
            buf[bytesRead] = '\0'; //  null terminate the received data

            // append the new data to the accumulated message buffer 
            if (message_len + bytesRead < BUFFER_SIZE) {
                strncat(msg_buf, buf, bytesRead);
                message_len += bytesRead;
            } else {
                perror("buffer overflow detected, message to long");
                pthread_exit(NULL);
            }
            
            // check if the delimiter '\n\ is in the accumulate message bufffer
            char *delimter_pos = strstr(msg_buf, "\n#142c ");
            if (delimter_pos != NULL) {
                // we found the delimiter extract the complete message 
                *delimter_pos = '\0';

                // process teh completee message (display the user)
                printf(" %s" , msg_buf);

                // shift any remaining unprocessed data to the beginning of the buffer 
                int remaining_data_len = message_len - (delimter_pos - msg_buf + 3); //  +3 for the "\n # "
                memmove(msg_buf, delimter_pos + 3, remaining_data_len);
                msg_buf[remaining_data_len]= '\0';

                // set the prompt mode since we have received the full message 
                pthread_mutex_lock(&lock); // wrap cs in lock 
                promptMode = 1;
                pthread_mutex_unlock(&lock); // unlock 
            }
            

            /*if (strcmp(buf, "\n# ") == 0) {
                // server sent prompt
                pthread_mutex_lock(&lock); // wrap CS in lock ...
                promptMode = 1;
                pthread_mutex_unlock(&lock); // ... unlock
                printf("switched promptMode: %d\n", promptMode);
                fflush(stdout);
            }*/
            //printf("%s", buf); // print the servers response
            fflush(stdout);
            //printf("5promptMode: %d\n", promptMode);
            //fflush(stdout);
        } else {
            // server has closed the connection
            printf("Server closed the connection\n");
            close(sockfd);
            exit(EXIT_FAILURE); // Exit the entire program
            // break;
        }
    }
    pthread_exit(NULL);
}

void handle_fg_bg_jobs(const char *command) {

    send_command_to_server(command);
    /*if (strcmp(command, "fg") == 0 || strcmp(command, "bg") == 0 || strcmp(command, "jobs") == 0) {
        send_command_to_server(command);
    } else {
        printf("Unknown job command: %s\n", command);
    }*/
}

// handle plain text input after issuing commands like cat 
void handle_plain_text() {
    char line[BUFFER_SIZE] = {0};

    // the user can input multiple lines of text until the type eof (ctrl-d)
    while (fgets(line, sizeof(line), stdin))
    {
        /* code */
        if (send(sockfd, line, strlen(line), 0) < 0) {
            perror("Failed to send text to the server");
            break;
        }
    }
    // end of input to the server by closing the connection 
    shutdown(sockfd, SHUT_WR); // close write channel 
    printf("End of plain text input(Ctrl+D detected.)\n");

}

#ifndef TESTING

int main(int argc, char *argv[]){

    pthread_t comm_thread;
    int threadStatus;
    struct sockaddr_in servaddr; // structure for server address 

    // check for correct number of arguments expect server ip 
    if ( argc !=2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // create a sockt for communication ipv4, tcp 
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) <0) {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    // setup server address structure 
    servaddr.sin_family = AF_INET; // ipv4 protocol 
    servaddr.sin_port = htons(PORT); // convert port number to network byte

    // convert ip address from text to binary format 
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0) {
        perror("Invalid ip address / address not supported");
        return EXIT_FAILURE;
    }

    // connect socket to server address
    if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) <0) {
        perror("Connectio to server failed");
        return EXIT_FAILURE;
    }

    // launch the communication thread 
   
    if(pthread_create(&comm_thread, NULL, communication_thread,NULL) != 0) {
        perror("Error creating communiaton thread");
        return EXIT_FAILURE;
    }

    pthread_detach(comm_thread);

    // set up signal handlers for ctrl c and ctrl z 
    signal(SIGINT, sig_handler); // catch ctrl c sigint
    signal(SIGTSTP, sig_handler); // catch ctrl z sigtstp

    printf("Connected to server at %s:%d\n", argv[1], PORT);
    printf("Client Prompt # "); // display prompt 

    

    // main loop to send commands and receive responses
    while (clientAlive)
    {
        /* code */
        char command[BUFFER_SIZE] = {0}; // buffer for user input 
        

        // read command input from the user 
        if (fgets(command, sizeof(command), stdin) ==  NULL) {
            if (feof(stdin)) {
                printf("Client terminating due to EOF.Ctrl + D ...\n");
                pthread_cancel(comm_thread); 
                //pthread_join(communication_thread, NULL);
                shutdown(sockfd, SHUT_RDWR); // closing the socket
                break; // exit on eof ctrl d 
            } else {
                perror("error reading command");
                continue;
            }
        }
        // remove the newline character from the input 
        command[strcspn(command, "\n")]  = 0;

        // check if hte command is a file redirecton command cat>file.txt
        if (strcmp(command, "jobs") == 0 || strcmp(command, "fg") == 0 || strcmp(command, "bg") == 0) {
            handle_fg_bg_jobs(command);
            //send_command_to_server(command); // send command to the server
            //printf("Enter plain text (CTRL-D to end): \n");
            //handle_plain_text(); // hnadle plain text for cat 
        } else if (strstr(command, "<") || strstr(command, ">") || strstr(command ,"|") || strstr(command, "&")){
            // redirection and piping commands handled by the server 
            send_command_to_server(command);
        } else { 
            // Use the function Send_command_to_server 
            send_command_to_server(command);
        }
        // display prompt again 
        printf("305 # ");

    }
    // close the socket 
    // pthread_join(thread, NULL);
    pthread_cancel(comm_thread);
    //pthread_join(thread, NULL);

    printf("yash client running....\n");
    close(sockfd);
    return 0;
}

#endif
