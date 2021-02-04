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

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 64

uint8_t buf[BUFSIZE];
char method[BUFSIZE];
char uri[BUFSIZE];
char version[BUFSIZE];
char filename[BUFSIZE];
char params[BUFSIZE];
char body[BUFSIZE];
char content_len[BUFSIZE];
char content_type[BUFSIZE];

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
  printf("%s", buf);
  sscanf(buf, "%s %s %s\n", method, uri, version);

  // Parsing Header
  while (strcmp(buf, "\r\n")) {
    fgets(buf, BUFSIZE, stream);
    
    char *p;
    if (p = strstr(buf, "Content-Length")) {
      p += 16;
      strcpy(content_len, p);
      content_len[strlen(content_len)-2] = '\0';
      // printf("%s", content_len);
    }
    if (p = strstr(buf, "Content-Type")) {
      p += 14;
      strcpy(content_type, p);
      content_type[strlen(content_type)-2] = '\0';
    }
    // printf("%s", buf);
  }

  if (strcmp(method, "POST") == 0) {
    char *temp;
    fgets(buf, strtold(content_len, &temp)+1, stream);
    strcpy(body, buf);
    // printf("%s\n", buf);
  }

  // printf("%s\n", body);

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
    if (strcmp(method, "POST") == 0) {
      setenv("CONTENT_LENGTH", content_len, 1);
      setenv("CONTENT_TYPE", content_type, 1);
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
      perror_die("on pipe");
    }
    
    int wait_status;
    int pid = fork();
    printf("%d %s %s\n", pid, method, filename);
    if (pid < 0) {
      perror_die("on fork");
    } else if (pid > 0) {
      if (strcmp(method, "POST") == 0) {
        close(pipefd[0]);
        FILE *pipewrite = fdopen(pipefd[1], "w");
        fprintf(pipewrite, "%s", body);
        fflush(pipewrite);
        fclose(pipewrite);
      }
      wait(&wait_status);
    } else {
      if (strcmp(method, "POST") == 0) {
        close(pipefd[1]);
        close(0);
        dup2(pipefd[0], 0);
      }
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
  printf("Listening on http://127.0.0.1:%d\n", PORT);
  while (1) {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    int peer_sockfd = accept(server_sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
    if (peer_sockfd < 0) {
      perror_die("on accept");
    }
    
    struct timespec tstart={0, 0}, tend={0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    clock_t start = clock();
    time_t begin = time(NULL);
    
    serve_connection(peer_sockfd);
    
    double cpu_time = ((double)(clock()-start)*1000) / CLOCKS_PER_SEC;
    clock_gettime(CLOCK_MONOTONIC, &tend);
    double real_time = ((double)tend.tv_sec*1000 + 1.0e-6*tend.tv_nsec) -
                       ((double)tstart.tv_sec*1000 + 1.0e-6*tstart.tv_nsec);

    printf("[ RT: %.2fms, CT: %.2fms ]\n\n", real_time, cpu_time);
  }

  printf("END\n");
}