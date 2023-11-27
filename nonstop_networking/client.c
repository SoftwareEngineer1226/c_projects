#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>

#include "common.h"

#define MY_EOF 1000

char **parse_args(int argc, char **argv);
verb check_args(char **args);

typedef struct {
    char inputBuffer[MAX_BUF_SIZE];
    char *pNext;
    size_t bytes_left;
    int sock;
    bool end;
} ReadState;

static int epipe = 0;
static void handle_sigpipe()
{
  epipe = 1;
}

void send_all(char* buffer, size_t size, int sock) {
    size_t bytes_sent = 0;
    do
    {
        int count;
        count = send(sock, &buffer[bytes_sent], size - bytes_sent, 0);
        if(count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            
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
        int count = read(state->sock, state->inputBuffer, MAX_BUF_SIZE);
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
        if(c == '\n' || c == '\0') {
            *pBuffer = '\0';
            return;
        }
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

static void print_response_status(ReadState* state, char* buffer) {
     if(strncmp(buffer, "ERROR\n", 6) == 0) {
        read_line(state, buffer, true);
        fprintf(stderr, "STATUS_ERROR\n");
        print_error_message(buffer);
        exit(1);
    }
    if(strncmp(buffer, "OK\n", 3) != 0) {
        print_invalid_response();
        exit(1);
    }
    fprintf(stderr, "STATUS_OK\n");
}

void handle_list_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[MAX_BUF_SIZE];
    read_line(&state, buffer, true);
    print_response_status(&state, buffer);
    size_t size = read_size(&state);
    size_t count = 0;
    
    fprintf(stderr, "Expecting %zu bytes from server\n", size);
    while (count < size)
    {
        int c = read_next(&state);
        if(c == MY_EOF) {
            print_too_little_data();
            exit(1);
        }
        putchar(c);
        count++;
    } 
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
    
    fprintf(stderr, "Received %zu bytes from server\n", count);
}

void handle_delete_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[MAX_BUF_SIZE];
    read_line(&state, buffer, true);
    print_response_status(&state, buffer);
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}

static void write_all(FILE* f, char* buffer, size_t size) {
    if(size == 0)
        return;
    
    size_t bytes_sent = 0;
    do
    {
        int count;
        count = fwrite(&buffer[bytes_sent], 1, size - bytes_sent, f);
        if(count < 0) {
            print_error_message("Output file");
            return;
        }
        if(count == 0) {
            print_error_message("Output write failed");
            return;
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void handle_get_response(char* pBuffer, size_t bytes_left, int sock, char* filename) {
    ReadState state;
    memset(&state, 0, sizeof(state));
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[81920] = "";
    read_line(&state, buffer, true);
    print_response_status(&state, buffer);
    
    FILE* output = fopen(filename, "wb");
    if(output == NULL) {
        fprintf(stderr, "Can't open %s for writing\n", filename);
        exit(1);
    }
    size_t size = read_size(&state);
    size_t readcount = 0;

    fprintf(stderr, "Expecting %zu bytes from server\n", size);

    write_all(output, state.pNext, state.bytes_left);
    readcount = state.bytes_left;

    while(1) {
        
        int count = read(state.sock, buffer, sizeof(buffer));
        if(count == 0) {
            state.end = true;
            break;
        }
        if(count == -1) {
            print_connection_closed();
            fclose(output);
            exit(1);
        }

        readcount += count;
        if( readcount > size ) {
            print_received_too_much_data();
            fclose(output);
            exit(1);
        }
        write_all(output, buffer, count);
        
    }
        
    if( readcount < size ) {
        print_too_little_data();
    }

    fprintf(stderr, "received %zu bytes from server\n", readcount);
    fclose(output);
}

void handle_put_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[MAX_BUF_SIZE];
    read_line(&state, buffer, true);
    print_response_status(&state, buffer);
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}



typedef struct s_response{
    int status;
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
        fprintf(stderr, "%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    fprintf(stderr, "\n");
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

    fprintf(stderr, "File size: %zu\n", file_info.st_size);
    
    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        fprintf(stderr, "Can't open file %s\n", filename);
        exit(1);
    }

    size_t bytes_written = 0;
    char buffer[MAX_BUF_SIZE];
    memcpy(buffer, &file_info.st_size, sizeof(size_t));
    send_all(buffer, sizeof(size_t), sock);
    
    do
    {
        size_t count = fread(buffer, 1, MAX_BUF_SIZE, f);
        if(count == 0 || ferror(f)) {
            fprintf(stderr, "error reading file");
            fclose(f);
            exit(1);
        }
        send_all(buffer, count, sock);
        bytes_written += count;
    } while (bytes_written < (size_t)file_info.st_size);
    fclose(f);

    fprintf(stderr, "Sent %zu bytes of file\n", bytes_written);
}

int main(int argc, char **argv) {
    char *host = strtok(argv[1], ":");
    char *strport = strtok(NULL, ":");

    signal(SIGPIPE, handle_sigpipe);
    
    if (strport == NULL) {
        print_client_help();
        return -1;
    }
    int port = atoi(strport);
    char* verb_as_char = argv[2];
    verb _verb = check_args(argv);
    char* firstFile = NULL;
    char* secondFile = NULL;
    if(argc > 3){
        firstFile = argv[3];
        secondFile = argv[3];
    }
    if(argc > 4){
        secondFile = argv[4];
    }

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[MAX_BUF_SIZE] = {0};

    // Creating a socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_error_message("Socket creation error");
        return -1;
    }
    
    int errcode;
    struct addrinfo hints, *res, *result;
    void *ptr;
    char addrstr[64] = "";

    memset (&hints, 0, sizeof (hints));
      hints.ai_family = PF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags |= AI_CANONNAME;

    errcode = getaddrinfo (host, NULL, &hints, &result);
    if (errcode != 0)
    {
          perror ("getaddrinfo");
          return -1;
    }
      
    res = result;
    
    while (res)
    {
        inet_ntop (res->ai_family, res->ai_addr->sa_data, addrstr, 64);
    
        switch (res->ai_family)
        {
            case AF_INET:
                ptr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
                inet_ntop (res->ai_family, ptr, addrstr, 100);
                host = addrstr;
                break;
            case AF_INET6:
                break;
        }
        res = res->ai_next;
    }
      
    freeaddrinfo(result);

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

    if ( shutdown(sock, 1) == -1 )
    {
      perror("shutdown: ");
      if ( !epipe )
        exit(1);
    }

    fprintf(stderr, "processing response\n");

    // Receiving the message from the server
    int recvCount = read(sock, buffer, MAX_BUF_SIZE); 
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
