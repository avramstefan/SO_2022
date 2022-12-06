#ifndef STRUCT_H_
#define STRUCT_H_

typedef struct thread_t {
    tid_t t;
    so_handler *func;
    sem_t semaphore;
    uint8_t priority;
    uint8_t state;
    unsigned int time_quantum;
    unsigned int event;
} thread_t;

typedef struct pq_t {
    thread_t **q;
    int size;
    int capacity;
} pq_t;

typedef struct scheduler_t {
    pq_t *pq;
    thread_t **threads;
    thread_t *running_thread;
    unsigned int time_quantum;
    unsigned int io;
    int n;
} scheduler_t;

#endif /* STRUCT_H_*/