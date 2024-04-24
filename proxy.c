#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef struct cache_t
{
  char uri[MAXLINE];
  int content_length;
  char *response_ptr;
  struct cache_t *prev, *next;
} cache_t;

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *);
void parse_uri(char *, char *, char *, char *); 
void *thread(void *);

cache_t *find_cache(char *);
void send_cache(cache_t *, int);
void insert_front_cache(cache_t *);
void insert_cache(cache_t *);

cache_t *rootp;  // 캐시 연결리스트의 root 객체
cache_t *lastp;  // 캐시 연결리스트의 마지막 객체
int total_cache_size = 0; // 캐싱된 객체 크기의 총합


int main(int argc, char **argv) {
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  rootp = (cache_t *)calloc(1, sizeof(cache_t));
  lastp = (cache_t *)calloc(1, sizeof(cache_t));

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // cmd에 입력한 Port로 듣기 식별자 열기
  while(1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
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

  // 현재 요청이 캐싱된 요청(uri)인지 확인
  cache_t *cache = find_cache(uri);
  if (cache) // 캐싱 되어있다면
  {
    send_cache(cache, fd);          // 캐싱된 객체를 Client에 전송
    insert_front_cache(cache);      // 사용한 객체를 맨 앞으로 넣기
    return;                         // Server로 요청을 보내지 않고 통신 종료
  }

  // 클라이언트로 부터 받은 header의 내용을 저장. (다시 서버로 보내주기 위해서)
  read_requesthdrs(&rio, header);

  printf("new request header\n");
  printf("%s", header);

  // 클라이언트 역할. 웹서버로 요청 보내기.
  int clientfd;
  clientfd = Open_clientfd(hostname, port);
  if (clientfd < 0) {
    printf("Failed to connect to the server\n");
    return;
  }

  Rio_writen(clientfd, header, strlen(header)); // 저장해 둔 request header 보내기.

  /* 서버로부터 응답을 받아 클라이언트로 전송 */
  rio_t rio_cli;
  char buf_cli[MAXLINE];
  char *saved_content = NULL; // 저장할 메모리 공간 초기화
  int saved_content_size = 0;
  int n;

  Rio_readinitb(&rio_cli, clientfd);

  // 서버로 부터 받아온 응답 내용 클라이언트로 보내주기. (캐시에 저장하기 위해 saved_content에 따로 저장해두기)
  while((n = Rio_readnb(&rio_cli, buf_cli, MAXLINE)) > 0) {
    saved_content_size = n;
    saved_content = malloc(n);
    memcpy(saved_content, buf_cli, n);
    Rio_writen(fd, buf_cli, n);
  }

  // MAX_OBJECT_SIZE를 넘지 않는 경우 캐시 연결리스트에 추가하기
  if (sizeof(saved_content) <= MAX_OBJECT_SIZE)
  {
    cache_t *cache = (cache_t *)calloc(1, sizeof(cache_t));
    cache->response_ptr = saved_content;
    cache->content_length = saved_content_size;
    strcpy(cache->uri, uri);
    insert_cache(cache); // 캐시 연결 리스트에 추가
  }
  else
    free(saved_content); // 저장할 수 없는 경우 메모리 반환

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

/* cache 관련 함수 */

// 캐시 연결 리스트에 해당 uri가 있는지 찾는 함수
cache_t *find_cache(char *uri)
{
  if(!rootp)
    return NULL;

  cache_t *current = rootp;
  while (strcmp(current->uri, uri)) {
    if (!current->next)
      return NULL;

    current = current->next;
    if (!strcmp(current->uri, uri))
      return current;
  }
  return current;
}

// cache에 저장된 내용을 클라이언트로 전송하는 함수
void send_cache(cache_t *cache, int fd)
{
  char buf[MAXLINE];
  Rio_writen(fd, cache->response_ptr, cache->content_length);

}

// cache에서 찾아서 사용한 경우. 맨 앞으로 붙여주는 함수.
void insert_front_cache(cache_t *cache)
{
  if (cache == rootp) // 현재 노드가 이미 root인 경우
    return;

  // 이전, 다음 노드들의 연결 끊기
  if (cache->next) { // 다음 노드가 있는 경우
    cache_t *prev_cache = cache->prev;
    cache_t *next_cache = cache->next;
    if (prev_cache)
      cache->prev->next = next_cache;
    cache->next->prev = prev_cache;
  }else { // 다음 노드가 없는 경우
    cache->prev->next = NULL;
  }

  // 현재 노드 맨 앞으로 붙이기
  cache->next = rootp;
  rootp = cache;
}

void insert_cache(cache_t *cache)
{
  // 전체 캐시 사이즈에 현재 객체의 크기 추가
  total_cache_size += cache->content_length;

  // 저장 공간이 부족한 경우(최대 총 캐시 크기를 초과한 경우) 가장 오래된 객체부터 제거
  while (total_cache_size > MAX_CACHE_SIZE) {
    total_cache_size -= lastp->content_length;
    lastp = lastp->prev;
    free(lastp->next);
    lastp->next = NULL;
  }

  if (!rootp) // 캐시 리스트가 빈 경우
    lastp = cache;

  // 현재 객체를 맨 앞에 추가
  if (rootp)
  {
    cache->next = rootp;
    rootp->prev = cache;
  }
  rootp = cache;
}