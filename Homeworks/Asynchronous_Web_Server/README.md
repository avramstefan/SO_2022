**Name: Avram Cristian-Stefan**\
**Group: 321 CA**

# Asynchronous Web Server


1. [**Introduction**](#1-introduction)
2. [**Handle a new connection**](#2-handle-a-new-connection)
3. [**Handle client request**](#3-handle-client-request)
4. [**Send a message**](#4-send-a-message)
5. [**Sockets**](#5-sockets)
6. [**Epoll**](#6-epoll)

## **1. Introduction**
The most important aspect of **asynchronous web servers** is represented by the possibility to deliever spontaneous presentation changes to the user as the state of a dynamic system changes. Therefore, this project is oriented in implementing this concept, using Linux advanced **I/O**, **POSIX asynchronous
I/O (AIO)** and the notion of **sockets** ([**Sockets section**](#5-sockets)).

The server is started by running the *aws* executable (```./aws```). It then creates an epoll ([**Epoll section**](#6-epoll)), used for monitoring multiple
file descriptors. A listener socket is also created, representing, in fact, the server socket, as seen in **Fig. 1**. After the listener is added in the latter created epoll, an infinite loop is emerged, representing the running server. Further, an *epoll_event* is created and made to wait for an indefinite time, until an event occurs. When a client request has been made, our server will treat it based on three fundamental cases, which I will talk about next.
<center><img src=server_socket.png allign = "right"  width="300" height="240"></center>
<center><b>Fig 1. - TLPI 56-2: A Pending socket connection</b></center>

## **2. Handle a new connection**
Regarding epoll ([**Epoll section**](#6-epoll)), when the data kept in the *epoll_event* variable has the file descriptor the same as the listener file descriptor, it means that there is a new connection required. So, going further to ```handle_new_connection()``` function, a new connection is created using ```accept()``` socket system call ([**Sockets section**](#5-sockets)). After creation, there are some settings applied to the new socket, using ```setsockopt()```.
```C
#include <sys/socket.h>

/*
 * TCP_NODELAY is disabling Nagle's algorithm and allows data
 * to be sent as soon as it becomes available.
 */
setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes,
                    sizeof(int));

/*---------------------------------------------------------*/

struct linger linger_opt = {
    .l_onoff = 1,  // enable SO_LINGER
    .l_linger = 100  // linger time in seconds
};

/*
 * SO_LINGER blocks the deleting process (when close()) of 
 * a socket until the data is transmitted or the connection
 * has timed out - IBM setsockopt()
 */
setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger_opt,
                    sizeof(linger_opt));
```

Also, there is a need of calling ```fcntl()``` function, which is used for manipulating a file descriptor:
```C
#include <fcntl.h>

/* 
 * F_GETFL - return the file access mode and the file status
 * F_SETFL - set the file status flags to the specified values
 * O_NONBLOCK - setting a non-blocking socket
 */
fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
```

In the end, the **sockfd** will be stored in a wrapper structure called **connection**, where all the necessary data about a connection will be kept. The **conn** variable will actually be **event.data.ptr**.


> A very important part of handling a new creation is thinking of the way of destroying it. So, the connection is going to be closed using ```shutdown()```, specifying the socket fd and the flag **SHUT_RDWR**. This call assures us of the fact that the data is going to be set before the connection is interrupted. The removal of the latter socket will not allow further receptions and transmissions.

## **3. Handle client request**
When the events are set with **EPOLLIN** flag, it means that the associated file is available for read operations, as seen in ([**Epoll section**](#6-epoll)). Further, in ```handle_client_request()```, a message is received through ```receive_message()``` function. Now, let's talk about the latter function and what is its purpose.

In ```receive_message()``` function, there happens an action of reading from the current socket, storing the read data into a buffer called **recv_buffer**. This action is fulfilled by the ```recv()``` function. This actually represents the client request.

Moving further, in ```handle_client_request()```, after receiving the message, a http parser is initialized and used for extrapolating the path of the requested file. If the path is correct and determines a valid file, then the **HTTP_OK_MSG** will be sent, otherwise **HTTP_NOT_FOUND_MSG** will be preferred. If the path is valid, then the **file_sz** from **conn** variable will store the size of the requested file and the **send_buffer** will be populated by the latter http message.

> The given path will determine whether the file is *static* or *dynamic*, using ```determine_file_type()``` function.

## **4. Send a message**
When the events are set with **EPOLLOUT** flag, then the associated file is available for write operations, as seen in 
([**Epoll section**](#6-epoll)). The sending process breaks in two parts.

### **Sending the header**
Our server is going to send the data in one call. So, we need to be sure that the data we are sending will be upright. In order to do that, a loop will emerge in our function. This approach is motivated by the behaviour of ```send()``` function, which is used for transmitting a message from a buffer to a socket. In our case, the buffer is going to be the **send_buffer**, which stores the HTTP message.
```C
int bytes_sent = send(conn->sockfd, conn->send_buffer + 
        total_sent_bytes, conn->send_len - total_sent_bytes, 0);
```

The loop is needed because of the fact that the ```send()``` function may not always send the total number of bytes that are specified on the third parameter. So, the loop will ensure the transmission of the entire message.

### **Sending the file**
This part breaks, again, into two partitions, depending on the type of the file, if it is *static* or *dynamic*.

#### **|| STATIC ||**
When sending a static file, there is a procedure similar to sending the header. This is possible because there is, again, a loop needed. The main difference consists in the function that is using for sending data.
```C
#include <sys/sendfile.h>

/*
Definition of the sendfile() function:

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
*/

int rc = sendfile(conn->sockfd, conn->file, NULL,
                    conn->file_sz);
```
The ```sendfile(...)``` function is now sending data from the file to the socket (as shown in the definition: in_fd -> out_fd). The offset is set as ```NULL```, because the actual function will start reading data from *in_fd* at the file offset, which is updated by the call. Now, the **count** parameter represents the maximum number of bytes that may be copied between the file and socket. This probability of sending less data creates the necessity of having a loop.

#### **|| DYNAMIC ||**
When sending a dyanmic file, the situation takes a big turn. In this case, the **Linux AIO** will be used, an API which allows I/O execution in parallel with the rest of a program's job. For more details, check the course's laboratory. The most important part of this approach is splitting and sending data into chunks. That is the reason why **data_blocks** is used, for storing and sending blocks of data from file to the socket.

In ```iocb_setup()``` function, the fundamental variables that this approach needs are allocated or initialized. In the next lines, there are some explanations about the purpose of each variable:
```C
#include <liabio.h>

struct connection {
        ...
    struct iocb **iocb_r;
    struct iocb **iocb_w;
        ...

    /*
     * iocb structure is used for encapsulating an
     * asynchronous operation
    */
}
```
**iocb_r** refers to the array of *iocb* structures that are used for reading this data blocks (chunks) from file, in order to be sent to the socket.
```C
for (int i = 0; sent_bytes < conn->file_sz; i++) {
            ...
    io_prep_pread(conn->iocb_r[i], conn->file,
        conn->data_blocks[i], readb_sz, sent_bytes);
    io_set_eventfd(conn->iocb_r[i], conn->event_fd);
            ...
}
```
```io_set_eventfd()``` is used for obtaining information about the state of the I/O operations and offers a mechanism of waiting them.

After reading data from the file, we need to send this data to the socket, this procedure is similar to reading from the file, but now the **iocb_w** will be used. So, the previous read data, which is stored in **data_blocks**, will now be sent as shown in the following lines of code:
```C
for (int i = 0; sent_bytes < conn->file_sz; i++) {
            ...
    io_prep_pwrite(conn->iocb_w[i], conn->file,
        conn->data_blocks[i], readb_sz, 0);
    io_set_eventfd(conn->iocb_w[i], conn->event_fd);
            ...
}
```

Now, let's talk about how this mechanism works. As the functions' names suggest, they are just preparing the data that is manipulated (in both read/send cases). In order to execute these operations, there should be used the next functions (after every *io_prep_pread* or *io_prep_pwrite* as shown previously):
```C
// Example for submitting read operation
io_submit(conn->aio_ctx, NUM_OPS, &conn->iocb_r[i])
async_IO_wait(conn);
```
```io_submit()``` function triggers the start of the asynchronous operations that are defined in the array of **iocb** structures given as an argument (it may be *iocb_r* or *iocb_w*). For example, if used after the ```io_prep_write()``` function, it will start the process of sending data from the *data_blocks* buffer to the socket (Fig 2.).
<center><img src=Linux_AIO.jpg allign = "right"  width="350" height="300"></center>
<center><b>Fig 2. - Linux AIO process</b></center>

The ```async_IO_wait()``` wrapper function encapsulates the consacrated ```io_getevents()``` function and uses it in a loop, so that the program will wait for the submission of the operations (using *io_submit()*) and will obtain information about its result. As the function needs a ```aio_context_t``` variable as the first parameter, one will be initialised in ```iocb_setup()``` and will be stored in our **conn** variable.

After finishing the operations, the memory that has been used for this sending process has to be released and the ```aio_context_t``` variable has to be destroyed using ```io_destroy()```.

## **5. Sockets**
**Sockets** allow communication and data exchanging between two processes / applications on the same host or different hosts connected via internet. A socket is created using the following command:
```C
#include <sys/socket.h>

int sockfd = socket(domain, type, protocol);
```

As we see, there are three parameters that we have to provide. When talking about domain, there are more options available:
* UNIX (**AF_UNIX**) - communication between applications on the same computer.
* IPv4 (**AF_INET** / **PF_INET**) - data exchange between processes on hosts connected via IPv4.
* IPv6 (**AF_INET6** / **PF_INET6**) - data exchange between processes on hosts connected via IPv6.

**AF_INET** and **PF_INET** are pretty much the same thing, with the difference that **AF** refers to addresses from the internet (IP addresses), while **PF** suggests protocols (sockets / ports).

Talking about types, there are two types of sockets (stream = **SOCK_STREAM** // datagram = **SOCK_DGRAM**).

**Stream sockets** are reliable and bidirectional, allowing data to be transmitted both ways, usually used for TCP (Transmission Control Protocol). 

**Datagram sockets** are not reliable, nor bidirectional, but message boundaries are preserved, usually used for UDP (User Datagram Protocol).

For the actual project, we will specify *0* as the protocol parameter.

As seen in **Fig. 1**, there are some more system calls provided by the socket API. One of them is represented by the ```bind()```, which is usually called after creating a socket and it is useful for binding the socket to a well-known address, so that the client will be able to locate the socket. After that, ```listen()``` is used for creating a stream socket, that will allow and accept incoming connections from other sockets. Now, talking about accepting, the ```accept()``` system call actually creates a new socket that is connected to the peer socket that performed ```connect()```. The *listenfd* (listening socket) remains open.

## **6. Epoll**
The **epoll** is a mechanism that allows a process to monitor and notify file I/O events. It contrast with **poll**, it provides much better performance when observing a large number of file descriptors. The monitorized file descriptors are kept in a so called *list of interest*.

```C
#include <sys/epoll.h>

int epoll_wait(int epfd, struct epoll_event *events,
                      int maxevents, int timeout);
```
When calling the previous function, we expect to receive ```maxevents``` available events on the file descriptors that are kept in the list of interest.

In order to add / modify / remove file descriptors in this list, ```epoll_ctl(...)``` function has to be called with the suitable events flags. There are more fundamental flags that are used for implementing this project:
```C
#include <sys/epoll.h>

int epoll_ctl(int epfd, int op, int fd,
                struct epoll_event *event);

/*--------------------------------*/

/* op <=> options */
/* Add an entry to the interest list */
EPOLL_CTL_ADD

/*Change old fd settings to the new settings specified inevent*/
EPOLL_CTL_MOD

/*Remove the target file descriptor fd*/
EPOLL_CTL_DEL

/*--------------------------------*/
/*event->events represent a bitmask specifying the following available event types*/

/*the associated file is available for read operations*/
EPOLLIN

/*the associated file is available for write operations*/
EPOLLOUT
```

The data about the event received by ```epoll_wait(...)``` is stored in ```ev.events.ptr```, while the file descriptor is stored in ```ev.events.fd```.
