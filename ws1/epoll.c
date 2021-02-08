/*
  Epoll example
*/

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <netdb.h>
#include <errno.h>
#include <sys/epoll.h>

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64
#define MAX_EVENTS 5
#define READ_SIZE 10

const int true = 1;
const int false = 0;

typedef struct {
  int want_read;
  int want_write;
} fd_status_t;

const fd_status_t fd_status_R = {.want_read = true, .want_write = false};
const fd_status_t fd_status_W = {.want_read = false, .want_write = true};
const fd_status_t fd_status_RW = {.want_read = true, .want_write = true};
const fd_status_t fd_status_NORW = {.want_read = false, .want_write = false};

char buf[BUFSIZE];

void perror_die(const char *s) {
  perror(s);
  exit(EXIT_FAILURE);
}

int listen_socket(int portnum) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0); // domain(ipv4), type(two-way), protocol(?)
  if (sockfd < 0) {
    perror_die("opening socket");
  }

  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { // socket(file_descriptor), level(), option_name(), option_value(), socklen_t()
    perror_die("on setsockopt");
  }
  
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portnum);

  if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) { // socket(file_descriptor), struct_addr(), addr_len()
    perror_die("on bind");
  }

  if (listen(sockfd, N_BACKLOG) < 0) {
    perror_die("on listen");
  }

  return sockfd;
}

void setnonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    perror("on fcntl");
    exit(EXIT_FAILURE);
  }

  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("on fcntl");
    exit(EXIT_FAILURE);
  }
}

int main() {
  struct epoll_event event, events[MAX_EVENTS];
  int listen_sock, conn_sock, event_count;
  struct sockaddr addr;
  socklen_t addrlen;
  fd_status_t status;
  listen_sock = listen_socket(PORT);

  int epoll_fd = epoll_create1(0);

  if (epoll_fd == -1) {
    fprintf(stderr, "Failed to create epoll file discriptor\n");
    return 1;
  }

  event.events = EPOLLIN;
  event.data.fd = listen_sock;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event)) {
    fprintf(stderr, "Failed to add file descriptor to epoll\n");
    close(epoll_fd);
    return 1;
  }

  for(;;) {
    printf("\nPolling for input...\n");
    event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < event_count; i++) {
      if (events[i].data.fd == listen_sock) {
        conn_sock = accept(listen_sock, (struct sockaddr *) &addr, &addrlen);
        if (conn_sock < 0) {
          perror("accept");
          exit(EXIT_FAILURE);
        }
        setnonblocking(conn_sock);
        status = fd_status_W;
        event.events = EPOLLIN;
        event.data.fd = conn_sock;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_sock, &event) == -1) {
          perror("epoll ctl: conn_sock");
          exit(EXIT_FAILURE);
        }
      } else {
        if (events[i].events & EPOLLIN) {
          int fd = events[i].data.fd;
          int nbytes = recv(fd, buf, sizeof buf, 0);
          if (nbytes == 0) {
            status = fd_status_NORW;
          } else {
            status = fd_status_W;
          }

          event.data.fd = fd;
          event.events = 0;
          if (status.want_read) {
            event.events |= EPOLLIN;
          }
          if (status.want_write) {
            event.events |= EPOLLOUT;
          }


          if (event.events == 0) {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
              perror_die("epoll_ctl");
            }
          } else {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
              perror_die("epoll_ctl");
            }
          }
        } else if (events[i].events & EPOLLOUT) {
          int fd = events[i].data.fd;

          send(fd, "res\n", 4, 0);

          status = fd_status_R;

          event.data.fd = fd;
          event.events = 0;
          if (status.want_read) {
            event.events |= EPOLLIN;
          } 
          if (status.want_write) {
            event.events |= EPOLLOUT;
          }
          if (event.events == 0) {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
              perror_die("epoll_ctl");
            }
          } else {
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
              perror_die("epoll_ctl");
            }
          }
        }
      }
    }
  }

  if (close(epoll_fd)) {
    fprintf(stderr, "Failed to close epoll file descriptor\n");
    return 1;
  }

  return 0;
}