#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#define MAX_EVENTS 1024 // Max pending events to handle per epoll_wait call
#define DEFAULT_HT_SIZE 64

// I'd like to have a hash table int -> (fd struct/parse_func)...
// It'd be nice to reuse the hash table I made for group_manager but 
// that's map string -> void*.

typedef void (*event_callback_t)(int, void*);
typedef void (*handler_t)(char*, size_t);

struct fd_data {
	int fd;
	void *context; // State maintained by cb if needed
	event_callback_t cb_func;
	struct fd_callback *next;
};

static handler_t handler = NULL;

// Global event structure
static struct epoll_event ev, events[MAX_EVENTS];
// Global fds
static int epollfd, listenfd;
// Simple hash table
struct fd_data *hashtable[DEFAULT_HT_SIZE] = {0};

static void 
insert_hashtable(struct fd_data *fdata) {
	struct fd_data *p, *prev;
	int idx = fdcb->fd % DEFAULT_HT_SIZE;

	p = hashtable[idx];
	if(p == NULL) {
		hashtable[idx] = fdata;
	} else {
		while(p != NULL) {
			prev = p;
			p = p->next;
		}
		prev->next = fdata;
	}
}

static struct fd_data *
fetch_hashtable(int fd)
{
	struct fd_data *p;
	int idx = fd % DEFAULT_HT_SIZE;

	p = hashtable[idx];
	while(p != NULL && p->fd != fd)
		p = p->next;
	return p;
}

static struct fd_data *
remove_hashtable(int fd)
{
	struct fd_data *p, *prev = NULL;
	int idx = fd % DEFAULT_HT_SIZE;
	
	p = hashtable[idx];
	while(p != NULL && p->fd != fd) {
		prev = p;
		p = p->next;
	}

	if(p == NULL)
		return NULL;

	if(prev == NULL) {
		hashtable[idx] = p->next;
	} else {
		prev->next = p->next;
	}
	return p;
}
static void
handle_message(int sockfd, void *context)
{
	char *msg = NULL;
	size_t buf_sz = 0;
	uint32_t msg_sz = 0;
	uint32_t remaining = 0;
	int ret;

	while(1) {
		// Get header
		ret = recv(sockfd, &msg_sz, sizeof(uint32_t), 0);
		if(ret == 0) {
			// Client closed connection
			fprintf(stderr, "Client closed connection");
			close(sockfd);
			break;
		} else if(ret == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			perror("read");
			close(sockfd);
			break;
		}

		msg_sz = ntohl(msg_sz);
		remaining = msg_sz;
		if((msg = malloc(msg_sz)) == NULL)
			break;

		// For now we're going to block until we read msg_sz bytes
		// off this socket. However in the future we may save the 
		// state in *context so we can return instead of block and
		// handle the rest on the next event trigger
		while(remaining) {
			ret = recv(sockfd, msg, remaining, 0);
			if(ret == 0) {
				// Client closed connection
				fprintf(stderr, "Client closed connection");
				close(sockfd);
				free(msg);
				break;
			} else if(ret == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
				perror("read");
				close(sockfd);
				free(msg);
				break;
			}
			remaining -= ret;
		}

		// Invoke handler with message
		handler(msg, msg_sz);
		free(msg);
	}
}

static void 
accept_cb(int sockfd, void *context)
{
	struct sockaddr_in client_addr;
	struct fd_data *fdata;
	int client_addr_len = sizeof(client_addr);
	int client_fd;

	if((client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
		perror("accept");
		return;
	}

	if(set_nonblocking(client_fd) == -1) {
		close(client_fd);
		return;
	}

	// Construct fd_data 
	if((fdata = malloc(sizeof(struct fd_data))) == NULL) {
		close(client_fd);
		return;
	}
	fdata->fd = client_fd;
	fdata->cb_func = &handle_message;
	// For now no context, but in the future we might add a struct
	// for storing data between events if we don't get a full message
	// in one read and need to know how much more to expect between events
	fdata->context = NULL; 

	insert_hashtable(fdata);

	ev.events = EPOLLIN | EPOLLET;
	ev.data.fd = client_fd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
		perror("epoll_ctl: client_fd");
		close(client_fd);
		free(fdata);
		return;
	}

}

static int 
set_nonblocking(int fd)
{
	int sock_flags = fcntl(fd, F_GETFL, 0);
	if(sock_flags < 0) {
		perror("fcntl: GETFL");
		return -1;
	}

	if(fcntl(fd, F_SETFL, sock_flags | O_NONBLOCK)) {
		perror("fcntl: SETFL");
		return -1;
	}

	sock_flags = 1;
	if(setsockopt(fd, IPROTO_TCP, TCP_NODELAY, (char *)&sock_flags, sizeof(int)))
		perror("TCP_NODELAY");
	if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &sock_flags, sizeof(int)))
		perror("SO_KEEPALIVE");

	return 0;
}

int 
init_networking(int port, handler_t h_func) // For now a raw int port, eventually a configuration object
{
	struct addrinfo hints, *servinfo, *p;
	struct fd_data *fdata;
	int rv, optval = 1;

	// For now only a single handler function. In the future
	// perhaps allow a series of handler which will be chained
	handler = h_func; 

	// Set up the epoll structure, bind the listen socket etc.
	if((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		return -1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}

	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}

		if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) == -1) {
			perror("setsockopt");
			return -1;
		}

		if(bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(listenfd);
			perror("bind");
			continue;
		}
		break;
	}

	freeaddrinfo(servinfo);

	if(p == NULL) {
		fprintf(stderr, "failed to bind\n");
		return -1;
	}

	if(set_nonblocking(listenfd) == -1) {
		fprintf(stderr, "Failed to set nonblocking\n");
		close(listenfd);
		return -1;
	}

	if(listen(listenfd, BACKLOG) == -1) {
		perror("listen");
		close(listenfd);
		return -1;
	}

	// Build fd struct, add to epoll and then return
	if((fdata = malloc(sizeof(struct fd_data))) == NULL) {
		fprintf(stderr, "malloc");
		close(listenfd);
		return -1;
	}

	fdata->fd = listenfd;
	fdata->cb_func = &accept_cb;
	fdata->context = NULL; // No state to keep

	insert_hashtable(fdata);
	
	ev.events = EPOLLIN; // listen socket is level triggered, NOT edge triggered
	ev.data.fd = listenfd;
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
		perror("epoll_ctl: listenfd");
		close(listenfd);
		return -1;
	}

	return 0;
}

void 
start_networking_loop()
{
	int numfds, idx;
	struct fd_data *fdata;
	// I need to set up some signal handlers soon
	while(1) {
		numfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		if(numfds == -1) {
			perror("epoll_wait");
		}

		for(idx = 0; idx < numfds; ++idx) {
			fdata = fetch_hashtable(events[idx].data.fd);
			if(fdata == NULL) {
				fprintf(stderr, "Failed to retrieve fd data");
				continue;
			}
			fdata->cb_func(fdata->fd, fdata->context);
		}
	}
}
