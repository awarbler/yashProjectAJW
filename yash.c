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


int sockfd = 0; // declare globally to use in signal handler

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

    // closes the socket on the ctrl c or ctrl z to gracefully terminate 
    if ( signo == SIGINT || signo == SIGTSTP) {
        printf("Terminating client ....\n");
        close(sockfd);
        exit(0);
    }
    printf(" Control signal sent to server. \n");
}

// A Test function to send a command to the server 
void send_command_to_server(const char *command) {
    char message[BUFFER_SIZE] = {0}; // Buffer for the message to be sent 
    int valread;

    // debuggin output: check what is being sent
    printf("client sending command: %s\n", command);

    // Format the message to be sent to the server 
    snprintf(message, sizeof(message), "CMD %s\n", command);

    // Send the command to the server
    if (send(sockfd, message, strlen(message), 0) < 0) {
        perror("Failed to send command");
        return;
    }

    // buffer for receiving response
    char buf[BUFFER_SIZE] = {0};

    // rec response from server 
    while ((valread = recv(sockfd, buf, BUFFER_SIZE -1 , 0)) > 0)
    {
        /* code */
        buf[valread] = '\0'; // ensures null terminated staring 
        printf("%s", buf); // print the servers response
        fflush(stdout);
        // break the lop when the server sends the prompt #
        if (strstr(buf, "# ") != NULL) {
            break;
        }
    }

    //int valread = recv(sockfd, buf, BUFFER_SIZE, 0);
    if (valread < 0) {
        perror("Error receiving response");
    } //else if (valread > 0) {
        //printf("%s", buf); // print the servers response
        //printf("Client received: %s", buf); // print the servers response
    //}
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

    // set up signal handlers for ctrl c and ctrl z 
    signal(SIGINT, sig_handler); // catch ctrl c sigint
    signal(SIGTSTP, sig_handler); // catch ctrl z sigtstp

    printf("Connected to server at %s:%d\n", argv[1], PORT);
    printf("# "); // display prompt 

    // main loop to send commands and receive responses
    while (1)
    {
        /* code */
        char command[BUFFER_SIZE] = {0}; // buffer for user input 
        //printf("# "); // display prompt 

        // read command input from the user 
        if (fgets(command, sizeof(command), stdin) ==  NULL) {
            if (feof(stdin)) {
                printf("Client terminating due to EOF.Ctrl + D ...\n");
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
        if (strstr(command, ">") != NULL) {
            send_command_to_server(command); // send command to the server
            printf("Enter plain text (CTRL-D to end): \n");
            handle_plain_text(); // hnadle plain text for cat 
        } else { 
            // Use the function Send_command_to_server 
            send_command_to_server(command);

        }

    }
    // close the socket 

    printf("yash client running....\n");
    close(sockfd);
    return 0;
}

#endif
