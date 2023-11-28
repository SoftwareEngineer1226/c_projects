/**
 * savvy_scheduler
 * CS 341 - Fall 2023
 */
#include "libpriqueue/libpriqueue.h"
#include "libscheduler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "print_functions.h"
static double totalresponse;
static double totalwait;
static double totalturnaround;
static int prempt;
static int numarr;
static int numfin;
static int currenttime;
static int seq;
/**
 * The struct to hold the information about a given job
 */
typedef struct _job_info {
    int id;
    double runningtime;
    double priority;
    double arrivetime;
    double timeremaining;
    double starttime;
    double sequence;

    /* TODO: Add any other information and bookkeeping you need into this
     * struct. */
} job_info;

void scheduler_start_up(scheme_t s) {
    prempt = 0;
    switch (s) {
    case FCFS:
        comparision_func = comparer_fcfs;
        break;
    case PRI:
        comparision_func = comparer_pri;
        break;
    case PPRI:
        comparision_func = comparer_ppri;
        prempt = 1;
        break;
    case PSRTF:
        comparision_func = comparer_psrtf;
        prempt = 1;
        break;
    case RR:
        comparision_func = comparer_rr;
        prempt = 1;
        break;
    case SJF:
        comparision_func = comparer_sjf;
        break;
    default:
        printf("Did not recognize scheme\n");
        exit(1);
    }
    priqueue_init(&pqueue, comparision_func);
    pqueue_scheme = s;
    // Put any additional set up code you may need here
    numarr = 0;
    numfin = 0;
    totalresponse = 0;
    totalwait = 0;
    totalturnaround = 0;
    currenttime = -1;
    seq = 0;
}

static int break_tie(const void *a, const void *b) {
    return comparer_fcfs(a, b);
}

int comparer_fcfs(const void *a, const void *b) {
    // TODO: Implement me!
    //start time
    if (((job_info*)((job*)a)->metadata) ->arrivetime < ((job_info*)((job*)b)->metadata)->arrivetime) {
        return -1;
    } else if (((job_info*)((job*)a)->metadata) ->arrivetime > ((job_info*)((job*)b)->metadata)->arrivetime) {
        return 1;
    }
    return 0;
}

int comparer_ppri(const void *a, const void *b) {
    // Complete as is
    return comparer_pri(a, b);
}

int comparer_pri(const void *a, const void *b) {
    // TODO: Implement me!
    if (((job_info*)((job*)a)->metadata) ->priority < ((job_info*)((job*)b)->metadata)->priority) {
        return -1;
    } else if (((job_info*)((job*)a)->metadata) ->priority > ((job_info*)((job*)b)->metadata)->priority){
        return 1;
    }
    return break_tie(a, b);
}

int comparer_psrtf(const void *a, const void *b) {
    // TODO: Implement me!
    if (((job_info*)((job*)a)->metadata) ->timeremaining < ((job_info*)((job*)b)->metadata)->timeremaining) {
        return -1;
    } else if (((job_info*)((job*)a)->metadata) ->timeremaining > ((job_info*)((job*)b)->metadata)->timeremaining){
        return 1;
    }
    return break_tie(a, b);
}

int comparer_rr(const void *a, const void *b) {
    // TODO: Implement me!
    if (((job_info*)((job*)a)->metadata) ->sequence < ((job_info*)((job*)b)->metadata)->sequence) {
        return -1;
    } else if (((job_info*)((job*)a)->metadata) ->sequence > ((job_info*)((job*)b)->metadata)->sequence){
        return 1;
    }
    return break_tie(a, b);
}

int comparer_sjf(const void *a, const void *b) {
    // TODO: Implement me!
    if (((job_info*)((job*)a)->metadata) ->runningtime < ((job_info*)((job*)b)->metadata)->runningtime) {
        return -1;
    } else if (((job_info*)((job*)a)->metadata) ->runningtime > ((job_info*)((job*)b)->metadata)->runningtime){
        return 1;
    }
    return break_tie(a, b);
}

// Do not allocate stack space or initialize ctx. These will be overwritten by
// gtgo
void scheduler_new_job(job *newjob, int job_number, double time,
                       scheduler_info *sched_data) {
    // TODO: Implement me!
    job_info* info = malloc(sizeof(job_info));
    info->id = job_number;
    info->priority = sched_data->priority;
    info->runningtime = sched_data->running_time;
    info->arrivetime = time;
    info->timeremaining = sched_data->running_time;
    info->starttime = 0;
    info->sequence = seq;
    seq ++;
    newjob->metadata = info;
    numarr += 1;
    priqueue_offer(&pqueue, newjob);
}

job *scheduler_quantum_expired(job *job_evicted, double time) {
    // TODO: Implement me!
    if (currenttime != -1 && job_evicted) {
        ((job_info*)job_evicted->metadata)->timeremaining -= (time - currenttime);
    }
    currenttime = time;
    if (job_evicted && !prempt) {
        return job_evicted;
    }
    if (job_evicted) {
        ((job_info*)((job*)job_evicted)->metadata) ->sequence = seq;
        seq ++;
        priqueue_offer(&pqueue, job_evicted);
    }
    job* next = priqueue_poll(&pqueue);
    if (next && ((job_info*)next->metadata)->timeremaining == ((job_info*)next->metadata)->runningtime) {
        ((job_info*)next->metadata)->starttime = time;
    }
    return next;
}

void scheduler_job_finished(job *job_done, double time) {
    // TODO: Implement me!
    numfin += 1;
    totalturnaround += time - ((job_info*)(job_done->metadata))->arrivetime;
    totalwait += time - ((job_info*)(job_done->metadata))->arrivetime - ((job_info*)(job_done->metadata))->runningtime;
    totalresponse += ((job_info*)(job_done->metadata))->starttime - ((job_info*)(job_done->metadata))->arrivetime;
    free(job_done->metadata);
    
}

static void print_stats() {
    fprintf(stderr, "turnaround     %f\n", scheduler_average_turnaround_time());
    fprintf(stderr, "total_waiting  %f\n", scheduler_average_waiting_time());
    fprintf(stderr, "total_response %f\n", scheduler_average_response_time());
}

double scheduler_average_waiting_time() {
    // TODO: Implement me!
    return totalwait/numfin;
}

double scheduler_average_turnaround_time() {
    // TODO: Implement me!
    return totalturnaround/numfin;
}

double scheduler_average_response_time() {
    // TODO: Implement me!
    return totalresponse/numfin;
}

void scheduler_show_queue() {
    // OPTIONAL: Implement this if you need it!
}

void scheduler_clean_up() {
    priqueue_destroy(&pqueue);
    print_stats();
}
