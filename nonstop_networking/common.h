#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#define LOG(...)                      \
    if(verbose_flag == 1)                 \
        do {                              \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n");        \
        } while (0);

#define MAX_BUF_SIZE 2048

#define HASH_TABLE_SIZE           10000

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;

typedef enum { OK, ERROR } status;


typedef enum hashtable_return_code_e {
    HASH_TABLE_OK                  = 0,
    HASH_TABLE_INSERT_OVERWRITTEN_DATA,
    HASH_TABLE_KEY_NOT_EXISTS         ,
    HASH_TABLE_SEARCH_NO_RESULT       ,
    HASH_TABLE_KEY_ALREADY_EXISTS     ,
    HASH_TABLE_BAD_PARAMETER_HASHTABLE,
    HASH_TABLE_BAD_PARAMETER_KEY,
    HASH_TABLE_SYSTEM_ERROR           ,
    HASH_TABLE_CODE_MAX
} hashtable_rc_t;


typedef struct my_hash_node_s
{
    uint32_t key;
    void *data;
    struct my_hash_node_s *next;
} my_hash_node_t;

typedef struct
{
    uint32_t num_elements;
    my_hash_node_t *nodes[HASH_TABLE_SIZE];
    uint32_t      (*hashfunc)(const uint32_t);
    char *name;
} my_hash_table_t;


void hashtable_ts_init (my_hash_table_t * hashtblP, uint32_t (*hashfuncP) (const uint32_t),
      char *tablename);
hashtable_rc_t hashtable_ts_insert (my_hash_table_t * hashtblP,
      uint32_t keyP, void *dataP);
hashtable_rc_t hashtable_ts_free (my_hash_table_t * hashtblP, const uint32_t keyP);
hashtable_rc_t hashtable_ts_get (my_hash_table_t * hashtblP,
      const uint32_t keyP, void **dataP);

void send_all(char* buffer, size_t size, int sock);

int get_binary_file(int sock, char* filename, size_t size);

int send_binary_file(int sock, char* filename);