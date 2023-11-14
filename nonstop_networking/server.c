#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>

#include "common.h"
#include "format.h"

#define BASE_FOLDER "test"

typedef struct c_resp{
    verb _verb;
    size_t size;
    char filename[255];

}c_resp;

c_resp* parse_client_response(char* buffer){
    c_resp* retval = (c_resp*) malloc(sizeof(c_resp));
    size_t offs = 0; 
    while(buffer[offs] != ' ' && buffer[offs] != '\n'){
        offs+=1;
    }
    buffer[offs] = '\0';
    char* buffer_ptr = &buffer[offs +1];

    char c_verb[offs];
    strcpy(c_verb, buffer);

    if(strcmp(c_verb, "PUT") == 0){
        retval->_verb = PUT;
        size_t ptr_off = 0;
        while(buffer_ptr[ptr_off] != '\n'){
            ptr_off++;
        }
        memcpy(retval->filename, buffer_ptr, ptr_off);
        memcpy(&retval->size, buffer_ptr + ptr_off, sizeof(size_t));
    }
    else if(strcmp(c_verb, "GET") == 0){
        retval->_verb = GET;
        size_t ptr_off = 0;
        while(buffer_ptr[ptr_off] != '\n'){
            ptr_off++;
        }
        memcpy(retval->filename, buffer_ptr, ptr_off);
    }
    else if(strcmp(c_verb, "LIST") == 0){
        retval->_verb = LIST;
    }
    else if(strcmp(c_verb, "DELETE") == 0){
        retval->_verb = DELETE;
        size_t ptr_off = 0;
        while(buffer_ptr[ptr_off] != '\n'){
            ptr_off++;
        }
        memcpy(retval->filename, buffer_ptr, ptr_off);
    }

    
    return retval;
}

void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}


void insert_size_into_mem(char* pBuffer, size_t size) {
    memcpy(pBuffer, &size, sizeof(size_t));
}

void perform_get(char* filename, int sock) {
    printf("Server get for %s\n", filename);
    char buffer[MAX_BUF_SIZE];
    char path[512];
    sprintf(path, "%s/%s", BASE_FOLDER, filename);
    struct stat file_info;
    stat(path, &file_info);

    // Set up the header
    strcpy(buffer, "OK\n"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer) + sizeof(size_t);
    insert_size_into_mem(buffer+3, file_info.st_size);
    send_all(buffer, pBuffer - buffer, sock);

    send_binary_file(sock, path);
}

void perform_put(char* filename, size_t bytes_left, int sock) {
    printf("Server put for %s\n", filename);
    // Put is tricky since we have already read some of the bytes in the file into the buffer in the initial read.
    // Plus we need to get the bytes containing the size.

    // We will use this buffer for a few things.
    char buffer[MAX_BUF_SIZE];
    printf("%zu\n", bytes_left);
    // Now open the output file
    char path[512];
    sprintf(path, "%s/%s", BASE_FOLDER, filename);
    get_binary_file(sock, path, bytes_left);
    strcpy(buffer, "OK\n");
    send_all(buffer, strlen(buffer), sock);

}

void perform_list(int sock) {
    printf("Server list\n");
    char buffer[MAX_BUF_SIZE];
    strcpy(buffer, "OK\n"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer) + sizeof(size_t);
    size_t dataSize = 0;
    DIR *d;
    struct dirent *dir;
    d = opendir(BASE_FOLDER);
    char list_buffer[MAX_BUF_SIZE];
    char* lbfr_ptr = &list_buffer[0]; 
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char filename[MAX_BUF_SIZE];
            sprintf(filename, "%s\n", dir->d_name);
            strcpy(lbfr_ptr, filename);
            size_t len = strlen(filename);
            lbfr_ptr += len;
            dataSize += len;
        }
    }
    insert_size_into_mem(buffer + 3, dataSize);

    send_all(buffer, pBuffer - buffer, sock);

    closedir(d);

    send_all(list_buffer, dataSize, sock);
}

void perform_delete(char* filename, int sock) {
    printf("Server delete for %s\n", filename);
    char buffer[MAX_BUF_SIZE];
    sprintf(buffer, "%s/%s", BASE_FOLDER, filename);
    int result = unlink(buffer);
    if(result == 0) {
        sprintf(buffer, "OK\n");
        send_all(buffer, strlen(buffer), sock);
    } else {
        sprintf(buffer, "ERROR\n%s\n", strerror(errno));
        send_all(buffer, strlen(buffer), sock);
    }
}


int main(int argc, char **argv) {
    if(argc != 2) {
        printf("Usage: %s portnum", argv[0]);
        exit(-1);
    }
    int portNum = atoi(argv[1]);
    if(portNum < 1024) {
        printf("Usage: %s portnum", argv[0]);
        exit(-1);
    }
    int server_fd, sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[MAX_BUF_SIZE];
    int bytesRead;

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int enable = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))<0) {
        perror(NULL);
        exit(1);
    }
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int))<0) {
        perror(NULL);
        exit(1);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(portNum);

    // Binding to the specified port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        print_error_message("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listening for incoming connections
    if (listen(server_fd, 3) < 0) {
        print_error_message("Listen failed");
        exit(EXIT_FAILURE);
    }
    while(1) {
        // Accepting the incoming connection
        if ((sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            print_error_message("Accept failed");
            exit(EXIT_FAILURE);
        }
        bytesRead = recv(sock, buffer, MAX_BUF_SIZE, 0);
        if (bytesRead < 0) {
            perror("Error receiving data");
            exit(EXIT_FAILURE);
        }

        c_resp* resp = parse_client_response(buffer);

        if(resp->_verb == GET) {
            perform_get(resp->filename, sock);
        } else if(resp->_verb == PUT) {
            perform_put(resp->filename, resp->size, sock);
        } else if(resp->_verb == LIST) {
            perform_list(sock);
        } else if(resp->_verb == DELETE) {
            perform_delete(resp->filename, sock);
        }
        close(sock);
    }
    return 0;
}
