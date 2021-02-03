#include <bits/stdc++.h>
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
#include <netdb.h>

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64

char buf[BUFSIZE];
char method[BUFSIZE];
char uri[BUFSIZE];
char version[BUFSIZE];
char filename[BUFSIZE];

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

class HttpResponse {
public:
  int status_code;
  std::map<int, const char*> status_msg_mp = {
    { 200, "OK" },
    { 404, "Not Found"},
  };
  std::string header;
  std::string body;
  char *response;
  HttpResponse(int status, char *header, char *body);
  char *to_string();
private:
};

HttpResponse::HttpResponse(int status, char *header, char *body, int body_len) {
  sprintf(response, "HTTP/1.1 %d %s\r\n", status_code, status_msg_mp[status_code]);

  // Header
  sprintf(response, "Server: KTK's Web Server\r\n");
  sprintf(response, "Content-length: %ld\r\n", body_len);
  sprintf(response, "Content-type: text/html\r\n");
  sprintf(response, "\r\n");
}

char *HttpResponse::to_string() {

}

class HttpRequest {
public:
private:
};

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

  strcpy(filename, ".");
  strcat(filename, uri);

  if (strcmp(uri, "/") == 0) {
    strcpy(filename, "index.html");
  }

  printf("%s\n", filename);

  struct stat stat_buf;
  if (stat(filename, &stat_buf) < 0) {
    printf("404 not found");
    fclose(stream);
    return;
  }  
  
  // Request Line
  fprintf(stream, "HTTP/1.1 200 OK\r\n");

  // Header
  fprintf(stream, "Server: KTK's Web Server\r\n");
  fprintf(stream, "Content-length: %ld\r\n", stat_buf.st_size);
  fprintf(stream, "Content-type: text/html\r\n");
  fprintf(stream, "\r\n");
  fflush(stream);
  
  // Body
  int fd = open(filename, O_RDONLY);
  char *p = (char *)mmap(0, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  fwrite(p, 1, stat_buf.st_size, stream);
  munmap(p, stat_buf.st_size);

  // fprintf(stream, "Hello, World!");
  // fprintf(stream, "\r\n");
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