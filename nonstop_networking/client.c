#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <poll.h>

#include "common.h"

#define MY_EOF 1000

char **parse_args(int argc, char **argv);
verb check_args(char **args);

typedef struct {
    char inputBuffer[BUFSIZ];
    char *pNext;
    size_t bytes_left;
    int sock;
    bool end;
} ReadState;

void send_all(char* buffer, size_t size, int sock) {
    size_t bytes_sent = 0;
    do
    {
        size_t count = write(sock, &buffer[bytes_sent], size - bytes_sent);
        if(count < 0) {
            print_connection_closed();
            exit(1);
        }
        if(count == 0) {
            print_connection_closed();
            exit(1);
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void start_read(ReadState* state, char* pBuffer, size_t bytes_left, int sock) {
    memcpy(state->inputBuffer, pBuffer, bytes_left);
    state->pNext = state->inputBuffer;
    state->bytes_left = bytes_left;
    state->sock = sock;
    state->end = false;
}

int read_next(ReadState* state) {
    if(state->end)
        return MY_EOF;
    if(state->bytes_left > 0) {
        char c = *(state->pNext++);
        state->bytes_left--;
        return c;
    } else {
        struct pollfd fds[1];
        int timeout = 1000; // ms
        fds[0].fd = state->sock;
        fds[0].events = POLLIN|POLLPRI;
        fds[0].revents = 0;
        int res = poll(fds, 1, timeout);
        if (res == 0)
        {
            //printf("poll timeout\n");
            return MY_EOF;
        } else if (res < 0) {
            printf("error in poll\n");
            return MY_EOF;
        }
        
        int count = read(state->sock, state->inputBuffer, BUFSIZ);
        if(count == 0) {
            state->end = true;
            return MY_EOF;
        }
        if(count == -1) {
            print_connection_closed();
            exit(1);
        }
        state->bytes_left = count;
        state->pNext = state->inputBuffer;
        return read_next(state);
    }
}

void read_line(ReadState* state, char *pBuffer, bool formatError) {
    int c;
    while((c = read_next(state)) != MY_EOF) {
        *pBuffer++ = (char)c;
        if(c == '\n' || c == '\0')
            return;
    }
    if(formatError)
        print_invalid_response();
    else
        print_too_little_data();
    exit(1);
}

size_t read_size(ReadState* state) {
    size_t result;
    char* p = (char*)(&result);
    for(size_t i=0; i<sizeof(size_t); i++) {
        int c = read_next(state);
        if(c == MY_EOF) {
            print_invalid_response();
            exit(1);
        }
        *p++ = (char)c;
    }
    return result;
}

void handle_list_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strcmp(buffer, "ERROR\n") == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strcmp(buffer, "OK\n") != 0) {
        print_invalid_response();
        exit(1);
    }
    size_t size = read_size(&state);
    size_t count = 0;
    do
    {
        int c = read_next(&state);
        if(c == MY_EOF) {
            print_too_little_data();
            exit(1);
        }
        putchar(c);
        count++;
    } while (count < size);
    int c = read_next(&state);
    if(c != MY_EOF) {
        print_received_too_much_data();
        exit(1);
    }
}

void handle_delete_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strcmp(buffer, "ERROR\n") == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strcmp(buffer, "OK\n") != 0) {
        print_invalid_response();
        exit(1);
    }
    int c = read_next(&state);
    if(c != MY_EOF) {
        print_received_too_much_data();
        exit(1);
    }
}

void handle_get_response(char* pBuffer, size_t bytes_left, int sock, char* filename) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strncmp(buffer, "ERROR\n", 6) == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strncmp(buffer, "OK\n", 3) != 0) {
        print_invalid_response();
        exit(1);
    }
    FILE* output = fopen(filename, "wb");
    if(output == NULL) {
        printf("Can't open %s for writing\n", filename);
        exit(1);
    }
    size_t size = read_size(&state);
    size_t count = 0;
    do
    {
        int c = read_next(&state);
        if(c == MY_EOF) {
            print_too_little_data();
            exit(1);
        }
        fputc(c, output);
        count++;
    } while (count < size);
    int c = read_next(&state);
    if(c != MY_EOF) {
        print_received_too_much_data();
        exit(1);
    }
}
void handle_put_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strncmp(buffer, "ERROR\n", 6) == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strncmp(buffer, "OK\n", 3) != 0) {
        print_invalid_response();
        exit(1);
    }
    int c = read_next(&state);
    if(c != MY_EOF) {
        print_received_too_much_data();
        exit(1);
    }
}



