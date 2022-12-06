**Name: Avram Cristian-Stefan**\
**Group: 321 CA**

# Thread Scheduler

Implemented a preemptive thread scheduler in a uniprocessor system. Execution of multiple threads on a single CPU in some order is called *scheduling*. The algorithm used for scheduling is called *Round Robin*. Each thread is going to run in the context of a real thread from the system.

The source code takes into consideration several steps in order to create a correctly implemented scheduler.

First of all, these are the **structures** that represent our **tools** in the deployment phase.

```c
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
```

Now, let's explain each structure in part and see what is its role in the implementation.

1. <span style="color:#ff3333">**SCHEDULER_T**</span>
    * <span style="color:#ff704d">**io**</span> $\longrightarrow$ number of events (I/O devices) that are supported. This parameter is used when a thread is waiting for a specific event.
    * <span style="color:#ff704d">**time_quantum**</span> $\longrightarrow$ number of time units that a process has before it gets preempted. With every execution, the *time_quantum* is decremented.
    * <span style="color:#ff704d"> **n**</span> $\longrightarrow$ number of threads that have entered the system.
    * <span style="color:#ff704d">**running_thread**</span>  $\longrightarrow$ as its name suggests, this variable keeps track of the currently running thread. In a uniprocessor system, there may be just one process running.
    * <span style="color:#ff704d">**threads**</span>  $\longrightarrow$ an array containing a reference to every thread that have ever entered the system.
    * <span style="color:#ff704d">**pq**</span>  $\longrightarrow$ a reference to a priority queue structure, that keeps evidence of the existing threads in the system and helps with implementing the *Round Robin* scheduler.
2. <span style="color:#00cc99">**THREAD_T**</span>
    * <span style="color:#009999">**event**</span> $\longrightarrow$ initially settled as **-1**, suggesting that there is no **io** waited, this variable consists in the event that the thread may be waiting for.
    * <span style="color:#009999">**time_quantum**</span> $\longrightarrow$ it is the same time_quantum from the *scheduler_t*.
    * <span style="color:#009999">**state**</span> $\longrightarrow$ the thread's current state.
        * **NEW** - newly forked thread
        * **READY** - ready to be planified (to be set as running)
        * **RUNNING** - suggests a planified thread, there is usually just one running thread in the system, referenced by *running_thread* variable from *scheduler_t*
        * **WAITING** - suggests that the thread is waiting for an event (kept in *event* variable)
        * **TERMINATED** - the thread finished its task and there is no need for rescheduling it
    * <span style="color:#009999">**priority**</span>  $\longrightarrow$ thread's assigned priority, which is relevant in scheduling using priority queues.
    * <span style="color:#009999">**semaphore**</span>  $\longrightarrow$ synchronization object, used for actually implementing the scheduler. It has the following functionalities:
        * **sem_init** - function used for initializing a unnamed semaphore
        * **sem_wait** - function that locks the respectively thread. The semaphore has a value that keeps incrementing and decrementing. When this value is **0** and *sem_wait* is called, the semaphore cannot decrement anymore and the thread is blocked. This thread is going to be locked as long as the semaphore's value is **0**.
        * **sem_post** - in contrast with *sem_wait*, this function is incrementing the semaphore's value. So, in other words, this function is unlocking the thread because the semaphore's value will no longer be **0**. The thread will continue its process where it has been stopped by the last *sem_wait*.
        * **sem_destroy** - function that destroys the semaphore and releases the occupied memory.
    * <span style="color:#009999">**func**</span>  $\longrightarrow$ the function that the thread has to call. The call will happen in a wrapped function, which allows semaphores synchronization and rescheduling.
    * <span style="color:#009999">**t**</span>  $\longrightarrow$ variable used to uniquely identify a thread. It is given as the first parameter when creating a thread using *pthread_create*.
1. <span style="color:#e6e600">**PQ_T**</span>
    * <span style="color:#999900">**capacity**</span> $\longrightarrow$ represents the number of elements that fit in the priority_queue at a moment of time.
    * <span style="color:#999900">**size**</span> $\longrightarrow$ the number of elements that currently exist in the priority queue.
    * <span style="color:#999900">**q**</span> $\longrightarrow$ the actual *priority queue*, implemented as an array of threads, arranged by their priority, beginning with the highest priority. It presents more functions that are going to be presented later.

## Creating the scheduler
\
This is the first step taken in the process of implementation. It consists in creating the following function:
```c
int so_init(unsigned int time_quantum, unsigned int io);
```
It is used for initializing the scheduler variables, which are kept in the *scheduler* variable of type *scheduler_t*. The priority queue is also initialized using *pq_create* function.

So, the *scheduler* and the *priority queue* represent structures that are dynamically initialized.

## The process behind forking a thread
\
This step is the most signifiant one. There are much more functions involved. The first one that is called is:
```c
tid_t so_fork(so_handler *func, unsigned int priority);
```
*so_fork* is called when a thread is added in system. First of all, it creates a *thread_t* dynamically allocated structure using the following function, where the variables are being initialized.
```c
thread_t *create_thread(so_handler *func, unsigned int priority,
                        scheduler_t *scheduler);
```
After that, the semaphore is created using *sem_init* function.
```c
int sem_init(sem_t *sem, int pshared, unsigned int value);
```
As we create separate semaphores for each thread in part, there is no need of sharing the semaphore between the threads of a process, so we are going to pass value *0* as the second parameter. 
The starting value of the semaphore is going to be *0*, too, as we do not want our thread to start its task before being scheduled.

