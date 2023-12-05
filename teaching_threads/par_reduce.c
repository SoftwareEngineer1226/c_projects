#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "reduce.h"
#include "reducers.h"

/* You might need a struct for each task ... */
typedef struct par_reduce_param {
    int* pList;
    int length;
    int baseCase;
    reducer fn;
} par_reduce_param;

void* reduce_with_param(void* pVoidArg) {
    par_reduce_param* pArg = (par_reduce_param*) pVoidArg;
    int *result = malloc(sizeof(int));
    *result = reduce(pArg->pList, pArg->length, pArg->fn, pArg->baseCase);
    return (void*)result;
}

int par_reduce(int *list, size_t list_len, reducer reduce_func, int base_case,
               size_t num_threads) {
    if (num_threads >= list_len) {
        return reduce(list, list_len, reduce_func, base_case);
    }
    size_t items_per_thread = list_len / num_threads;
    size_t last_thread = list_len - (num_threads-1) * items_per_thread;
    assert((num_threads-1)*items_per_thread + last_thread == list_len);
    pthread_t* threads = malloc(sizeof(pthread_t) * num_threads);
    int* result = malloc(sizeof(int) * num_threads);
    par_reduce_param* params = malloc(sizeof(par_reduce_param) * num_threads);
    
    for(size_t thread_num = 0; thread_num < num_threads; thread_num++) {
        par_reduce_param* pParam = &params[thread_num];
        pParam->pList = &list[thread_num*items_per_thread];
        pParam->fn = reduce_func;
        pParam->baseCase = base_case;
        if(thread_num == num_threads - 1) {
            pParam->length = last_thread;
        } else {
            pParam->length = items_per_thread;
        }
        pthread_create(&(threads[thread_num]), NULL, reduce_with_param, pParam);
    }

    for(size_t thread_num = 0; thread_num < num_threads; thread_num++) {
        void *retval = NULL;
        pthread_join(threads[thread_num], &retval);
        result[thread_num] = *((int *)retval);
        free(retval);
    }

    int final = reduce(result, num_threads, reduce_func, base_case);

    free(threads);
    free(params);
    free(result);
    return final;
}
