#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "common.h"

#define MAX_BUFFER_SIZE 1024

char **parse_args(int argc, char **argv);
verb check_args(char **args);



typedef struct server_response{
    status status;
    char* error_message;
    size_t size;
}server_response;


void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}




void create_message(char* buffer, char* verb_as_char, char* filename, size_t* off){
    memcpy(buffer, verb_as_char, strlen(verb_as_char));
    *off += strlen(verb_as_char);
    
    if(filename != NULL){
        memcpy(buffer + *off, " ", 1);
        *off+=1;
        memcpy(buffer + *off, filename, strlen(filename));
        *off+=strlen(filename);
    }
    
    memcpy(buffer + *off, "\n\0", 2);
    *off+=2;
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
    char* remotefile = NULL;
    char* localfile = NULL;
    if(argc > 3){
        remotefile = args[3];
    }
    if(argc > 4){
        localfile = args[4];
    }

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[MAX_BUFFER_SIZE] = {0};

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

    size_t off = 0;
    create_message(buffer, verb_as_char, remotefile, &off);
    // Sending the message to the server

    if(_verb == PUT){
        struct stat fileStat;
        stat("your_file.txt", &fileStat);
        memcpy(buffer, &fileStat.st_size, sizeof(size_t));
    }

    if (send(sock, buffer, off, 0) < 0) {
        print_error_message("Error sending message");
        return -1;
    }
    if(_verb == PUT){
        int res = send_binary_file(sock, localfile);
        printf("res: %d\n", res);


    }
    // Receiving the message from the server
    if (read(sock, buffer, 1024) < 0) {
        print_error_message("Read error");
        return -1;
    }
    off = 0;
    s_response* srv_rsp = parse_server_response(buffer, &off);
    if(srv_rsp == NULL){
        return -1;
    }
    if(srv_rsp->status == OK){
        printf("OK\n");
    }
    else if( srv_rsp->status == ERROR){
        printf("ERROR\n");
        printf("%s", srv_rsp->error_message);
        free(srv_rsp);
        close(sock);

        return 0;
    }
    off++;
    
    if(_verb == GET || _verb == LIST){
        memcpy(&(srv_rsp->size), buffer+off, sizeof(size_t));
        //printf("%zu\n", srv_rsp->size);
        off+=8;


        int res = get_binary_file(sock, remotefile, srv_rsp->size);
        printf("res: %d\n", res);
    }

    // Close the socket
    close(sock);

    return 0;
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
        if (args[3] != NULL && args[4] != NULL) {
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
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}
