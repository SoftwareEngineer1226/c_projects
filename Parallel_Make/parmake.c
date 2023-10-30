#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include <stdio.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>


pthread_mutex_t mutex;
graph* dependency_graph;
queue* _queue;
vector* pending_targets;



time_t get_modification_time(char* filename){
    struct stat file_stat;
    
    if (stat(filename, &file_stat) == 0) {
        return file_stat.st_mtime; 
    }
    return -1;
}

int run_rule(graph* dependency_graph, rule_t* rule){

    vector* commands = rule->commands;

    VECTOR_FOR_EACH(commands, command, {
        int res = system(command);
        if(res != 0) return -1;
    });

    return 1;
}


void *thread_func(void* arg){
    queue* q = (queue*) arg;    
    char* target_name;

    vector* deps = NULL;


    while((target_name = queue_pull(q))){
        if(deps != NULL)
            vector_destroy(deps);
        if(target_name == NULL) break;

        pthread_mutex_lock(&mutex);
        rule_t* this_target_rule = (rule_t*)graph_get_vertex_value(dependency_graph, target_name);
        deps = graph_neighbors(dependency_graph, target_name);
        pthread_mutex_unlock(&mutex);

        
        rule_t* dep_rule;

        short push_back_to_pending_q = 0;
        short failed = 0;
        VECTOR_FOR_EACH(deps, dep, {
            pthread_mutex_lock(&mutex);

            dep_rule = (rule_t*)graph_get_vertex_value(dependency_graph, dep);
            pthread_mutex_unlock(&mutex);
            if(dep_rule->state == 2){
                if(push_back_to_pending_q == 0){
                    push_back_to_pending_q = 1;
                }
            }
            if(dep_rule->state == 0){
                dep_rule->state = 2;
                if(push_back_to_pending_q == 0){

                    push_back_to_pending_q = 1;
                }
                queue_push(q, dep);
            }
            else if(dep_rule->state == -1){
                this_target_rule->state = -1;
                failed = 1;
            }

        });

        if(failed){
            if(((pthread_mutex_t*)this_target_rule->data) != NULL){

                pthread_mutex_t* this_target_mutex = (pthread_mutex_t*)this_target_rule->data;
                pthread_mutex_unlock(this_target_mutex);
            }


            pthread_mutex_lock(&mutex);
            while(vector_empty(pending_targets) == false){

                char* temp = *vector_back(pending_targets);
                vector_pop_back(pending_targets);

                queue_push(q, temp);
            }
            pthread_mutex_unlock(&mutex);


            continue;

        } 
        if(push_back_to_pending_q){
            vector_push_back(pending_targets, target_name);
            continue;
        }
        


        int cur_file_modtime = (int)get_modification_time(target_name);
        int newest_dep_modtime = -1;  

        if(cur_file_modtime != -1){
            VECTOR_FOR_EACH(deps, dep, {
                int temp_modtime = (int)get_modification_time(dep);
                if(temp_modtime > newest_dep_modtime){
                    newest_dep_modtime = temp_modtime;
                }
            });
            if(newest_dep_modtime == -1){

                this_target_rule->state = run_rule(dependency_graph, this_target_rule);
            }
            else if(newest_dep_modtime > cur_file_modtime){  
                this_target_rule->state = run_rule(dependency_graph, this_target_rule);

            }
            else{
                this_target_rule->state = 1;
            }
        }
        else{
            this_target_rule->state = run_rule(dependency_graph, this_target_rule);
        }

        if(((pthread_mutex_t*)this_target_rule->data) != NULL){

            pthread_mutex_t* this_target_mutex = (pthread_mutex_t*)this_target_rule->data;
            pthread_mutex_unlock(this_target_mutex);
        }

        pthread_mutex_lock(&mutex);
            while(vector_empty(pending_targets) == false){

                char* temp = *vector_back(pending_targets);
                vector_pop_back(pending_targets);
                    
                queue_push(q, temp);
            }
        pthread_mutex_unlock(&mutex);

        
    }    
    if(deps != NULL){
        vector_destroy(deps);
    }
    return NULL;
}


