#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include <stdio.h>



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

    vector_destroy(neighbors);
                
    rule_t* rule = graph_get_vertex_value(dependency_graph, val);

    vector* commands = rule->commands;

    VECTOR_FOR_EACH(commands, command, {
        int res = system(command);
        if(res != 0) return 0;
    });

    return 1;
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
            return 1;
        }
    });
    return 0;
}

int parmake(char *makefile, size_t num_threads, char **targets) {

    graph* dependency_graph = parser_parse_makefile(makefile, targets);
    if(dependency_graph == NULL) return 0;
    int size = graph_vertex_count(dependency_graph);
    if(size == 0) return 0;

    vector* target_vertices = graph_neighbors(dependency_graph, "");
    vector* visited_vertices;
    VECTOR_FOR_EACH(target_vertices, vertex, {
        visited_vertices = vector_create(NULL, NULL, NULL);
        int _hasCycle = hasCycle(dependency_graph, vertex, visited_vertices);

        vector_destroy(visited_vertices);
        if(_hasCycle){
            print_cycle_failure(vertex);
        }
        else{
            parmake_h(dependency_graph, vertex);
        }
    });

    
    vector_destroy(target_vertices);
    graph_destroy(dependency_graph);

    return 1;
}
