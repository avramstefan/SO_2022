CC = gcc
CFLAGS = -Wextra -Wall -g

build:
	$(CC) -fPIC $(CFLAGS) -o so_scheduler.o -c so_scheduler.c
	$(CC) -fPIC $(CFLAGS) -o priority_queue.o -c priority_queue.c
	$(CC) -fPIC $(CFLAGS) -o utils.o -c utils.c
	$(CC) -fPIC $(CFLAGS) -shared -o libscheduler.so so_scheduler.o priority_queue.o utils.o

clean:
	rm -f so_scheduler.o priority_queue.o libscheduler.so utils.o