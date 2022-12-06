#ifndef UTILS_H_
#define UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include "so_scheduler.h"
#include "struct.h"

#define DIE(assertion, call_description)                                       \
    do {                                                                       \
        if (assertion) {                                                       \
            fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                 \
            perror(call_description);                                          \
            exit(errno);                                                       \
        }                                                                      \
    } while (0)

#define NEW 0
#define READY 1
#define RUNNING 2
#define WAITING 3
#define TERMINATED 4

#define NOT_WAITING_THREAD(s) (((s) != WAITING) ? 1 : 0)
#define COMPARE_PRIORITIES(p1, p2) (((p1) > (p2)) ? 1 : 0)

void add_thread(scheduler_t *scheduler, thread_t *thread);
thread_t *create_thread(so_handler *func, unsigned int priority,
                        scheduler_t *scheduler);

#endif /* UTILS_H_ */