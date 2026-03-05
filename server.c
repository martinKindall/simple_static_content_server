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

#define PORT "3490"
#define BACKLOG 10
#define MAXEVENTS 64

// Helper function to make a socket non-blocking
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main() {
    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    // 1. Standard Socket Setup (Same as before)
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
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
        exit(1);
    }

    // Make the listening socket non-blocking
    set_nonblocking(sockfd);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

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

	struct {
        int file_fd;
        int is_redirect;
    } client_states[1024];

    memset(client_states, 0, sizeof(client_states));

    printf("server: waiting for connections via epoll...\n");

    // 3. The Event Loop
    while(1) {
        int n = epoll_wait(efd, events, MAXEVENTS, -1);
        
        for (int i = 0; i < n; i++) {
            
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                fprintf(stderr, "epoll error\n");
                close(events[i].data.fd);
                continue;
            }

            // INCOMING CONNECTION ON LISTENING SOCKET
            if (events[i].data.fd == sockfd) {
                while(1) {
                    sin_size = sizeof their_addr;
                    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
                    
                    if (new_fd == -1) {
                        // EAGAIN means we have accepted all pending connections
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; 
                        }
                        perror("accept");
                        break;
                    }

                    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
                    printf("server: got connection from %s\n", s);

                    set_nonblocking(new_fd);

					// Initialize state for this new client
                    client_states[new_fd].file_fd = -1;
                    client_states[new_fd].is_redirect = 0;

                    // Add to epoll, waiting to READ the request first (EPOLLIN)
                    event.data.fd = new_fd;
                    event.events = EPOLLIN | EPOLLONESHOT;
                    epoll_ctl(efd, EPOLL_CTL_ADD, new_fd, &event);
                }
            }

            else if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                char buffer[2048];
                
                int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

                if (bytes_read <= 0) {
                    close(client_fd);
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
                    close(client_fd);
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

                close(client_fd);
            }
        }
    }

    free(events);
    close(sockfd);
    return 0;
}

