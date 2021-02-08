#include <arpa/inet.h>
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

typedef unsigned char byte;
typedef struct { int sockfd; } thread_config_t;

pthread_t thread_pool[THREAD_POOL_SIZE];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

int reqCount = 1;
int fpm_sockfd;

typedef struct {
  char *key;
  char *value;
  int key_size;
  int value_size;
} map;

typedef struct {
  map *pairs;
  int size;
} fcgi_pairs;

void perror_die(const char *s) {
  perror(s);
  exit(EXIT_FAILURE);
}


// START fastcgi



/*
 * Listening socket file number
 */
#define FCGI_LISTENSOCK_FILENO 0

typedef struct {
    byte version;
    byte type;
    byte requestIdB1;
    byte requestIdB0;
    byte contentLengthB1;
    byte contentLengthB0;
    byte paddingLength;
    byte reserved;
} FCGI_Header;

/*
 * Number of bytes in a FCGI_Header.  Future versions of the protocol
 * will not reduce this number.
 */
#define FCGI_HEADER_LEN  8

/*
 * Value for version component of FCGI_Header
 */
#define FCGI_VERSION_1           1

/*
 * Values for type component of FCGI_Header
 */
#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

/*
 * Value for requestId component of FCGI_Header
 */
#define FCGI_NULL_REQUEST_ID     0

typedef struct {
    byte roleB1;
    byte roleB0;
    byte flags;
    byte reserved[5];
} FCGI_BeginRequestBody;

typedef struct {
    FCGI_Header header;
    FCGI_BeginRequestBody body;
} FCGI_BeginRequestRecord;

/*
 * Mask for flags component of FCGI_BeginRequestBody
 */
#define FCGI_KEEP_CONN  1

/*
 * Values for role component of FCGI_BeginRequestBody
 */
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

typedef struct {
    byte appStatusB3;
    byte appStatusB2;
    byte appStatusB1;
    byte appStatusB0;
    byte protocolStatus;
    byte reserved[3];
} FCGI_EndRequestBody;

typedef struct {
    FCGI_Header header;
    FCGI_EndRequestBody body;
} FCGI_EndRequestRecord;

/*
 * Values for protocolStatus component of FCGI_EndRequestBody
 */
#define FCGI_REQUEST_COMPLETE 0
#define FCGI_CANT_MPX_CONN    1
#define FCGI_OVERLOADED       2
#define FCGI_UNKNOWN_ROLE     3

/*
 * Variable names for FCGI_GET_VALUES / FCGI_GET_VALUES_RESULT records
 */
#define FCGI_MAX_CONNS  "FCGI_MAX_CONNS"
#define FCGI_MAX_REQS   "FCGI_MAX_REQS"
#define FCGI_MPXS_CONNS "FCGI_MPXS_CONNS"

typedef struct {
    byte type;    
    byte reserved[7];
} FCGI_UnknownTypeBody;

typedef struct {
    FCGI_Header header;
    FCGI_UnknownTypeBody body;
} FCGI_UnknownTypeRecord;

typedef struct {
  FCGI_Header header;
  byte *buf;
} record_t;

// Implementation
byte get_byte(int pos, int value) {
  if (pos == 0) {
    return (byte)(value & 0xff);
  } else if (pos == 1) {
    return (byte)((value >> 8) & 0xff);
  } else if (pos == 2) {
    return (byte)((value >> 16) & 0xff);
  } else if (pos == 3) {
    return (byte)((value >> 24) & 0xff);
  }
  return 0;
}

int get_int2(byte b0, byte b1) {
  return (int)b0 + (((int)b1)<<8);
}

int get_int4(byte b0, byte b1, byte b2, byte b3) {
  return (int)b0 + (((int)b1)<<8) + (((int)b2)<<16) + (((int)b3)<<24);
}

FCGI_Header *init_header(
  int recType,
  int reqId,
  int contentLength
) {
  FCGI_Header *h = malloc(sizeof(FCGI_Header));
  h->version = 1;
  h->type             = (byte) recType;
  h->requestIdB0      = get_byte(0, reqId);
  h->requestIdB1      = get_byte(1, reqId);
  h->contentLengthB0  = get_byte(0, contentLength);
  h->contentLengthB1  = get_byte(1, contentLength);
  h->paddingLength    = (byte) (-contentLength & 7);
  return h;
}

void write_fcgi_header(int sockfd, FCGI_Header *h) {
  if (send(sockfd, h, FCGI_HEADER_LEN, 0) < 0) {
    perror_die("on write_fcgi_header");
  }
}

void write_fcgi_content(int sockfd, byte *content, int content_len) {
  if (send(sockfd, content, content_len, 0) < 0) {
    printf("Failed to write\n");
    perror_die("on write_fcgi_content");
  }
}

void write_fcgi_padding(int sockfd, FCGI_Header *h) {
  char *buf = malloc(sizeof(char)*h->paddingLength);
  if (send(sockfd, buf, h->paddingLength, 0) < 0) {
    perror_die("on write_fcgi_header");
  }
}

void write_fcgi_record(int sockfd, char recType, int reqId, byte *content, int content_len) {
  FCGI_Header *h = init_header(recType, reqId, content_len);
  write_fcgi_header(sockfd, h);
  write_fcgi_content(sockfd, content, content_len);
  write_fcgi_padding(sockfd, h);
}

void write_fcgi_begin_request(int sockfd, int reqId, int role, short flags) {
  char b[8] = {
    get_byte(1, role),
    get_byte(0, role),
    flags
  };
  write_fcgi_record(sockfd, FCGI_BEGIN_REQUEST, reqId, (byte *)b, 8);
}

