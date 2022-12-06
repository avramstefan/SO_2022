#include "utils.h"
#include "priority_queue.h"
#include "struct.h"
#include "so_scheduler.h"

thread_t *create_thread(so_handler *func, unsigned int priority,
                        scheduler_t *scheduler) {
    thread_t *thread = (thread_t *)malloc(sizeof(thread_t));
    DIE(thread == NULL, "Malloc failed!");

    thread->priority = priority;
    thread->func = func;
    thread->state = NEW;
    thread->time_quantum = scheduler->time_quantum;
    thread->event = -1;

    return thread;
}

void add_thread(scheduler_t *scheduler, thread_t *thread) {
    scheduler->n++;
    if (!scheduler->threads)
        scheduler->threads = (thread_t **)malloc(sizeof(thread_t *));
    else
        scheduler->threads = (thread_t **)realloc(scheduler->threads, scheduler->n * sizeof(thread_t *));

    DIE(scheduler->threads == NULL, "Alloc failed!");

    scheduler->threads[scheduler->n - 1] = thread;
}