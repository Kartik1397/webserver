#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#define PORT 8080
#define N_BACKLOG 64

void perror_die(const char *s) {
  char *err;
  sprintf(err, "ERROR: %s\n", s);
  perror(err);
  exit(EXIT_FAILURE);
}

int listen_socket(int portnum) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror_die("opening socket");
  }

  int opt = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror_die("on setsockopt");
  }
  
  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portnum);

  if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror_die("on bind");
  }

  if (listen(sockfd, N_BACKLOG) < 0) {
    perror_die("on listen");
  }

  return sockfd;
}

void serve_connection(int sockfd) {
  while (1) {
    uint8_t buf[1024];
    int len = recv(sockfd, buf, sizeof buf, 0);
    buf[len] = '\0';
    printf("%s\n", buf);
    if (send(sockfd, "received\n", 9, 0) < 1) {
      perror_die("on send");
    }
  }

}

int main(int argc, const char *argv[]) {
  const int sockfd = listen_socket(PORT);

  while (1) {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    int newsockfd = accept(sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
    if (newsockfd < 0) {
      perror_die("on accept");
    }

    serve_connection(newsockfd);
  }

  printf("END\n");
}