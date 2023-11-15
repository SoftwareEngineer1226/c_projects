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

void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}

void send_all(char* buffer, size_t size, int sock) {
    size_t bytes_sent = 0;
    do
    {
        size_t count = write(sock, &buffer[bytes_sent], size - bytes_sent);
        if(count < 0) {
            perror("Client socket:");
            return;
        }
        if(count == 0) {
            printf("Client disconnected\n");
            return;
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void write_all(FILE* f, char* buffer, size_t size) {
    size_t bytes_sent = 0;
    do
    {
        size_t count = fwrite(&buffer[bytes_sent], 1, size - bytes_sent, f);
        if(count < 0) {
            perror("Output file");
            return;
        }
        if(count == 0) {
            printf("Output write failed\n");
            return;
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void insert_size_into_mem(char* pBuffer, size_t size) {
    memcpy(pBuffer, &size, sizeof(size_t));
}

void perform_get(char* filename, int sock) {
    printf("Server get for %s\n", filename);
    char buffer[BUFSIZ];
    sprintf(buffer, "%s/%s", BASE_FOLDER, filename);
    struct stat file_info;
    stat(buffer, &file_info);
    FILE* f = fopen(buffer, "rb");
    if(f == NULL) {
        sprintf(buffer, "ERROR\nUnknown file\n");
        send_all(buffer, strlen(buffer), sock);
        return;
    }
    // Set up the header
    strcpy(buffer, "OK\n12345678"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer);
    insert_size_into_mem(&buffer[3], file_info.st_size);
    send_all(buffer, pBuffer - buffer, sock);
    size_t bytes_read = 0;
    do
    {
        int count = fread(buffer, 1, BUFSIZ, f);
        if(count < 0) {
            perror("Get failed");
            fclose(f);
            return;
        }
        if(count == 0) {
            printf("Client terminated early\n");
            fclose(f);
            return;
        }
        bytes_read += count;
        send_all(buffer, count, sock);
    } while (bytes_read < (size_t)file_info.st_size);
    fclose(f);    
}

void perform_put(char* filename, size_t bytes_left, char* fileDataStart, int sock) {
    printf("Server put for %s\n", filename);
    // Put is tricky since we have already read some of the bytes in the file into the buffer in the initial read.
    // Plus we need to get the bytes containing the size.

    // We will use this buffer for a few things.
    char buffer[BUFSIZ];

    // Read the size
    size_t size;
    read(sock, (char*)(&size), sizeof(size_t));

    // Now open the output file
    sprintf(buffer, "%s/%s", BASE_FOLDER, filename);
    FILE* f = fopen(buffer, "wb");
    if(f == NULL) {
        sprintf(buffer, "ERROR\n%s\n", strerror(errno));
        return;
    }
    size_t total_sent = 0;

    while(total_sent < size) {
        int count = read(sock, buffer, BUFSIZ);
        if(count == -1) {
            perror("read failed");
            fclose(f);
            return;
        }
        if(count == 0) {
            printf("Client sent too few bytes\n");
            fclose(f);
            return;
        }
        write_all(f, buffer, count);
        total_sent += count;
    }
    fclose(f);
    sprintf(buffer, "OK\n");
    send_all(buffer, strlen(buffer), sock);
}

void perform_list(int sock) {
    printf("Server list\n");
    char buffer[BUFSIZ];
    strcpy(buffer, "OK\n12345678"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer);
    size_t dataSize = 0;
    DIR *d;
    struct dirent *dir;
    d = opendir(BASE_FOLDER);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char filename[BUFSIZ];
            sprintf(filename, "%s\n", dir->d_name);
            if(*filename == '.')
                continue;
            strcpy(pBuffer, filename);
            size_t len = strlen(filename);
            pBuffer += len;
            dataSize += len;
        }
    }
    closedir(d);
    insert_size_into_mem(&buffer[3], dataSize);
    send_all(buffer, pBuffer - buffer, sock);
}

void perform_delete(char* filename, int sock) {
    printf("Server delete for %s\n", filename);
    char buffer[BUFSIZ];
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
    char buffer[BUFSIZ];
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
        bytesRead = recv(sock, buffer, BUFSIZ, 0);
        if (bytesRead < 0) {
            perror("Error receiving data");
            exit(EXIT_FAILURE);
        }

        if(strncmp(buffer, "GET", strlen("GET")) == 0) {
            char* filename = &buffer[strlen("GET")+1];
            char* newline = strchr(filename, '\n');
            *newline = '\0';
            perform_get(filename, sock);
        } else if(strncmp(buffer, "PUT", strlen("PUT")) == 0) {
            char* filename = &buffer[strlen("PUT")+1];
            char* newline = strchr(filename, '\n');
            *newline = '\0';
            size_t bytes_used_so_far = (newline+1) - buffer;
            perform_put(filename, bytesRead - bytes_used_so_far, newline+1, sock);
        } else if(strncmp(buffer, "LIST", 4) == 0) {
            perform_list(sock);
        } else if(strncmp(buffer, "DELETE", strlen("DELETE")) == 0) {
            char* filename = &buffer[strlen("DELETE")+1];
            char* newline = strchr(filename, '\n');
            *newline = '\0';
            perform_delete(filename, sock);
        }
        close(sock);
    }
    return 0;
}
