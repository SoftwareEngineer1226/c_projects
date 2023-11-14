#pragma once
#include <stddef.h>
#include <sys/types.h>

#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

#define MAX_BUF_SIZE 1024


typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;

typedef enum { OK, ERROR } status;




void send_all(char* buffer, size_t size, int sock);

int get_binary_file(int sock, char* filename, size_t size);

int send_binary_file(int sock, char* filename);