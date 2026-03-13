#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include "utils.h"

#define PORT "3490"
#define BACKLOG 10
#define MAXEVENTS 64
#define MAX_CONNECTIONS 500

typedef struct {
    int status;
    int sockfd;
} BindResult;

typedef struct {
    int file_fd;
    int is_redirect;
    off_t offset;
    off_t file_size;
} ClientState;

typedef struct {
    int status;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;
} EpollSetup;

void close_client(int fd, int *connections);

BindResult setup_and_bind();

EpollSetup setup_epoll(int sockfd);

int handle_new_connection(int sockfd, int efd, struct epoll_event *event,
                          ClientState client_states[], int active_connections);

void parse_request(int client_fd, int efd, struct epoll_event *event,
                    ClientState client_states[], int *active_connections);

void send_response(int client_fd, int efd, struct epoll_event *event,
                   ClientState client_states[], int *active_connections);

int main() {
    BindResult bind_result = setup_and_bind();
    if (bind_result.status != 0) return bind_result.status;
    int sockfd = bind_result.sockfd;

    EpollSetup epoll_setup = setup_epoll(sockfd);
    if (epoll_setup.status != 0) return epoll_setup.status;

    ClientState client_states[1024];
    memset(client_states, 0, sizeof(client_states));

    int active_connections = 0;

    printf("server: waiting for connections via epoll...\n");

    while(1) {
        int n = epoll_wait(epoll_setup.efd, epoll_setup.events, MAXEVENTS, -1);

        for (int i = 0; i < n; i++) {

            if ((epoll_setup.events[i].events & EPOLLERR) || (epoll_setup.events[i].events & EPOLLHUP)) {
                fprintf(stderr, "epoll error\n");
                if (epoll_setup.events[i].data.fd != sockfd)
                    close_client(epoll_setup.events[i].data.fd, &active_connections);
                else
                    close(epoll_setup.events[i].data.fd);
                continue;
            }

            if (epoll_setup.events[i].data.fd == sockfd) {
                active_connections = handle_new_connection(sockfd, epoll_setup.efd, &epoll_setup.event, client_states, active_connections);
            }

            else if (epoll_setup.events[i].events & EPOLLIN) {
                parse_request(epoll_setup.events[i].data.fd, epoll_setup.efd, &epoll_setup.event, client_states, &active_connections);
            }

            else if (epoll_setup.events[i].events & EPOLLOUT) {
                send_response(epoll_setup.events[i].data.fd, epoll_setup.efd, &epoll_setup.event, client_states, &active_connections);
            }
        }
    }

    free(epoll_setup.events);
    close(sockfd);
    return 0;
}

void close_client(int fd, int *connections) {
    (*connections)--;

	if (*connections < 0) *connections = 0;

    close(fd);
}

BindResult setup_and_bind() {
    BindResult result = {0, -1};
    struct addrinfo hints, *servinfo, *p;
    int sockfd;
    int yes = 1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        result.status = 1;
        return result;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            freeaddrinfo(servinfo);
            result.status = 1;
            return result;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        result.status = 1;
        return result;
    }

    set_nonblocking(sockfd);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        result.status = 1;
        return result;
    }

    result.sockfd = sockfd;
    return result;
}

EpollSetup setup_epoll(int sockfd) {
    EpollSetup result = {0, -1, {0}, NULL};

    result.efd = epoll_create1(0);
    if (result.efd == -1) {
        perror("epoll_create1");
        result.status = 1;
        return result;
    }

    result.events = calloc(MAXEVENTS, sizeof(struct epoll_event));

    result.event.data.fd = sockfd;
    result.event.events = EPOLLIN;
    epoll_ctl(result.efd, EPOLL_CTL_ADD, sockfd, &result.event);

    return result;
}

int handle_new_connection(int sockfd, int efd, struct epoll_event *event,
                          ClientState client_states[], int active_connections) {
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];
    int new_fd;

    while (1) {
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

        if (new_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("server: got connection from %s\n", s);

        if (active_connections >= MAX_CONNECTIONS) {
            char *response = "HTTP/1.1 529 Too Many Requests\r\nConnection: close\r\n\r\n";
            send(new_fd, response, strlen(response), 0);
            close(new_fd);
            continue;
        }

        set_nonblocking(new_fd);

        client_states[new_fd].file_fd = -1;
        client_states[new_fd].is_redirect = 0;
        client_states[new_fd].offset = 0;
        client_states[new_fd].file_size = 0;

        event->data.fd = new_fd;
        event->events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(efd, EPOLL_CTL_ADD, new_fd, event);
        active_connections++;
    }

    return active_connections;
}

void send_response(int client_fd, int efd, struct epoll_event *event,
                   ClientState client_states[], int *active_connections) {
    if (client_states[client_fd].is_redirect) {
        char *redirect = "HTTP/1.1 302 Found\r\nLocation: /\r\nConnection: close\r\n\r\n";
        send(client_fd, redirect, strlen(redirect), 0);
        close_client(client_fd, active_connections);
        return;
    }

    int file_fd = client_states[client_fd].file_fd;
    off_t file_size = client_states[client_fd].file_size;

    if (client_states[client_fd].offset == 0) {
        char headers[256];
        int header_len = snprintf(headers, sizeof(headers),
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: %ld\r\n"
            "Connection: close\r\n\r\n",
            (long)file_size);
        send(client_fd, headers, header_len, 0);
    }

    sendfile(client_fd, file_fd, &client_states[client_fd].offset,
             file_size - client_states[client_fd].offset);

    if (client_states[client_fd].offset >= file_size) {
        close(file_fd);
        close_client(client_fd, active_connections);
    } else {
        printf("send_response: partial send, offset %ld of %ld, re-arming fd %d\n",
               (long)client_states[client_fd].offset, (long)file_size, client_fd);
        event->data.fd = client_fd;
        event->events = EPOLLOUT | EPOLLONESHOT;
        epoll_ctl(efd, EPOLL_CTL_MOD, client_fd, event);
    }
}

void parse_request(int client_fd, int efd, struct epoll_event *event,
                    ClientState client_states[], int *active_connections) {
    char buffer[2048];
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        close_client(client_fd, active_connections);
        return;
    }

    buffer[bytes_read] = '\0';

    char method[16], path[256], protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) == 3) {
        char filename[256];

        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            strcpy(filename, "public/index.html");
        } else {
            snprintf(filename, sizeof(filename), "public/%s", path + 1);
        }

        int file_fd = open(filename, O_RDONLY);
        if (file_fd == -1) {
            client_states[client_fd].is_redirect = 1;
        } else {
            struct stat stat_buf;
            fstat(file_fd, &stat_buf);
            client_states[client_fd].file_fd = file_fd;
            client_states[client_fd].offset = 0;
            client_states[client_fd].file_size = stat_buf.st_size;
        }

        event->data.fd = client_fd;
        event->events = EPOLLOUT | EPOLLONESHOT;
        epoll_ctl(efd, EPOLL_CTL_MOD, client_fd, event);
    } else {
        close_client(client_fd, active_connections);
    }
}
