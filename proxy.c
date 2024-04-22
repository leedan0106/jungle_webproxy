#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *);
void parse_uri(char *, char *, char *, char *); 

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // cmd에 입력한 Port로 듣기 식별자 열기
  while(1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
}

void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], header[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  char hostname[MAXLINE], path[MAXLINE];
  char port[MAXLINE];

  // uri에서 hostname, path, port 분리
  parse_uri(uri, hostname, path, port);
  printf("hostname, path, port %s %s %s\n", hostname, path, port);
  sprintf(header, "%s %s %s\r\n", method, path, "HTTP/1.0");

  // rio의 값을 header로 저장.
  read_requesthdrs(&rio, header);

  printf("new request header\n");
  printf("%s", header);

  // 클라이언트 역할. 웹서버로 요청 보내기.
  int clientfd;
  // printf("host %s %d\n", hostname, port);
  clientfd = Open_clientfd(hostname, port);
  if (clientfd < 0) {
    printf("Failed to connect to the server\n");
    return;
  }
  Rio_writen(clientfd, header, strlen(header)); // request header 보내기.

  // 서버로부터 응답을 받아 클라이언트로 전송
  ssize_t n;
  rio_t rio_cli;
  char buf_cli[MAXLINE];

  /* Read request line and headers */
  Rio_readinitb(&rio_cli, clientfd);

  while ((n = Rio_readlineb(&rio_cli, buf_cli, MAXLINE)) != 0) {
    Rio_writen(fd, buf_cli, n);
  }

  Close(clientfd);

}

void parse_uri(char *uri, char *hostname, char *path, char *port) {
  // "http://" 이후의 문자열을 hostname에 복사
  char *ptr = strstr(uri, "//");
  if (ptr != NULL) {
      ptr += 2;
      char *end_ptr = strchr(ptr, '/');
      if (end_ptr != NULL) {
          // hostname과 path를 추출하여 복사
          *end_ptr = '\0'; // hostname의 끝을 표시
          strcpy(hostname, ptr);
          *end_ptr = '/'; // 다시 복원
          strcpy(path, end_ptr); // "/" 이후의 문자열을 path에 복사
      } else {
          // "/"가 없는 경우
          strcpy(hostname, ptr);
          strcpy(path, "/"); // 기본 경로 "/"
      }
  } else {
      // "http://"이 없는 경우
      strcpy(hostname, uri);
      strcpy(path, "/"); // 기본 경로 "/"
  }

  // hostname에서 ":"을 찾아 port를 추출
  ptr = strchr(hostname, ':');
  if (ptr != NULL) {
      *ptr = '\0';
      strcpy(port, ptr + 1);
  } else {
      strcpy(port, "80"); // 기본 포트 80 사용
  }
}

void read_requesthdrs(rio_t *rp, char *header) {
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    if (strncmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")) != 0 &&
        strncmp(buf, "User-Agent:", strlen("User-Agent:")) != 0) {
        strcat(header, buf);
    }
    while(strcmp(buf, "\r\n")) {
        Rio_readlineb(rp, buf, MAXLINE);
        if (strncmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")) != 0 &&
            strncmp(buf, "User-Agent:", strlen("User-Agent:")) != 0) {
            strcat(header, buf);
        } else if (strncmp(buf, "User-Agent:", strlen("User-Agent:")) == 0) {
            // "User-Agent" 헤더를 원하는 문자열로 변경
            strcat(header, user_agent_hdr);
        }
    }
    return;
}