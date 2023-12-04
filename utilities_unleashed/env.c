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

int is_valid_args(char *key, char *value) {
    size_t i;
    for (i= 0; i < strlen(key); i++) {
        char c = key[i];
        if (isalpha(c) || isdigit(c) || c == '_')
            continue;
        else
            return 0;
    }
    i = 0;
    if(value[0] == '%')
        i++;
    for (; i < strlen(value); i++) {
        char c = value[i];
        if (isalpha(c) || isdigit(c) || c == '_')
            continue;
        else
            return 0;
    }
    return 1;
}

int main(int argc, char *argv[]) {
    int exist_cmd = 0;
    char **tmp = argv;
    tmp++;
    for(int i = 1; i < argc; i++) {
        if(strcmp(*tmp, "--") != 0) {
            char *key, *value;
            key = *tmp;
            value = strchr(key, '=');
            if(value == NULL) {
                print_env_usage();
            }
            *(value++) = '\0';
            
            if(!is_valid_args(key, value)) {
                print_env_usage();
            }
            if(value[0] == '%') {
                char* desired = value+1;
                value = getenv(desired);
                if(value == NULL)
                    value = "";
            }
            if(setenv(key, value, 1) == -1) {
                print_environment_change_failed();
            }
        } else {
            exist_cmd = 1;
            break;
        }
        tmp++;
    }

    if(!exist_cmd) {
        print_env_usage();
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
        char *cmd = *(++tmp);
        if(execvp(cmd, tmp) == -1) {
            print_exec_failed();
        }
    } else {
        int status;
        waitpid(child, &status, 0);
    }

}