The actual thread instance is created with *pthread_create*, which starts a new thread in the calling process:
```c
int pthread_create(pthread_t *restrict thread,
                          const pthread_attr_t *restrict attr,
                          void *(*start_routine)(void *),
                          void *restrict arg);
```
The first parameter is going to be *t* from *thread_t* and the second parameter will be NULL, as we want to create the thread with default attributes.

The third parameter is a wrapper function used to encapsulate the *handler* that the thread should run. This function will allow just one parameter, that is represented by the last parameter from *pthread_create* $\longrightarrow$ *args*. These arguments will be sent as the *thread_t* variable that has been initialized.

Finally, the wrapper function looks like this:
```c
void *thread_task(void *args) {
    
    thread_t *thread = (thread_t *)args;

    int s = sem_wait(&thread->semaphore);
    DIE(s != 0, "Semaphore wait failed!");

    thread->func(thread->priority);
    thread->state = TERMINATED;

    ...

    return 0;
}
```

After we initialize the variables that we need, the next step is to add the thread into *scheduler_t* threads, to keep track of its existence until the end of the program, where its memory will be released. The adding process is made by the following function, where *n* variable from *scheduler* is used for keeping the number of threads:
```c
void add_thread(scheduler_t *scheduler, thread_t *thread);
```
After these steps, the thread is going to be scheduled and it is sent to this function:
```c
void schedule_thread(thread_t *thread);
```
Here, the thread is added in the priority queue and if its *state* is different from **WAITING**, then the new state is going to be **READY**. So, we have done these things and now is time for checking if the running thread should be preempted or not. This part is accomplished by the following function:
```c
void check_preempt(thread_t *thread);
```
There are more situations that may intervene in this case:
1. If there is no running thread, then this newly added thread should be planned.
2. If there is a running thread but its priority is less than the given thread's priority, the running thread will be preempted and added into queue and the other thread will be planned.
3. If the situation is not *1.* or *2.*, then the running thread will simply execute, continuing its task.

Now, let's see how the thread is planned. First of all, it is taken out of the priority queue and put as the new running thread. In order to let the thread begin his task, there is sent a signal to the semaphore by incrementing its value using *sem_post*. This will allow the thread <img src=fork_scheme.png align="right"
     alt="Size Limit logo by Anton Lovchikov" width="120" height="250">to start doing its job and unlock it from where it has been lastly locked.

If there would've been a running thread and it would've needed to be preempted, then this old running thread would've been scheduled again and put into the priority queue.

Continuing, let's suppose this thread has firstly been locked in its wrapper function, in *thread_task*, then the semaphore will unlock and the thread will run its handler. After completing these steps, its state will be **TERMINATED**, the running thread will be set to *NULL* and the scheduler will check for a new thread to plan.

## Executing a generic instruction

```c
void so_exec();
```
This function is simulating a generic instruction and it is used for decrementing the time quantum that the running thread posses. If the time quantum becomes **0**, the thread is going to be rescheduled and its semaphore will lock in the *so_exec()*, so it will continue its process from where it was rescheduled.

## Waiting and recieving a signal
If the running thread is signaled to wait for an event, then it will be locked until this event will be received. This concept is implemented in the following function:
```c
int so_wait(unsigned int io);
```
First of all, the function checks if the *io* is valid and if there is another thread available to run in the priority queue. The last condition exists for avoiding **deadlock**, when two threads are waiting for each other and there is no endline.

After this, the running thread's state will be set to **WAITING** and it will be rescheduled so that another thread will start running. To implement this function correctly, there will be a *sem_wait* used, so that the thread will continue its process from the moment when it has been locked.

The previous lines suggest a problem statement, but the solution is given by properly signaling an event to the waiting thread. This concept is implemented in:
```c
int so_signal(unsigned int io);
```
Again, the event has to be valid. Plus, there should be a running thread in the system, as the signal is usually sent from other threads.

An iterating process follows, where the function unlocks every blocked thread that has been waiting for this event / io. As one of the awaken threads may have a higher priority than the running thread, a rescheduling in needed. In case the last statement is true, the running thread will wait to continue its process from there, being locked with *sem_wait*.

## Releasing memory

At the end of the program, when ```void so_end(void)``` is called, the program will wait for every thread to finish its task and release its memory using:
```c
int pthread_join(pthread_t thread, void **retval);
```
After that, the semaphores are destroyed using *sem_destroy* and the structures are freed.

# How does a priority queue work?

Priority Queue is an abstract data type that is similar to a queue, and every element has some priority value associated with it. The priority of the elements in a priority queue determines the order in which elements are served.

First, there is a function for enqueuing threads. When adding a thread, its priority is compared to the other threads' priority and it is added as soon as it finds a thread with a lower priority. The first elements from the priority queue have the highest priorities, while the last elements have the lowest priorities. After adding a thread, the threads with lower priority are shifted to the right of the queue, so the added thread will receive the correct and best index.
```c
void enqueue(pq_t *pq, thread_t *t) {
    ...   

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
```
As there is a function for enqueuing a thread, there is a function for dequeuing a thread too. It takes out the first thread with the highest priority, that is not in **WAITING** state. This thread is returned and the threads that were positioned on the right in the priority queue will be shifted to the left.
```c
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
```
There is a ```thread_t *front(pq_t *pq)``` function too, which returns the first thread that has the highest priority and is not in **WAITING** state. It differs from *dequeue* function with the fact that it does not remove the thread from the priority queue.
