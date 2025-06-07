#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 2048
#define DEFAULT_PORT 8888
#define DEFAULT_IP "127.0.0.1"

volatile sig_atomic_t flag = 0;
int sock = 0;

void catch_ctrl_c(int sig) {
    flag = 1;
}

void str_trim_lf(char* arr, int length) {
    for (int i = 0; i < length; i++) {
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

void print_client_instructions() {
    printf("Connected to chat server at 127.0.0.1:8888\n");
    printf("Commands:\n");
    printf("- Type '@nickname message' for private messages\n");
    printf("- Type any other message for broadcast\n");
    printf("- Type 'quit' to exit\n");
    printf("----------------------------------------\n");
}

void *send_message_handler(void *arg) {
    char message[BUFFER_SIZE] = {};
    char buffer[BUFFER_SIZE + 32] = {};

    while (1) {
        fgets(message, BUFFER_SIZE, stdin);
        str_trim_lf(message, BUFFER_SIZE);

        if (strcmp(message, "quit") == 0) {
            flag = 1;
            break;
        } else {
            sprintf(buffer, "%s", message);
            send(sock, buffer, strlen(buffer), 0);
        }

        bzero(message, BUFFER_SIZE);
        bzero(buffer, BUFFER_SIZE + 32);
    }
    return NULL;
}

void *receive_message_handler(void *arg) {
    char message[BUFFER_SIZE] = {};
    while (1) {
        int receive = recv(sock, message, BUFFER_SIZE, 0);
        
        if (receive > 0) {
            printf("%s", message);
            fflush(stdout);
        } else if (receive == 0) {
            break;
        }
        
        memset(message, 0, sizeof(message));
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    char *ip = DEFAULT_IP;
    int port = DEFAULT_PORT;

    // parsing command line arguments:
    if (argc > 1) ip = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    signal(SIGINT, catch_ctrl_c);

    // socket settings:
    sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    // connecting to the server:
    int err = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err == -1) {
        printf("ERROR: connect\n");
        return EXIT_FAILURE;
    }

    // printing instructions:
    print_client_instructions();

    // creating sending and receiving threads:
    pthread_t send_msg_thread;
    if (pthread_create(&send_msg_thread, NULL, send_message_handler, NULL) != 0) {
        printf("ERROR: pthread (send)\n");
        return EXIT_FAILURE;
    }

    pthread_t recv_msg_thread;
    if (pthread_create(&recv_msg_thread, NULL, receive_message_handler, NULL) != 0) {
        printf("ERROR: pthread (receive)\n");
        return EXIT_FAILURE;
    }

    // waiting for threads or ctrl+c:
    while (1) {
        if (flag) {
            printf("\nBye\n");
            break;
        }
        sleep(1);
    }

    close(sock);
    return EXIT_SUCCESS;
} 