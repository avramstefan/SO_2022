#include "utils.h"
#include "priority_queue.h"
#include "struct.h"

pq_t *pq_create() {
    pq_t *pq = (pq_t *)malloc(sizeof(pq_t));
    
    pq->size = 0;
    pq->capacity = 1;
    pq->q = NULL;

    return pq;
}

void alloc_memory_for_pq(pq_t *pq) {
    pq->capacity *= 2;

    if (pq->q)
        pq->q = (thread_t **)realloc(pq->q, pq->capacity * sizeof(thread_t *));
    else
        pq->q = (thread_t **)malloc(sizeof(thread_t *));

    DIE(pq->q == NULL, "Alloc failed!");
}

void enqueue(pq_t *pq, thread_t *t) {
    pq->size++;

    if (pq->size == pq->capacity)
        alloc_memory_for_pq(pq);    

    pq->q[pq->size - 1] = t;
    for (int i = 0; i < pq->size - 1; i++) {
        if (COMPARE_PRIORITIES(t->priority, pq->q[i]->priority)) {
            for (int j = pq->size - 1; j > i; j--)
                pq->q[j] = pq->q[j - 1];
            pq->q[i] = t;
            break;
        }
    }
}

thread_t *front(pq_t *pq) {
    for (int i = 0; i < pq->size; i++)
        if (NOT_WAITING_THREAD(pq->q[i]->state))
            return pq->q[i];
    return NULL;
}

thread_t *dequeue(pq_t *pq) {
    for (int i = 0; i < pq->size; i++)
        if (NOT_WAITING_THREAD(pq->q[i]->state)) {
            thread_t *ret_thread = pq->q[i];
            
            for (int j = i; j < pq->size - 1; j++)
                pq->q[j] = pq->q[j + 1];
            
            pq->size--;
            return ret_thread;
        }
    return NULL;
}