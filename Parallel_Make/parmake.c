#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include <stdio.h>



int parmake_h(graph* dependency_graph, char* val, vector* visited){

    short hasCycle = 0;
    VECTOR_FOR_EACH(visited, temp, {
        if(temp == val) {
            print_cycle_failure(val);
            hasCycle = 1;
        }
        
    });

    if(!hasCycle){
        vector_push_back(visited, val);
        vector* neighbors = graph_neighbors(dependency_graph, val);

        void **_it = vector_begin(neighbors);
        void **_iend = vector_end(neighbors);
        _iend--;
        for (; _it <= _iend; _iend--) {
            char* val_n = *_iend;
            int res = parmake_h(dependency_graph, val_n, visited);
            
            if(res == 0){
                vector_destroy(neighbors);
                return 0;
            }
        }   
        vector_destroy(neighbors);

    }    
    rule_t* rule = graph_get_vertex_value(dependency_graph, val);

    vector* commands = rule->commands;

    VECTOR_FOR_EACH(commands, command, {
        int res = system(command);
        if(res != 0) return 0;
    });

    return 1;
}

int parmake(char *makefile, size_t num_threads, char **targets) {

    graph* dependency_graph = parser_parse_makefile(makefile, targets);
    if(dependency_graph == NULL) return 0;
    vector* vertices = graph_vertices(dependency_graph);
    if(vector_empty(vertices)) return 0;

    vector* target_vertices = graph_neighbors(dependency_graph, "");
    
    VECTOR_FOR_EACH(target_vertices, vertice, {
        vector* visited = vector_create(NULL, NULL, NULL);
        parmake_h(dependency_graph, vertice, visited);
        vector_destroy(visited);
    });

    
    vector_destroy(target_vertices);
    vector_destroy(vertices);
    graph_destroy(dependency_graph);

    return 1;
}
