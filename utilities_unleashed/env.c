/**
 * utilities_unleashed
 * CS 341 - Fall 2023
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wait.h>
#include <stdlib.h>
#include <ctype.h>
#include "format.h"

void copy_str_array(char** destination, char** original, int length) {
    for (int i = 0; i < length; i++) {
        destination[i] = malloc(sizeof(char) * strlen(original[i])+1);
        strcpy(destination[i], original[i]);
        destination[i][strlen(original[i])] = '\0';
    }
}

char** split_string(char* string, char delim) {
    char* first_instance = strchr(string, delim);
    if(first_instance == NULL) {
        return NULL;
    }
    char** split = malloc(sizeof(char*)*3);
    // abc=def
    // 0123456
    // first_instance = 3
    // string = 0
    // strlen(string) = 7
    // strlen(first_instance) = 4
    // 7-4 = 3
    split[0] = malloc(sizeof(char) * (strlen(string) - strlen(first_instance)+1));
    split[1] = malloc(sizeof(char) * (strlen(first_instance)));
    split[2] = NULL;

    strncpy(split[0], string, sizeof(char) * (strlen(string) - strlen(first_instance)));
    strncpy(split[1], first_instance+1, strlen(first_instance)-1);
    split[0][strlen(string) - strlen(first_instance)] = '\0';
    split[1][strlen(first_instance)-1] = '\0';
    return split;
}

char** get_cmd(int argc, char* argv[]) {
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "--") == 0) {
            i++;
            if(i >= argc) {
                return NULL;
            }
            char** new_argv = malloc(sizeof(char*) * argc + sizeof(NULL));
            copy_str_array(new_argv, argv+i, argc - i);
            new_argv[argc-i] = NULL;
            return new_argv;
        }
    }
    return NULL;
}

void clear_str_array(char** arr) {
    char** current = arr;
    while(*current != NULL) {
        free(*current);
        current++;
    }
    free(arr);
}
int main(int argc, char *argv[]) {
    char** nargv = get_cmd(argc, argv);
    if(nargv == NULL) {
        print_env_usage();
        return -1;
    }

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--") != 0) {
            char* current = argv[i];
            int equalsCount = 0;
            while(*current != '\0') {
                if(*current == '=')
                    equalsCount++;
                else if(!isalpha(*current) && !isdigit(*current) && *current != '_') {
                    print_env_usage();
                    return -1;
                }
                current++;
            }
            if(equalsCount != 1) {
                    print_env_usage();
                    return -1;
            }
            char** key_value = split_string(argv[i],'=');
            if(key_value == NULL) {
                print_env_usage();
            }
            if(key_value[1][0] == '%') {
                char* desired = &key_value[1][1];
                char* val = getenv(desired);
                free(key_value[1]);
                key_value[1] = strdup(val);
            }
            if(setenv(key_value[0], key_value[1], 1) == -1) {
                print_environment_change_failed();
            }
            clear_str_array(key_value);
            /*
            if(putenv(argv[i]) != 0) {
                print_environment_change_failed();
            }*/
        } else {
            break;
        }
    }
    
    /*
    find index of = and split into two parts
    if right part starts with % then use getenv to get the value of the variab
    use setenv(char* name, char* value, int overwrite) to set it
    */


    pid_t child = fork();
    if(child ==-1) {
        print_fork_failed();
    }
    if (child == 0) {
        //Child
        if(execvp(nargv[0], nargv) == -1) {
            print_exec_failed();
        }
    } else {
        int status;
        waitpid(child, &status, 0);
    }

    clear_str_array(nargv);
}
