#include "common.h"

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
#include "format.h"


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
int get_binary_file(int sock, char* path, size_t size){
    
    FILE* f = stdout;
    if(path != NULL){
        f = fopen(path, "w");
    }
    char buffer[MAX_BUF_SIZE];

    if(f == NULL) {
        sprintf(buffer, "ERROR\n%s\n", strerror(errno));
        return -1;
    }
    size_t total_sent = 0;
    while(total_sent < size) {
        int count = read(sock, buffer, MAX_BUF_SIZE);
        if(count == -1) {
            perror("read failed");
            fclose(f);
            return -1;
        }
        if(count == 0) {
            printf("Client sent too few bytes\n");
            fclose(f);
            return -1;
        }
        //printf("%zu\n", total_sent);
        write_all(f, buffer, count);
        total_sent += count;
    }
    fclose(f);
    return total_sent;
}

int send_binary_file(int sock, char* filename){

    char buffer[MAX_BUF_SIZE];

    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        sprintf(buffer, "ERROR\nUnknown file\n");
        send_all(buffer, strlen(buffer), sock);
        return -3;
    }
    
    size_t bytes_read = 0;
    size_t count = 0;
    while((count = fread(buffer, 1, MAX_BUF_SIZE, f)) > 0){
    
        if(count < 0) {
            perror("Get failed");
            fclose(f);
            return -2;
        }

        bytes_read += count;
        printf("%zu\n", bytes_read);
        send_all(buffer, count, sock);
        memset(buffer, 0, MAX_BUF_SIZE);
    }
    fclose(f); 
    return bytes_read;   
}

