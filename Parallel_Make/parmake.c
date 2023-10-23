#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include <stdio.h>
#include <sys/stat.h>


time_t get_modification_time(char* filename){
    struct stat file_stat;
    
    if (stat(filename, &file_stat) == 0) {
        return file_stat.st_mtime; 
    }
    return -1;
}

int run_rule(graph* dependency_graph, char* val){
    rule_t* rule = graph_get_vertex_value(dependency_graph, val);

    vector* commands = rule->commands;

    VECTOR_FOR_EACH(commands, command, {
        int res = system(command);
        if(res != 0) return 0;
    });

    return 1;
}

int parmake_h(graph* dependency_graph, char* val){


    vector* neighbors = graph_neighbors(dependency_graph, val);

    void **_it = vector_begin(neighbors);
    void **_iend = vector_end(neighbors);
    _iend--;
    for (; _it <= _iend; _iend--) {
        char* val_n = *_iend;
        int res = parmake_h(dependency_graph, val_n);
        
        if(res == 0){
            vector_destroy(neighbors);
            return 0;
        }
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

}

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
    return 0;
}

int parmake(char *makefile, size_t num_threads, char **targets) {

    graph* dependency_graph = parser_parse_makefile(makefile, targets);
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

    void **_it = vector_begin(target_vertices);
    void **_iend = vector_end(target_vertices);
    _iend--;
    for (; _it <= _iend; _iend--) {
        char* val_n = *_iend;

        vector* visited_vertices = vector_create(NULL, NULL, NULL);
        int _hasCycle = hasCycle(dependency_graph, val_n, visited_vertices);

        vector_destroy(visited_vertices);
        if(_hasCycle){
            print_cycle_failure(val_n);
        }
        else{
            parmake_h(dependency_graph, val_n);
        }
        
    }   

    

    
    vector_destroy(target_vertices);
    graph_destroy(dependency_graph);

    return 1;
}
