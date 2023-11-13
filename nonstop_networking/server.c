#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "common.h"
#include "format.h"


#define PORT 8080


void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}

int main(int argc, char **argv) {
    int server_fd, sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[MAX_BUFFER_SIZE];
    int bytesRead;

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

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

    // Accepting the incoming connection
    if ((sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
        print_error_message("Accept failed");
        exit(EXIT_FAILURE);
    }
    bytesRead = recv(sock, buffer, MAX_BUFFER_SIZE, 0);
    if (bytesRead < 0) {
        perror("Error receiving data");
        exit(EXIT_FAILURE);
    }

    buffer[bytesRead] = '\0'; // Null-terminate the received data

    printBuffer(buffer, MAX_BUFFER_SIZE);
    // Sending a message to the client


    FILE *file = fopen("test/dog.png", "rb");

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    rewind(file);
    size_t off = 0;
    memcpy(buffer, "ERROR\n", 7);
    off+=6;
    memcpy(buffer + off, "fail test\n", 11);
    off += 10;
    memcpy(buffer+off, &file_size, sizeof(size_t));
    off += sizeof(size_t);
    size_t total_read = 0;

    while (total_read < file_size) {
        size_t tmp_off = off%MAX_BUFFER_SIZE;
        size_t elements_to_read = (size_t)(((long)file_size < MAX_BUFFER_SIZE - (long)tmp_off) ? file_size : MAX_BUFFER_SIZE-(long)tmp_off);
        size_t elements_read = fread(buffer+tmp_off, 1, elements_to_read, file);
        
        
        send(sock, buffer, MAX_BUFFER_SIZE, 0);
        total_read += elements_to_read;
        off += elements_read;
    }
    fclose(file);



    close(sock);

    return 0;
}
