#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include <stdio.h>



int parmake_h(graph* dependency_graph, char* val, vector* visited){


    VECTOR_FOR_EACH(visited, temp, {
        if(temp == val) {
            print_cycle_failure(val);
            return 0;
        }
    });

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

    char* head = vector_get(vertices, 0);

    vector* visited = vector_create(NULL, NULL, NULL);
    parmake_h(dependency_graph, head, visited);

    
    vector_destroy(vertices);
    graph_destroy(dependency_graph);
    vector_destroy(visited);

    return 1;
}
