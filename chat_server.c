#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdarg.h>

#define MAX_CLIENTS 50
#define BUFFER_SIZE 2048
#define NAME_LEN 32

// client struct:
typedef struct {
    int socket;
    char nickname[NAME_LEN];
    char ip[INET_ADDRSTRLEN];
    int port;
} client_t;

// global variables:
client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *log_file;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// fınction prototypes:
void log_message(const char *format, ...);
void console_log(const char *format, ...);
void *handle_client(void *arg);
void send_message(const char *message, int sender_socket);
void send_private_message(const char *message, const char *target, int sender_socket);
int add_client(client_t *client);
void remove_client(int socket);
client_t *get_client_by_nickname(const char *nickname);
int is_nickname_taken(const char *nickname);

// console logging function:
void console_log(const char *format, ...) {
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("[LOG] %s\n", buffer);
}

// file logging function:
void log_message(const char *format, ...) {
    pthread_mutex_lock(&log_mutex);
    
    time_t now;
    time(&now);
    char time_str[26];
    ctime_r(&now, time_str);
    time_str[24] = '\0';  // remove newline
    
    fprintf(log_file, "[%s] ", time_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}

// add clşent to the array:
int add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = client;
            pthread_mutex_unlock(&clients_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

// remove client from array:
void remove_client(int socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->socket == socket) {
            free(clients[i]);
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// check if nickname is already taken:
int is_nickname_taken(const char *nickname) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && strcmp(clients[i]->nickname, nickname) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

// get client by nickname:
client_t *get_client_by_nickname(const char *nickname) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && strcmp(clients[i]->nickname, nickname) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return clients[i];
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

// send message to all clients (except sender):
void send_message(const char *message, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL && clients[i]->socket != sender_socket) {
            send(clients[i]->socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// send private message to specific client:
void send_private_message(const char *message, const char *target, int sender_socket) {
    client_t *target_client = get_client_by_nickname(target);
    if (target_client != NULL) {
        send(target_client->socket, message, strlen(message), 0);
    } else {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "Error: User '%s' not found\n", target);
        send(sender_socket, error_msg, strlen(error_msg), 0);
    }
}

// handle client connection:
void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    char nickname[NAME_LEN];
    int valid_nickname = 0;

    // get nickname from client:
    while (!valid_nickname) {
        send(client->socket, "Enter your nickname: ", 20, 0);
        int bytes_received = recv(client->socket, nickname, NAME_LEN - 1, 0);
        if (bytes_received <= 0) {
            close(client->socket);
            free(client);
            return NULL;
        }
        
        nickname[bytes_received - 1] = '\0';  // remove newline
        
        if (is_nickname_taken(nickname)) {
            send(client->socket, "Nickname already taken or invalid. Please choose another: ", 57, 0);
        } else {
            valid_nickname = 1;
            strncpy(client->nickname, nickname, NAME_LEN);
            
            // log client connection:
            console_log("Client %s connected from %s:%d", 
                       client->nickname, client->ip, client->port);
            log_message("Client %s connected from %s:%d",
                       client->nickname, client->ip, client->port);
            
            // send acceptance message:
            send(client->socket, "Nickname accepted! You can now chat.\n", 36, 0);
            
            // announce new client to all
            snprintf(buffer, BUFFER_SIZE, "%s has joined the chat.\n", client->nickname);
            send_message(buffer, client->socket);
        }
    }

    // main chat loop:
    while (1) {
        int bytes_received = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        
        // handle private messages:
        if (buffer[0] == '@') {
            char *space = strchr(buffer, ' ');
            if (space != NULL) {
                *space = '\0';
                char *target = buffer + 1;
                char *message = space + 1;
                
                // log the message:
                console_log("Message from %s: %s", client->nickname, buffer);
                log_message("Message from %s: %s", client->nickname, buffer);
                
                // format and send private message:
                char formatted_msg[BUFFER_SIZE];
                snprintf(formatted_msg, BUFFER_SIZE, "[Private from %s]: %s\n", client->nickname, message);
                send_private_message(formatted_msg, target, client->socket);
            }
        } else {
            // log the message:
            console_log("Message from %s: %s", client->nickname, buffer);
            log_message("Message from %s: %s", client->nickname, buffer);
            
            // broadcast message:
            char formatted_msg[BUFFER_SIZE];
            snprintf(formatted_msg, BUFFER_SIZE, "%s: %s\n", client->nickname, buffer);
            send_message(formatted_msg, client->socket);
            send(client->socket, formatted_msg, strlen(formatted_msg), 0);
        }
    }

    // client disconnection:
    console_log("Client %s disconnected", client->nickname);
    log_message("Client %s disconnected", client->nickname);
    
    snprintf(buffer, BUFFER_SIZE, "%s has left the chat.\n", client->nickname);
    send_message(buffer, client->socket);
    
    close(client->socket);
    remove_client(client->socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    pthread_t thread_id;
    
    // opening log file:
    log_file = fopen("chat_server.log", "a");
    if (log_file == NULL) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    // creating socket:
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        console_log("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // setting socket options:
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        console_log("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // configuring server address:
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);

    // binding socket:
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        console_log("Bind failed");
        exit(EXIT_FAILURE);
    }

    // listening for connections:
    if (listen(server_socket, 5) < 0) {
        console_log("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Chat server started on port 8888\n");
    console_log("Chat server started");
    log_message("Chat server started");

    // accepting client connections:
    while (1) {
        socklen_t client_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_socket < 0) {
            console_log("Accept failed");
            continue;
        }

        // creating new client structure:
        client_t *client = (client_t *)malloc(sizeof(client_t));
        client->socket = client_socket;
        inet_ntop(AF_INET, &client_addr.sin_addr, client->ip, INET_ADDRSTRLEN);
        client->port = ntohs(client_addr.sin_port);

        // adding client to array:
        if (add_client(client) < 0) {
            console_log("Maximum clients reached");
            free(client);
            close(client_socket);
            continue;
        }

        // creating new thread for client:
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client) != 0) {
            console_log("Failed to create thread");
            remove_client(client_socket);
            free(client);
            close(client_socket);
            continue;
        }

        // detaching thread:
        pthread_detach(thread_id);
    }

    return 0;
} 