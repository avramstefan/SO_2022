#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <netinet/tcp.h>
#include <libaio.h>
#include <sys/eventfd.h>

#include "util.h"
#include "debug.h"
#include "sock_util.h"
#include "w_epoll.h"
#include "aws.h"
#include "http_parser.h"

#define ECHO_LISTEN_PORT		42424
#define NUM_OPS 1
#define MIN(a,b) (((a)<(b))?(a):(b))

/* HTTP PARSER */
http_parser request_parser;

/* REQUEST PATH */
char path[BUFSIZ];

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

/*
 * Callback is invoked by HTTP request parser when parsing request path.
 * Request path is stored in global request_path variable.
 */

static int on_path_cb(http_parser *p, const char *buf, size_t len)
{
	assert(p == &request_parser);
	memcpy(path, buf, len);

	return 0;
}

/* Use mostly null settings except for on_path callback. */
static http_parser_settings settings = {
	/* on_message_begin */ 0,
	/* on_header_field */ 0,
	/* on_header_value */ 0,
	/* on_path */ on_path_cb,
	/* on_url */ 0,
	/* on_fragment */ 0,
	/* on_query_string */ 0,
	/* on_body */ 0,
	/* on_headers_complete */ 0,
	/* on_message_complete */ 0
};

enum connection_state {
	STATE_DATA_RECEIVED,
	STATE_DATA_SENT,
	STATE_CONNECTION_CLOSED
};

/* structure acting as a connection handler */
struct connection {
	int sockfd;
	/* buffers used for receiving messages and then echoing them back */
	char recv_buffer[BUFSIZ];
	size_t recv_len;
	char send_buffer[BUFSIZ];
	size_t send_len;
	enum connection_state state;

	int event_fd;
	io_context_t aio_ctx;
	struct iocb **iocb_r;
	struct iocb **iocb_w;
	char **data_blocks;
	struct io_event *events;

	char path[BUFSIZ];
	int file;
	int file_sz;
	short file_type;
	short header_is_written;
};

struct linger linger_opt = {
    .l_onoff = 1,  // enable SO_LINGER
    .l_linger = 100  // linger time in seconds
};

/*
 * Initialize connection structure on given socket.
 */

static struct connection *connection_create(int sockfd)
{
	struct connection *conn = malloc(sizeof(*conn));

	DIE(conn == NULL, "malloc");

	conn->sockfd = sockfd;
	conn->recv_len = 0;
	conn->header_is_written = 0;
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);

	return conn;
}

/*
 * Remove connection handler.
 */

