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

typedef struct s_response{
    status status;
    char* error_message;
    size_t size;
}s_response;



s_response* parse_server_response(char* buffer, size_t* off);

void send_all(char* buffer, size_t size, int sock);

int get_binary_file(int sock, char* filename, size_t size);

int send_binary_file(int sock, char* filename);