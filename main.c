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
#include <errno.h>

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64

uint8_t buf[BUFSIZE];
char method[BUFSIZE];
char uri[BUFSIZE];
char version[BUFSIZE];
char filename[BUFSIZE];
char params[BUFSIZE];

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

void write_header(
  FILE* stream,
  int status_code,
  const char *status_msg,
  size_t content_len,
  const char *content_type
) {
  fprintf(stream, "HTTP/1.1 %d %s\r\n", status_code, status_msg);
  fprintf(stream, "Server: KTK's Web Server\r\n");
  fprintf(stream, "Content-length: %ld\r\n", content_len);
  fprintf(stream, "Content-type: %s\r\n", content_type);
  fprintf(stream, "\r\n");
  fflush(stream);
}


void serve_connection(int sockfd) {  
  FILE *stream = fdopen(sockfd, "r+");

  if (stream == NULL) {
    perror_die("on fdopen");
  }

  fgets(buf, BUFSIZE, stream);
  // printf("%s", buf);
  sscanf(buf, "%s %s %s\n", method, uri, version);

  fgets(buf, BUFSIZE, stream);
  // printf("%s", buf);
  while (strcmp(buf, "\r\n")) {
    fgets(buf, BUFSIZE, stream);
    // printf("%s", buf);
  }

  char *p = strstr(uri, "?");
  if (p) {
    strcpy(params, p+1);
    *p = '\0';
  } else {
    strcpy(params, "");
  }
  strcpy(filename, ".");
  strcat(filename, uri);

  if (strcmp(uri, "/") == 0) {
    strcpy(filename, "index.html");
  }

  // printf("%s\n", filename);
  
  struct stat stat_buf;
  if (stat(filename, &stat_buf) < 0) {
    write_header(stream, 404, "Not Found", 13, "text/html");
    fprintf(stream, "404 Not Found\r\n");
    printf("404 not found\n");
    fclose(stream);
    close(sockfd);
    return;
  }

  char *filetype;
  int is_static = 1;

  if (strstr(filename, ".html")) {
    filetype = "text/html";
  } else if (strstr(filename, ".js")) {
    filetype = "text/javascript";
  } else if (strstr(filename, ".css")) {
    filetype = "text/css";
  } else if (strstr(filename, ".jpg")) {
    filetype = "image/jpeg";
  } else if (strstr(filename, ".png")) {
    filetype = "image/png";
  } else if (strstr(filename, ".php")) {
    filetype = "text/html";
    is_static = 0;
  } else {
    filetype = "text/pain";
  }

  if (is_static) {
    write_header(stream, 200, "OK", stat_buf.st_size, filetype);
    
    // Body
    int fd = open(filename, O_RDONLY);
    char *p = mmap(0, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // map file to memory
    if ((long int)p > 0) {
      fwrite(p, 1, stat_buf.st_size, stream);
      munmap(p, stat_buf.st_size); // free memory
    }
  } else {
    fprintf(stream, "HTTP/1.1 200 OK\r\n");
    fflush(stream);

    setenv("QUERY_STRING", params, 1);
    setenv("REDIRECT_STATUS", "200", 1);
    setenv("REQUEST_METHOD", method, 1);
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("SCRIPT_FILENAME", filename, 1);

    int wait_status;
    int pid = fork();
    printf("%d %s %s\n", pid, method, filename);
    if (pid < 0) {
      perror_die("on fork");
    } else if (pid > 0) {
      wait(&wait_status);
    } else {
      dup2(sockfd, 1);
      dup2(sockfd, 2);
      if (execl("/usr/bin/php-cgi", filename, (char *)NULL) < 0) {
        perror_die("on execve");
      }
    }
  }

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