typedef struct s_response{
    status status;
    char* error_message;
    size_t size;
}s_response;

s_response* parse_server_response(char* buffer, size_t* off){

    while(buffer[*off] != '\n'){
        (*off)++;
    }

    char message[(*off)];
    memcpy(message, buffer, (*off));

    if(strcmp(message, "OK") != 0 && strcmp(message, "ERROR") != 0){
        print_invalid_response();
        return NULL;
    }

    s_response* serv_resp = (s_response*)malloc(sizeof(s_response));

    if(strcmp(message, "OK") == 0){
        serv_resp->status = OK;
    }
    else if(strcmp(message, "ERROR") == 0){
        serv_resp->status = ERROR;

        *off += 1;
        size_t error_off = 0;
        while(buffer[(*off)+error_off] != '\n'){
            error_off++;
        }
        serv_resp->error_message = (char*)malloc(error_off);

        memcpy(serv_resp->error_message, buffer + (*off), error_off);

        *off += error_off+1;
        
    }
    else{

        print_invalid_response();
        free(serv_resp);
        return NULL;
    }
    return serv_resp;
}

void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}

void create_message(char* buffer, char* verb_as_char, char* filename){
    if(filename != NULL && strlen(filename) > 0)
        sprintf(buffer, "%s %s\n", verb_as_char, filename);
    else
        sprintf(buffer, "%s\n", verb_as_char);
}

void send_file(char* filename, int sock) {
    struct stat file_info;
    stat(filename, &file_info);
    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        printf("Can't open file %s\n", filename);
        exit(1);
    }
    write(sock, (char*)(&file_info.st_size), sizeof(size_t));
    size_t bytes_written = 0;
    char buffer[BUFSIZ];
    do
    {
        size_t count = fread(buffer, 1, BUFSIZ, f);
        if(count == 0 || ferror(f)) {
            printf("error reading file");
            fclose(f);
            exit(1);
        }
        size_t innerCount = 0;
        do
        {
            size_t count2 = write(sock, buffer, count - innerCount);
            if(count2 <= 0) {
                print_connection_closed();
                fclose(f);
                exit(1);
            }
            innerCount += count2;
        } while (innerCount < count);
        bytes_written += count;
    } while (bytes_written < (size_t)file_info.st_size);
    fclose(f);
}


int main(int argc, char **argv) {
    char** args = parse_args(argc, argv);
    if(args == NULL){
        print_client_help();
        return -1;
    }
    char* host = args[0];
    int port = atoi(args[1]);
    char* verb_as_char = args[2];
    verb _verb = check_args(args);
    char* firstFile = NULL;
    char* secondFile = NULL;
    if(argc > 3){
        firstFile = args[3];
        secondFile = args[3];
    }
    if(argc > 4){
        secondFile = args[4];
    }

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFSIZ] = {0};

    // Creating a socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_error_message("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        print_error_message("Invalid address/ Address not supported");
        return -1;
    }

    // Connecting to the server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        print_error_message("Connection failed");
        return -1;
    }

    create_message(buffer, verb_as_char, firstFile);
    send_all(buffer, strlen(buffer), sock);
    if(_verb == PUT){
        send_file(secondFile, sock);
    }

    // Receiving the message from the server
    int recvCount = read(sock, buffer, BUFSIZ); 
    if ( recvCount < 0) {
        print_error_message("Read error");
        return -1;
    }

    if(_verb == LIST) {
        handle_list_response(buffer, recvCount, sock);
        exit(0);
    }
    if(_verb == DELETE) {
        handle_delete_response(buffer, recvCount, sock);
        print_success();
        exit(0);
    }
    if(_verb == GET) {
        handle_get_response(buffer, recvCount, sock, secondFile);
        exit(0);
    }
    if(_verb == PUT) {
        handle_put_response(buffer, recvCount, sock);
        print_success();
        exit(0);
    }
    close(sock);
    print_client_usage();
    return 1;
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL ) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}
