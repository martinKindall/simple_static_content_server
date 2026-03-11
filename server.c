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

void close_client(int fd, int *connections) {
    (*connections)--;

	if (*connections < 0) *connections = 0;

    close(fd);
}

typedef struct {
    int status;
    int sockfd;
} BindResult;

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

typedef struct {
    int file_fd;
    int is_redirect;
} ClientState;

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

        event->data.fd = new_fd;
        event->events = EPOLLIN | EPOLLONESHOT;
        epoll_ctl(efd, EPOLL_CTL_ADD, new_fd, event);
        active_connections++;
    }

    return active_connections;
}

int main() {
    BindResult bind_result = setup_and_bind();
    if (bind_result.status != 0) return bind_result.status;
    int sockfd = bind_result.sockfd;

    // 2. Setup epoll
    int efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create1");
        exit(1);
    }

    struct epoll_event event;
    struct epoll_event *events = calloc(MAXEVENTS, sizeof event);

    // Register the listening socket to monitor for incoming connections (EPOLLIN)
    event.data.fd = sockfd;
    event.events = EPOLLIN; 
    epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &event);

    ClientState client_states[1024];

    memset(client_states, 0, sizeof(client_states));

    int active_connections = 0;

    printf("server: waiting for connections via epoll...\n");

    // 3. The Event Loop
    while(1) {
        int n = epoll_wait(efd, events, MAXEVENTS, -1);
        
        for (int i = 0; i < n; i++) {
            
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                fprintf(stderr, "epoll error\n");
                if (events[i].data.fd != sockfd)
                    close_client(events[i].data.fd, &active_connections);
                else
                    close(events[i].data.fd);
                continue;
            }

            // INCOMING CONNECTION ON LISTENING SOCKET
            if (events[i].data.fd == sockfd) {
                active_connections = handle_new_connection(sockfd, efd, &event, client_states, active_connections);
            }

            else if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                char buffer[2048];
                
                int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

                if (bytes_read <= 0) {
                    close_client(client_fd, &active_connections);
                    continue;
                }
                
                buffer[bytes_read] = '\0'; 

                // Parse the request line (e.g., "GET /about.html HTTP/1.1")
                char method[16], path[256], protocol[16];
                if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) == 3) {
                    
                    char filename[256];
                    
                    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
                        strcpy(filename, "index.html");
                    } 
                    else {
                        // Skip the leading slash (e.g., "/about.html" -> "about.html")
                        strcpy(filename, path + 1);
                    }

                    int file_fd = open(filename, O_RDONLY);
                    if (file_fd == -1) {
                        // File doesn't exist. Mark this client for a redirect.
                        client_states[client_fd].is_redirect = 1;
                    } else {
                        client_states[client_fd].file_fd = file_fd;
                    }

                    event.data.fd = client_fd;
                    event.events = EPOLLOUT | EPOLLONESHOT;
                    epoll_ctl(efd, EPOLL_CTL_MOD, client_fd, &event);
                    
                } else {
                    close_client(client_fd, &active_connections);
                }
            }	
            
            else if (events[i].events & EPOLLOUT) {
                int client_fd = events[i].data.fd;

                if (client_states[client_fd].is_redirect) {
                    char *redirect = "HTTP/1.1 302 Found\r\nLocation: /\r\nConnection: close\r\n\r\n";
                    send(client_fd, redirect, strlen(redirect), 0);
                }
                else {
                    int file_fd = client_states[client_fd].file_fd;

                    struct stat stat_buf;
                    fstat(file_fd, &stat_buf);

                    char headers[256];
                    int header_len = snprintf(headers, sizeof(headers),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: %ld\r\n"
                        "Connection: close\r\n\r\n",
                        (long)stat_buf.st_size);

                    send(client_fd, headers, header_len, 0);

                    off_t offset = 0;
                    sendfile(client_fd, file_fd, &offset, stat_buf.st_size);

                    close(file_fd);
                }

                close_client(client_fd, &active_connections);
            }
        }
    }

    free(events);
    close(sockfd);
    return 0;
}

