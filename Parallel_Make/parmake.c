#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include <stdio.h>
#include <sys/stat.h>
#include <pthread.h>


pthread_mutex_t mutex;
graph* dependency_graph;

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

    
    char* target_name = queue_pull(q);
    if(target_name == NULL){
        //TODO: force all the threads to terminate
    }

    pthread_mutex_lock(&mutex);
    rule_t* this_target_rule = (rule_t*)graph_get_vertex_value(dependency_graph, target_name);
    vector* deps = graph_neighbors(dependency_graph, target_name);
    pthread_mutex_unlock(&mutex);
    if(this_target_rule->state == -1){
        vector_destroy(deps);
        return NULL;
    }

    rule_t* dep_rule;
    
    short pushed_back_to_q = 0;
    VECTOR_FOR_EACH(deps, dep, {
        pthread_mutex_lock(&mutex);

        dep_rule = (rule_t*)graph_get_vertex_value(dependency_graph, dep);
        pthread_mutex_unlock(&mutex);

        if(dep_rule->state == 0){
            if(pushed_back_to_q == 0){
                pushed_back_to_q = 1;
            }
            //this target(this_target_rule) will be satisfied before the dependencies are satisfied
            //how can we wait until the dependencies are satisfied
            queue_push(q, dep);
        }
        else if(dep_rule->state == -1){
            this_target_rule->state = -1;
        }

    });


    if(pushed_back_to_q){
        queue_push(q, target_name);
        vector_destroy(deps);
        return NULL;
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
    
    vector_destroy(deps);

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
    queue* target_q = queue_create(0);

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

    VECTOR_FOR_EACH(target_vertices, target_vertex,{
        int _hasCycle = hasCycle(dependency_graph, target_vertex, visited_vertices);

        if(_hasCycle){
            print_cycle_failure(target_vertex);
        }
        else{
            queue_push(target_q, target_vertex);
        }
    });

    vector_destroy(visited_vertices);


    pthread_mutex_init(&mutex, NULL);
    
    for(int i=0;i<(int)num_threads;i++){
        pthread_create(&THREADS[i], NULL, thread_func, target_q);
    }
    for (int i = 0; i < (int)num_threads; i++) {
        pthread_join(THREADS[i], NULL);
    }    
    
    queue_destroy(target_q);
    vector_destroy(target_vertices);
    graph_destroy(dependency_graph);

    return 1;
}