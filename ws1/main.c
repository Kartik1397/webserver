#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "queue.h"

#define BUFSIZE 1024
#define PORT 8080
#define N_BACKLOG 100
#define THREAD_POOL_SIZE 30
#define MAX_HEADER 4096 // Header size limit

typedef struct { int sockfd; } thread_config_t;

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

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
  int sockfd,
  int status_code,
  const char *status_msg,
  size_t content_len,
  const char *content_type
) {
  char *res_header = malloc(sizeof(char)*MAX_HEADER);
  int size = 0;
  size += sprintf(res_header, "HTTP/1.1 %d %s\r\n", status_code, status_msg);
  size += sprintf(res_header + size, "Server: KTK's Web Server\r\n");
  size += sprintf(res_header + size, "Content-length: %ld\r\n", content_len);
  size += sprintf(res_header + size, "Content-type: %s\r\n", content_type);
  size += sprintf(res_header + size, "\r\n");
  if (send(sockfd, res_header, size, 0) < 1) {
    perror_die("on send");
  }
}

void write_body(int sockfd, char *body, int content_len) {
  if (send(sockfd, body, content_len, 0) < 1) {
    perror_die("on send");
  }
}

void serve_connection(int sockfd) {
  char *buf = malloc(sizeof(char)*MAX_HEADER);
  char method[16];
  char uri[BUFSIZE];  // URI should be less than 1024bytes
  char version[16];
  char filename[256];
  char params[BUFSIZE];
  char body[BUFSIZE];
  char content_len[16];
  char content_type[256];
  char *lines[100];

  int nlines = 0;

  // fgets(buf, BUFSIZE, stream);
  int len = recv(sockfd, buf, MAX_HEADER, 0);
  if (len < 0) {
    perror_die("recv");
  }
  buf[len] = '\0';

  char *p = buf;
  lines[nlines] = p;
  nlines++;
  while ((p = strstr(p, "\r\n"))) {
    *p = '\0';
    p += 2;
    lines[nlines] = p;
    nlines++;
  }
  nlines--;
  int line_idx = 0;
  sscanf(lines[line_idx], "%s %s %s", method, uri, version);
  line_idx++;
  nlines--;

  // Parsing Header
  while (nlines-- > 0) {
    if ((p = strstr(lines[line_idx], "Content-Length"))) {
      p += 16;
      strcpy(content_len, p);
    }
    if ((p = strstr(lines[line_idx], "Content-Type"))) {
      p += 14;
      strcpy(content_type, p);
    }
    line_idx++;
  }

  if (strcmp(method, "POST") == 0) {
    strcpy(body, lines[line_idx]);
  }

  p = strstr(uri, "?");
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

  struct stat stat_buf;
  if (stat(filename, &stat_buf) < 0) {
    write_header(sockfd, 404, "Not Found", 13, "text/html");
    write_body(sockfd, "404 Not Found\r\n", strlen("404 Not Found\r\n")+2);
    printf("404 not found\n");
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
    write_header(sockfd, 200, "OK", stat_buf.st_size, filetype);
    
    // Body
    int fd = open(filename, O_RDONLY);
    char *p = mmap(0, stat_buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0); // map file to memory
    if ((long int)p > 0) {
      write_body(sockfd, p, stat_buf.st_size);
      munmap(p, stat_buf.st_size); // free memory
    }
    close(fd);
  } else {
    send(sockfd, "HTTP/1.1 200 OK\r\n", 17, 0);

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
        write(pipefd[1], body, atoi(content_len));
        // FILE *pipewrite = fdopen(pipefd[1], "w");
        // if (pipewrite == NULL) {
        //   perror_die("on fdopen pipewrite");
        // }
        // fprintf(pipewrite, "%s", body);
        // fflush(pipewrite);
        // fclose(pipewrite);
      }
      wait(&wait_status);
      close(pipefd[1]);
      close(pipefd[0]);
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
  close(sockfd);
}

void *server_thread() {
  while(true) {
    int *pclient;
    pthread_mutex_lock(&mutex);
    if ((pclient = dequeue()) == NULL) {
      pthread_cond_wait(&condition_var, &mutex);
      pclient = dequeue();
    }
    pthread_mutex_unlock(&mutex);

    if (pclient != NULL) {
      int sockfd = *pclient;
      struct timespec tstart={0, 0}, tend={0, 0};
      clock_gettime(CLOCK_MONOTONIC, &tstart);
      clock_t start = clock();
      
      serve_connection(sockfd);
      
      double cpu_time = ((double)(clock()-start)*1000) / CLOCKS_PER_SEC;
      clock_gettime(CLOCK_MONOTONIC, &tend);
      double real_time = ((double)tend.tv_sec*1000 + 1.0e-6*tend.tv_nsec) -
                          ((double)tstart.tv_sec*1000 + 1.0e-6*tstart.tv_nsec);

      printf("[ RT: %.2fms, CT: %.2fms ]\n\n", real_time, cpu_time);
    }
  }
  return 0;
}

int main() {
  const int server_sockfd = listen_socket(PORT);
  printf("Listening on http://127.0.0.1:%d\n", PORT);

  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    pthread_create(&thread_pool[i], NULL, server_thread, NULL);
  }

  while (1) {
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    int peer_sockfd = accept(server_sockfd, (struct sockaddr*)&peer_addr, &peer_addr_len);
    if (peer_sockfd < 0) {
      perror_die("on accept");
    }

    int *pclient = malloc(sizeof(int));
    *pclient = peer_sockfd;
    pthread_mutex_lock(&mutex);
    enqueue(pclient);
    pthread_cond_signal(&condition_var);
    pthread_mutex_unlock(&mutex);

    // pthread_t the_thread;
    // thread_config_t* config = (thread_config_t *)malloc(sizeof(*config));
    // config->sockfd = peer_sockfd;

    // pthread_create(&the_thread, NULL, server_thread, config);
    // pthread_detach(the_thread);
  }

  printf("END\n");
}