void write_fcgi_pairs(int sockfd, int recType, int reqId, fcgi_pairs params) {
  byte b[2];

  int buf_size = 0;

  for (int i = 0; i < (int)params.size; i++) {
    int klen = params.pairs[i].key_size;
    int vlen = params.pairs[i].value_size;

    b[0] = get_byte(0, klen);
    b[1] = get_byte(0, vlen);
    buf_size += 2 + klen + vlen;
  }

  byte *buf = malloc(sizeof(char)*buf_size+2);
  byte *p = buf;
  for (int i = 0; i < (int)params.size; i++) {
    int klen = params.pairs[i].key_size;
    int vlen = params.pairs[i].value_size;

    b[0] = get_byte(0, klen);
    b[1] = get_byte(0, vlen);
    {
      memcpy(p, b, 2);
      p += 2;
      memcpy(p, params.pairs[i].key, klen);
      p += klen;
      memcpy(p, params.pairs[i].value, vlen);
      p += vlen;
    }
  }
  buf[buf_size+1] = '\0';
  write_fcgi_record(sockfd, recType, reqId, buf, buf_size);
  write_fcgi_record(sockfd, recType, reqId, (byte *)"", 0);
}

record_t read_fcgi_record(int sockfd) {
  record_t rec;
  int len = recv(sockfd, &rec.header, FCGI_HEADER_LEN, 0);
  if (len < 0) {
    perror_die("on recv read_fcgi_record");
  }
  
  int n = get_int2(rec.header.contentLengthB0, rec.header.contentLengthB1) + (int)rec.header.paddingLength;
  byte *buf = malloc(sizeof(char)*n+1);
  len = recv(sockfd, buf, n, 0);
  if (len < 0) {
    perror_die("on recv read_fcgi_record");
  }
  buf[n] = '\0';

  rec.buf = buf;
  return rec;
}

void fcgi_request(int sockfd, int client_sock, fcgi_pairs env, char *body, int body_len) {
  int reqId = reqCount;
  pthread_mutex_lock(&mutex);
  reqCount++;
  pthread_mutex_unlock(&mutex);
  write_fcgi_begin_request(sockfd, reqId, FCGI_RESPONDER, FCGI_KEEP_CONN);
  write_fcgi_pairs(sockfd, FCGI_PARAMS, reqId, env);
  if (body_len > 0) {
    write_fcgi_record(sockfd, FCGI_STDIN, reqId, (byte *)body, body_len);
  }
  write_fcgi_record(sockfd, FCGI_STDIN, reqId, (byte *)"", 0);

  while (true) {
    record_t rec = read_fcgi_record(sockfd);
    int content_len = get_int2(rec.header.contentLengthB0, rec.header.contentLengthB1);
    if (rec.header.type == FCGI_STDOUT) {
      send(client_sock, rec.buf, content_len, 0);
    } else if (rec.header.type == FCGI_STDERR) {
      break;
    } else if (rec.header.type == FCGI_END_REQUEST) {
      break;
    } else if (rec.header.type == FCGI_REQUEST_COMPLETE) {
      break;
    } else {
      break;
    }
  }
}

int fcgi_connect() {
  int sockfd;
  struct sockaddr_in serv_addr;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror_die("on fpm socket");
  }

  memset(&serv_addr, 0, sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  serv_addr.sin_port = htons(9000);

  if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) {
    perror_die("on connect fpm");
  }
  
  printf("Connected to php-fpm.\n");

  return sockfd;
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
  char method[16] = "";
  char uri[BUFSIZE] = "";  // URI should be less than 1024bytes
  char version[16] = "";
  char filename[256] = "";
  char params[BUFSIZE] = "";
  char body[BUFSIZE] = "";
  char content_len[16] = "";
  char content_type[256] = "";
  char document_root[BUFSIZE] = "/home/ktk/LetsTry/webserver/tests";
  char script_filename[BUFSIZE] = "";
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
    strcat(script_filename, document_root);
    strcat(script_filename, filename + 1);
    map list[11] = {
      {"REDIRECT_STATUS", "200", strlen("REDIRECT_STATUS"), strlen("200")},
      {"REQUEST_METHOD", method, strlen("REQUEST_METHOD"), strlen(method)},
      {"QUERY_STRING", params, strlen("QUERY_STRING"), strlen(params)},
      {"CONTENT_TYPE", content_type, strlen("CONTENT_TYPE"), strlen(content_type)},
      {"CONTENT_LENGTH", content_len, strlen("CONTENT_LENGTH"), strlen(content_len)},
      {"SCRIPT_FILENAME", script_filename, strlen("SCRIPT_FILENAME"), strlen(script_filename)},
      {"SCRIPT_NAME", filename, strlen("SCRIPT_NAME"), strlen(filename)},
      {"SERVER_SOFTWARE", "ktk", strlen("SERVER_SOFTWARE"), strlen("ktk")},
      {"SERVER_PROTOCOL", "HTTP/1.1", strlen("SERVER_PROTOCOL"), strlen("HTTP/1.1")},
      {"DOCUMENT_ROOT", document_root, strlen("DOCUMENT_ROOT"), strlen(document_root)},
      {"GATEWAY_INTERFACE", "CGI/1.1", strlen("GATEWAY_INTERFACE"), strlen("CGI/1.1")},
    };
    
    fcgi_pairs env = { list, 11 };
    printf("fpm_sockfd %d", fpm_sockfd);
    fcgi_request(fpm_sockfd, sockfd, env, body, atoi(content_len));
  }
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
  fpm_sockfd = fcgi_connect();
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
  }

  printf("END\n");
}