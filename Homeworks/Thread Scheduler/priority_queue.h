#ifndef PRIORITY_QUEUE_H_
#define PRIORITY_QUEUE_H_

#include "utils.h"
#include "struct.h"

void alloc_memory_for_pq(pq_t *pq);
void enqueue(pq_t *pq, thread_t *t);
thread_t *front(pq_t *pq);
thread_t *dequeue(pq_t *pq);
pq_t *pq_create();

#endif /* PRIORITY_QUEUE_H_ */