#include "utils.h"
#include "struct.h"
#include "priority_queue.h"

static scheduler_t *scheduler;

void check_preempt(thread_t *thread);
void so_free_thread(thread_t **thread);
void schedule_thread(thread_t *thread);

unsigned int check_so_init_params(unsigned int time_quantum, unsigned int io) {
    return ((time_quantum > 0) && (io <= SO_MAX_NUM_EVENTS) && !scheduler);
}

int so_init(unsigned int time_quantum, unsigned int io) {
    if (!check_so_init_params(time_quantum, io)) {
        return -1;
    }

    scheduler = (scheduler_t*)malloc(sizeof(scheduler_t));
    DIE(scheduler == NULL, "Malloc failed!\n");

    scheduler->time_quantum = time_quantum;
    scheduler->io = io;
    scheduler->pq = pq_create();
    scheduler->running_thread = NULL;
    scheduler->threads = NULL;
    scheduler->n = 0;

    return 0;
}

void *thread_task(void *args) {
    
    thread_t *thread = (thread_t *)args;

    int s = sem_wait(&thread->semaphore);
    DIE(s != 0, "Semaphore wait failed!");

    thread->func(thread->priority);
    thread->state = TERMINATED;

    scheduler->running_thread = NULL;

    check_preempt(front(scheduler->pq));

    return 0;
}

void check_preempt(thread_t *thread) {
    if (!thread)
        return;
    
    if (!scheduler->running_thread) {
        scheduler->running_thread = thread;

        dequeue(scheduler->pq);

        thread->state = RUNNING;
        int s = sem_post(&thread->semaphore);
        DIE(s != 0, "Semaphore post failed!");
    } else if (scheduler->running_thread->priority < thread->priority) {
        thread_t *preempted_thread = scheduler->running_thread;
        scheduler->running_thread = thread;

        dequeue(scheduler->pq);

        thread->state = RUNNING;
        sem_post(&thread->semaphore);

        schedule_thread(preempted_thread);

        int s = sem_wait(&preempted_thread->semaphore);
        DIE(s != 0, "Semaphore wait failed!");
    } else {
        so_exec();
    }
}

void schedule_thread(thread_t *thread) {
    enqueue(scheduler->pq, thread);

    if (thread->state != WAITING)
        thread->state = READY;

    check_preempt(front(scheduler->pq));
}

tid_t so_fork(so_handler *func, unsigned int priority) {
    if (!func || priority > SO_MAX_PRIO)
        return INVALID_TID;

    thread_t *thread = create_thread(func, priority, scheduler);

    int s = sem_init(&thread->semaphore, 1, 0);
    DIE(s != 0, "Semaphore init failed!");
    
    s = pthread_create(&thread->t, NULL, &thread_task, thread);
    DIE(s != 0, "pthread_create failed!");

    add_thread(scheduler, thread);

    schedule_thread(thread);

    return thread->t;
}

int so_wait(unsigned int io) {

    if (io >= scheduler->io
        || !scheduler->running_thread
        || !scheduler->pq->size
        || !front(scheduler->pq))
        return -1;

    scheduler->running_thread->state = WAITING;
    scheduler->running_thread->event = io;
    
    thread_t *preempted_thread = scheduler->running_thread;
    
    scheduler->running_thread = NULL;

    schedule_thread(preempted_thread);

    int s = sem_wait(&preempted_thread->semaphore);
    DIE(s != 0, "Semaphore wait failed!");

    return 0;
}

int so_signal(unsigned int io) {

    if (io >= scheduler->io || !scheduler->running_thread)
        return -1;

    int no_unlocked_threads = 0;
    for (int i = 0; i < scheduler->pq->size; i++)
        if (scheduler->pq->q[i]->event == io) {
            no_unlocked_threads++;
            scheduler->pq->q[i]->state = READY;
            scheduler->pq->q[i]->event = -1;
        }

    thread_t *preempted_thread = scheduler->running_thread;
    
    scheduler->running_thread = NULL;

    schedule_thread(preempted_thread);

    int s = sem_wait(&preempted_thread->semaphore);
    DIE(s != 0, "Semaphore wait failed!");

    return no_unlocked_threads;
}

void so_exec(void) {
    if (!scheduler->running_thread)
        return;

    scheduler->running_thread->time_quantum--;

    if (!scheduler->running_thread->time_quantum) {
        thread_t *preempted_thread = scheduler->running_thread;
        preempted_thread->time_quantum = scheduler->time_quantum;
        preempted_thread->state = READY;

        scheduler->running_thread = NULL;
        schedule_thread(preempted_thread);

        int s = sem_wait(&preempted_thread->semaphore);
        DIE(s != 0, "Semaphore wait failed!");
    }

    return;
}

void so_free_thread(thread_t **thread) {
    int s = sem_destroy(&(*thread)->semaphore);
    DIE(s != 0, "Semaphore destroy failed!");
    
    free(*thread);
}

void so_end(void) {
    if (scheduler) {

        for (int i = 0; i < scheduler->n; i++)
            pthread_join(scheduler->threads[i]->t, NULL);

        for (int i = 0; i < scheduler->n; i++) 
            so_free_thread(&scheduler->threads[i]);

        if (scheduler->pq->q)
            free(scheduler->pq->q);

        if (scheduler->pq)
            free(scheduler->pq);

        if (scheduler->threads)
            free(scheduler->threads);

        free(scheduler);
        scheduler = NULL;
    }
    return;
}