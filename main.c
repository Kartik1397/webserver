#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64

uint8_t buf[BUFSIZE];
char method[BUFSIZE];
char uri[BUFSIZE];
char version[BUFSIZE];


void perror_die(const char *s) {
  perror(s);
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
  FILE *stream = fdopen(sockfd, "r+");

  if (stream == NULL) {
    perror_die("on fdopen");
  }

  fgets(buf, BUFSIZE, stream);
  printf("%s", buf);
  sscanf(buf, "%s %s %s\n", method, uri, version);

  fgets(buf, BUFSIZE, stream);
  printf("%s", buf);
  while (strcmp(buf, "\r\n")) {
    fgets(buf, BUFSIZE, stream);
    printf("%s", buf);
  }

  // Request Line
  fprintf(stream, "HTTP/1.1 200 OK\r\n");

  // Header
  fprintf(stream, "Server: KTK's Web Server\r\n");
  fprintf(stream, "Content-length: 12\r\n");
  fprintf(stream, "Content-type: text/html\r\n");
  fprintf(stream, "\r\n");
  
  // Body
  fprintf(stream, "Hello, World!");
  fprintf(stream, "\r\n");
  fflush(stream);
  fclose(stream);
  close(sockfd);
}

int main(int argc, const char *argv[]) {
  const int server_sockfd = listen_socket(PORT);

  while (1) {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    int peer_sockfd = accept(server_sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
    if (peer_sockfd < 0) {
      perror_die("on accept");
    }
    serve_connection(peer_sockfd);
    printf("peer done\n");
  }

  printf("END\n");
}