/*
int parmake_h(graph* dependency_graph, char* val){


    vector* neighbors = graph_neighbors(dependency_graph, val);
    int isDependencyFailed = 0; 
    VECTOR_FOR_EACH(neighbors, neighbor, {

        int res = parmake_h(dependency_graph, neighbor);
        if(res == -1){
            isDependencyFailed = 1;
        }
    });

    if(isDependencyFailed){
        vector_destroy(neighbors);
        return -1;
    }
     

    int cur_file_modtime = (int)get_modification_time(val);
    int newest_dep_modtime = -1;  
    int retval = 0;

    if(cur_file_modtime != -1){
        VECTOR_FOR_EACH(neighbors, neighbor, {
            int temp_modtime = (int)get_modification_time(neighbor);
            if(temp_modtime > newest_dep_modtime){
                newest_dep_modtime = temp_modtime;
            }
        });
        if(newest_dep_modtime == -1){

            retval = run_rule(dependency_graph, val);
        }
        else if(newest_dep_modtime > cur_file_modtime){  
            retval = run_rule(dependency_graph, val);
        }
        else{
            retval = 1;
        }
    }
    else{
        retval = run_rule(dependency_graph, val);
    }
    vector_destroy(neighbors);

    return retval;

}*/



//checks if target has cycle with its dependencies
int hasCycle(graph* graph, char* dependency, vector* visited){
    VECTOR_FOR_EACH(visited, _temp,{
        if(strcmp((char*)_temp, dependency) == 0){
            return 1;
        }
    });
    vector_push_back(visited, dependency);

    vector* neighbors = graph_neighbors(graph, dependency);
    VECTOR_FOR_EACH(neighbors, neighbor,{
        int res = hasCycle(graph, neighbor, visited);
        if(res == 1){
            vector_destroy(neighbors);
            return 1;
        }
    });

    vector_destroy(neighbors);
    vector_pop_back(visited);
    return 0;
}

int parmake(char *makefile, size_t num_threads, char **targets) {
    
    pthread_t THREADS[num_threads];
    _queue = queue_create(0);
    pending_targets = vector_create(NULL, NULL, NULL);

    dependency_graph = parser_parse_makefile(makefile, targets);

    if(dependency_graph == NULL) return 0;
    int size = graph_vertex_count(dependency_graph);
    if(size == 0) return 0;

    vector* target_vertices;

    if(*targets == NULL) 
        target_vertices = graph_neighbors(dependency_graph, "");
    else {
        target_vertices = vector_create(NULL, NULL, NULL);
        do
        {
            vector_push_back(target_vertices, *targets++);
        } while (*targets != NULL);
    }
    vector* visited_vertices = vector_create(NULL, NULL, NULL);

    int target_count = (int)vector_size(target_vertices);
    pthread_mutex_t target_mutexes[target_count];
    for(int i=0; i< target_count;i++){
        pthread_mutex_init(&target_mutexes[i], NULL);
    }
    int i = 0;
    rule_t* deprule;
    VECTOR_FOR_EACH(target_vertices, target_vertex,{
        int _hasCycle = hasCycle(dependency_graph, target_vertex, visited_vertices);

        if(_hasCycle){
            print_cycle_failure(target_vertex);
        }
        else{
            deprule = (rule_t*)graph_get_vertex_value(dependency_graph, target_vertex);
            pthread_mutex_lock(&target_mutexes[i]);
            deprule->data = (void*)&target_mutexes[i++];
            queue_push(_queue, target_vertex);
        }
    });

    vector_destroy(visited_vertices);


    pthread_mutex_init(&mutex, NULL);
    
    for(int i=0;i<(int)num_threads;i++){
        pthread_create(&THREADS[i], NULL, thread_func, _queue);
    }
    
    for(int i = 0; i< target_count;i++){
        pthread_mutex_lock(&target_mutexes[i]);
    }
    
    for(int i = 0; i<(int)num_threads;i++){
        queue_push(_queue, NULL);
    }

    for (int i = 0; i < (int)num_threads; i++) {
        pthread_join(THREADS[i], NULL);
    }    
    
    queue_destroy(_queue);
    vector_destroy(target_vertices);
    vector_destroy(pending_targets);

    graph_destroy(dependency_graph);

    return 1;
}