/**
 * utilities_unleashed
 * CS 341 - Fall 2023
 */
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "format.h"

double get_time_seconds() {
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    double nseconds = (double) current.tv_nsec;
    double seconds = (double) current.tv_sec + nseconds/(1000000000.0);
    //float seconds = current.tv_sec;
    return seconds;
}

double get_time_nseconds() {
    struct timespec current;
    clock_gettime(CLOCK_MONOTONIC, &current);
    return (double) current.tv_nsec;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_time_usage();
        return -1;
    }

    double start = get_time_seconds();

    pid_t child = fork();
    if(child == -1) {
        print_fork_failed();
        return 1;
    } else if (child == 0) {
        //Child
        if(execvp(argv[1], argv+1) == -1) {
            print_exec_failed();
            exit(1);
        }
    } else {
        wait(NULL);
        double end = get_time_seconds();
        double duration = end - start;
        display_results(argv, duration);
    }
    return 0;
}
