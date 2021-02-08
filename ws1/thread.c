/*
  Thread example 
*/
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64
#define MAX_EVENTS 5
#define READ_SIZE 10

typedef struct { int sockfd; } thread_config_t;

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

void serve_connection(int sockfd) {
  while(1) {
    uint8_t buf[1024];
    int len = recv(sockfd, buf, sizeof buf, 0);
    if (len == 0) break;
    buf[len] = '\0';
    printf("%s\n", buf);

    send(sockfd, "received\n", 9, 0);
  }
  close(sockfd);
}

void* server_thread(void* arg) {
  thread_config_t* config = (thread_config_t*)arg;
  int sockfd = config->sockfd;
  free(config);

  unsigned long id = (unsigned long)pthread_self();
  printf("Created %lu\n", id);
  serve_connection(sockfd);
  return 0;
}

int main() {
  int listen_sock = listen_socket(PORT);

  while (1) {
    struct sockaddr addr;
    socklen_t addrlen;

    int peer_sock = accept(listen_sock, (struct sockaddr*)&addr, &addrlen);

    pthread_t the_thread;

    thread_config_t* config = (thread_config_t *)malloc(sizeof(*config));
    config->sockfd = peer_sock;
    pthread_create(&the_thread, NULL, server_thread, config);
    pthread_detach(the_thread);
  }
}