static void connection_remove(struct connection *conn)
{
	shutdown(conn->sockfd, SHUT_RDWR);

	if (conn->file != -1)
		close(conn->file);

	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

/*
 * Handle a new connection request on the server socket.
 */

static void handle_new_connection(void)
{
	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;

	/* accept new connection */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");

	dlog(LOG_ERR, "Accepted connection from: %s:%d\n",
		inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

	int yes = 1;
	rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &yes,
                    sizeof(int));
	rc = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

	/* instantiate new connection handler */
	conn = connection_create(sockfd);

	/* add socket to epoll */
	rc = w_epoll_add_ptr_in(epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_in");
}

/*
 * Receive message on socket.
 * Store message in recv_buffer in struct connection.
 */

static enum connection_state receive_message(struct connection *conn)
{
	ssize_t bytes_recv;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	bytes_recv = recv(conn->sockfd, conn->recv_buffer, BUFSIZ, 0);
	if (bytes_recv < 0) {		/* error in communication */
		dlog(LOG_ERR, "Error in communication from: %s\n", abuffer);
		goto remove_connection;
	}
	if (bytes_recv == 0) {		/* connection closed */
		dlog(LOG_INFO, "Connection closed from: %s\n", abuffer);
		goto remove_connection;
	}

	dlog(LOG_DEBUG, "Received message from: %s\n", abuffer);

	printf("--\n%s--\n", conn->recv_buffer);

	conn->recv_len = bytes_recv;
	conn->state = STATE_DATA_RECEIVED;

	return STATE_DATA_RECEIVED;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	connection_remove(conn);

	return STATE_CONNECTION_CLOSED;
}

off_t get_file_sz(struct connection *conn) {
	off_t s = lseek(conn->file, 0, SEEK_CUR);
	int file_size = lseek(conn->file, 0, SEEK_END);
	lseek(conn->file, s, SEEK_SET);

	return file_size;
}

void send_static_file(struct connection *conn) {
	off_t sent_bytes = 0;

	while (sent_bytes < conn->file_sz) {
		int rc = sendfile(conn->sockfd, conn->file, NULL, conn->file_sz);
		DIE(rc < 0, "Error sendfile.\n");
		sent_bytes += rc;
	}

	conn->state = STATE_DATA_SENT;
}

void io_free(struct connection *conn) {
	int readb_sz = MIN(conn->file_sz, BUFSIZ);

	for (int i = 0; i < conn->file_sz / readb_sz; i++) {
		free(conn->iocb_r[i]);
		free(conn->iocb_w[i]);
		free(conn->data_blocks[i]);
	}

	free(conn->iocb_r);
	free(conn->iocb_w);
	free(conn->events);
	free(conn->data_blocks);

	io_destroy(conn->aio_ctx);
}

void iocb_setup(struct connection *conn) {
	conn->event_fd = eventfd(0, 0);
	DIE(conn->event_fd < 0, "Invalid eventfd!\n");

	int readb_sz = MIN(conn->file_sz, BUFSIZ);

	conn->iocb_r = (struct iocb **)malloc(readb_sz * sizeof(struct iocb *));
	for (int i = 0; i < conn->file_sz / readb_sz; i++) {
		conn->iocb_r[i] = (struct iocb *)malloc(sizeof(struct iocb));
		memset(conn->iocb_r[i], 0, sizeof(struct iocb));
	}

	conn->iocb_w = (struct iocb **)malloc(readb_sz * sizeof(struct iocb *));
	for (int i = 0; i < conn->file_sz / readb_sz; i++) {
		conn->iocb_w[i] = (struct iocb *)malloc(sizeof(struct iocb));
		memset(conn->iocb_w[i], 0, sizeof(struct iocb));
	}

	conn->data_blocks = (char **)malloc(readb_sz * sizeof(char *));
	for (int i = 0; i < conn->file_sz / readb_sz; i++)
		conn->data_blocks[i] = malloc(BUFSIZ * sizeof(char));

	conn->events = (struct io_event *)malloc(sizeof(struct io_event));
	DIE(conn->events == NULL, "Error while allocating memory for events.\n");

	conn->aio_ctx = 0;
	DIE(io_setup(NUM_OPS, &conn->aio_ctx) < 0, "Invalid io_setup.\n");
}

void async_IO_wait(struct connection *conn) {
	int no_events_read = 0;
	while (no_events_read < NUM_OPS) {
		no_events_read = io_getevents(conn->aio_ctx, NUM_OPS, NUM_OPS,
									conn->events, NULL);
		DIE(no_events_read < 0, "Invalid io_getevents\n");
	}
}

void send_dynamic_file(struct connection *conn) {
	iocb_setup(conn);

	int readb_sz = MIN(conn->file_sz, BUFSIZ);

	int sent_bytes = 0;
	for (int i = 0; sent_bytes < conn->file_sz; i++) {
		io_prep_pread(conn->iocb_r[i], conn->file, conn->data_blocks[i], readb_sz, sent_bytes);
		io_set_eventfd(conn->iocb_r[i], conn->event_fd);
	
		DIE(io_submit(conn->aio_ctx, NUM_OPS, &conn->iocb_r[i]) < 0, "Invalid io_submit.\n");
		async_IO_wait(conn);
	
		io_prep_pwrite(conn->iocb_w[i], conn->sockfd, conn->data_blocks[i], readb_sz, 0);
		io_set_eventfd(conn->iocb_w[i], conn->event_fd);

		DIE(io_submit(conn->aio_ctx, NUM_OPS, &conn->iocb_w[i]) < 0, "Invalid io_submit.\n");
		async_IO_wait(conn);

		sent_bytes += readb_sz;
		memset(conn->events, 0, sizeof(struct io_event));
	}

	io_free(conn);
	conn->state = STATE_DATA_SENT;
}

void send_file_by_type(struct connection *conn) {
	if (conn->file_type == STATIC) {		 /* it is STATIC */
		send_static_file(conn);
	} else if (conn->file_type == DYNAMIC) { /* it is DYNAMIC */
		send_dynamic_file(conn);
	}
}

/*
 * Send message on socket.
 * Store message in send_buffer in struct connection.
 */

static enum connection_state send_message(struct connection *conn)
{
	if (conn->state == STATE_CONNECTION_CLOSED)
		return STATE_CONNECTION_CLOSED;

	ssize_t bytes_sent;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0) {
		ERR("get_peer_address");
		goto remove_connection;
	}

	/*
	 * Send data from send_buffer to the latter socket, to populate the answer
	 * with the HTTP header
	 */
	ssize_t total_sent_bytes = 0;
	while (!conn->header_is_written && total_sent_bytes < conn->send_len) {
		bytes_sent = send(conn->sockfd, conn->send_buffer + total_sent_bytes,
							conn->send_len - total_sent_bytes, 0);
		if (bytes_sent < 0) {		/* error in communication */
			dlog(LOG_ERR, "Error in communication to %s\n", abuffer);
			goto remove_connection;
		}

		if (bytes_sent == 0) {		/* connection closed */
			dlog(LOG_INFO, "Connection closed to %s\n", abuffer);
			goto remove_connection;
		}

		total_sent_bytes += bytes_sent;
	}
	conn->header_is_written = 1;

	dlog(LOG_DEBUG, "Sending message to %s\n", abuffer);

	printf("--\n%s--\n", conn->send_buffer);

	/* Send the file - the effective content of the file reffered as conn->file */
	if (conn->header_is_written && conn->file != FILE_NOT_FOUND)
		send_file_by_type(conn);

	if (conn->state == STATE_DATA_SENT)
		goto remove_connection;

	/* all done - remove out notification */
	rc = w_epoll_update_ptr_in(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_update_ptr_in");

	conn->state = STATE_DATA_SENT;

	return STATE_DATA_SENT;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");

	/* remove current connection */
	connection_remove(conn);

	return STATE_CONNECTION_CLOSED;
}

void determine_file_type(struct connection* conn) {
	char temp[BUFSIZ];
	char delim = '/';

	memmove(temp, path + 1, strlen(path) - 1);
	temp[strlen(temp)] = '\0';

	char *p = strtok(temp, &delim);
	if (!p) {
		conn->file_type = FILE_NOT_FOUND;
		return;
	}

	if (!strcmp(p, "static")) {
		conn->file_type = STATIC;
	} else if (!strcmp(p, "dynamic")) {
		conn->file_type = DYNAMIC;
	} else {
		conn->file_type = FILE_NOT_FOUND;
	}
}

void set_connection_path_and_file(struct connection* conn) {
	if (!strlen(path))
		return;

	memset(conn->path, 0, BUFSIZ);
	memmove(conn->path, AWS_DOCUMENT_ROOT, strlen(AWS_DOCUMENT_ROOT));
	memmove(conn->path + strlen(AWS_DOCUMENT_ROOT), path + 1, strlen(path) - 1);

	if (strstr(path, ".") == NULL)
		strcat(conn->path, ".");
	strcat(conn->path, "dat");

	conn->file = open(conn->path, O_RDWR);
	determine_file_type(conn);
}

void set_connection_send_buffer(struct connection *conn, int file_not_found) {
	memset(conn->send_buffer, 0, BUFSIZ);

	if (!file_not_found) {
		conn->send_len = strlen(HTTP_OK_MSG);
		memmove(conn->send_buffer, HTTP_OK_MSG, conn->send_len);
		conn->send_buffer[conn->send_len] = '\0';
		conn->file_sz = get_file_sz(conn);
	} else {
		conn->send_len = strlen(HTTP_NOT_FOUND_MSG);
		memmove(conn->send_buffer, HTTP_NOT_FOUND_MSG, conn->send_len);
		conn->send_buffer[conn->send_len] = '\0';
		conn->file_sz = 0;
	}
}

/*
 * Handle a client request on a client connection.
 */

static void handle_client_request(struct connection *conn)
{
	int rc, nparsed;
	enum connection_state ret_state;
	ret_state = receive_message(conn);
	if (ret_state == STATE_CONNECTION_CLOSED)
		return;

	// Initialize the http_parser 
	http_parser_init(&request_parser, HTTP_REQUEST);
	memset(path, 0, BUFSIZ);

	nparsed = http_parser_execute(&request_parser, &settings, conn->recv_buffer, conn->recv_len);
	dlog(LOG_INFO, "Completed request\tpath: %s\tbytes: %d\n", path, nparsed);

	set_connection_path_and_file(conn);

	if (conn->file == FILE_NOT_FOUND)
		set_connection_send_buffer(conn, FILE_NOT_FOUND);
	else
		set_connection_send_buffer(conn, FILE_FOUND);

	/* add socket to epoll for out events */
	rc = w_epoll_update_ptr_inout(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_add_ptr_inout");
}

int main(void)
{
	int rc;

	/* init multiplexing */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

	/* create server socket */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT,
		DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");
	
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");

	dlog(LOG_INFO, "Server waiting for connections on port %d\n",
		AWS_LISTEN_PORT);
	
	/* server main loop */
	while (1) {
		struct epoll_event rev;
			
		/* wait for events */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");
	
		/*
		 * switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */

		if (rev.data.fd == listenfd) {
			dlog(LOG_DEBUG, "New connection\n");
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			if (rev.events & EPOLLIN) {
				dlog(LOG_DEBUG, "New message\n");
				handle_client_request(rev.data.ptr);
			}
			if (rev.events & EPOLLOUT) {
				dlog(LOG_DEBUG, "Ready to send message\n");
				send_message(rev.data.ptr);
			}
		}
	}

	return 0;
}
