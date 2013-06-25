/*
  tftpd.c
  $Id: tftpd.c,v 1.15 2004/01/05 08:12:36 thayashi Exp $

  This file is part of t-tftpd.

  Copyright 2002,2007 Tomofumi Hayashi <s1061123@gmail.com>.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
  3. All advertising materials mentioning features or use of this software
  must display the following acknowledgement:

  This product includes software developed by
  Tomofumi Hayashi <s1061123@gmail.com> and its contributors.

  4. Neither the name of authors nor the names of its contributors may be used
  to endorse or promote products derived from this software without
  specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "tftp.h"

#include "tftpd.h"
#include "tftpdsubs.h"

#define PKTSIZE SEGSIZE+4

#define DEFAULT_THREAD 8
#define MAX_THREAD 256
#define TIMEOUT 2 /* sec */
#define MAXTIMEOUT 6 /* sec */
#define PATH_SIZ 128
#define CHANGE_NODE_INTERVAL 10

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif /* BUFSIZ */


#ifdef TFTPD_V4ONLY
#define SERV_PORT 69
#else 
#define SERV_PORT "69"
#endif 

#ifndef LOG_TFTP
#define LOG_TFTP LOG_DAEMON
#endif /* LOG_TFTP */

/* option support */
#define TFTP_OPTION_BLOCK_SIZE "blksize"
#define TFTP_OPTION_BLOCK_SIZE_MAX 65464
#define TFTP_OPTION_BLOCK_SIZE_MIN 8

/* for pthread_t */
#ifdef PTHREAD_T_POINTER
#define PTHREAD_T_NULL NULL
#else 
#define PTHREAD_T_NULL -1
#endif

#define MMAP_FILE_MAP_MULTIPLY  32
#ifdef HAVE_SYSCONF
#define MMAP_FILE_MAP_SIZE  	sysconf(_SC_PAGE_SIZE)*MMAP_FILE_MAP_MULTIPLY
#else
#define MMAP_FILE_MAP_SIZE  	getpagesize()*MMAP_FILE_MAP_MULTIPLY
#endif

struct _tftp_thread {
  int peer;
  int total_timeout;
  size_t buflen;
  enum mode {NETASCII, OCTET} mode;
  enum cond {RUNNING,WAITING,STOP} cond;
  char buf[BUFSIZ];
  /* for sending file */
  int newline, prevchar;
#ifdef TFTPD_V4ONLY
  struct sockaddr_in client_addr;
  /* for option */
  int block_size;
#else
  struct sockaddr_storage client_addr;
  /* for option */
  int block_size;
  struct server_socket *ssocket;
#endif
};
typedef struct _tftp_thread tftpd_thread;

#ifndef TFTPD_V4ONLY /* for IPv6 */
/* 
 * this structure for threads that create server socket.
 */
struct server_socket {
  int socket;
  int socket_domain;
  int socket_type;
  int socket_protocol;
  struct sockaddr_storage servaddr;
  socklen_t addrlen;
  pthread_t thread_tid[MAX_THREAD];
  pthread_cond_t thread_cond;
  pthread_mutex_t exit_mutex;
  pthread_t exited_tid;
};
#endif

static struct errmsg {
  int	e_code;
  const char *e_msg;
} errmsgs[] = {
  { EUNDEF,	"Undefined error code" },
  { ENOTFOUND,	"File not found" },
  { EACCESS,	"Access violation" },
  { ENOSPACE,	"Disk full or allocation exceeded" },
  { EBADOP,	"Illegal TFTP operation" },
  { EBADID,	"Unknown transfer ID" },
  { EEXISTS,	"File already exists" },
  { ENOUSER,	"No such user" },
  { -1,		0 }
};

void print_usage (void);
void change_node_thread(void);
void change_node(int sig);
void fun_thread_once(void);
void thread_destructor(void *ptr);
void thread_packet_parse(void); 
size_t read_data_ascii(FILE *fp, char *buf, size_t siz);
size_t read_data_ascii_mmap(char *fbuf, char *buf, size_t buf_size,
                            size_t max_read, size_t *fill_size);
size_t write_data_ascii(int fd, char *buf, size_t size); 
void send_file(int fd); 
void send_file_mmap(int fd); 
void recv_file(int fd);
int file_open(char *filename, int wd, enum mode mode); 
void thread_main(void *);
void thread_quit(void); 
void send_error(int error);
char *divide_token(char *src, char delim);
int serv_init(void);
void init_signal(void);
void quit(int sig);

#ifndef TFTPD_V4ONLY
void server_main (void *);
#endif

#ifdef _DEBUG
extern int debug_level;
#endif /* #ifdef _DEBUG */

// for performance mesurement
#define PERFORMANCE_CHECK 1
#ifdef PERFORMANCE_CHECK
unsigned long long rdtsc1 = 0, rdtsc2 = 0;
#define RDTSC(X) \
    do { \
        unsigned int eax, edx; \
        __asm__ __volatile__ ("cpuid"::: "eax", "ebx", "ecx", "edx"); \
        __asm__ __volatile__ ("rdtsc": "=a"(eax), "=d"(edx)); \
        X = ((unsigned long long)edx << 32) | eax; \
    } while (0);

unsigned long long rdtsc() {
    unsigned long long r;
    RDTSC(r);
    return r;
}
#endif

/* global variables */
#ifdef TFTPD_V4ONLY
static pthread_cond_t thread_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t node_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t thread_once = {PTHREAD_ONCE_INIT};
static pthread_key_t thread_key;
static pthread_t exited_tid = PTHREAD_T_NULL;
static pthread_t thread_tid[MAX_THREAD];
#else  /* for IPv6 */
static pthread_mutex_t node_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t thread_once = PTHREAD_ONCE_INIT;
static pthread_key_t thread_key;
static pthread_t thread_tid[MAX_THREAD];
#endif

#ifdef TFTPD_V4ONLY
static int serv_port;
static int sockfd;
static struct sockaddr_in servaddr;
#else /* IPv6 */
static char serv_port[8];
#endif 

static f_node *root_node;
static int sockfd;
static char tftpd_root[PATH_SIZ];
static int socket_threads;
static char program_name[256];
struct timeval timeout;
static int use_mmap = 0;

/* functions */
int main(int argc, char **argv)
{
  int cnt, ch, root, err;
  pthread_t change_tree_tid;

#ifdef TFTPD_V4ONLY
  struct sockaddr_in *svp;
#else /* for IPv6 */
  pthread_t serv_tid;
  struct addrinfo hints;
  struct addrinfo *res, *addpt;
  const int on = 1;
  int open_socket;
  struct server_socket *serv;
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV]; /* buffer for hostname/service */

#endif

  /* initialize */
  err = root = 0;
  timeout.tv_sec = TIMEOUT;
  timeout.tv_usec = 0;
  init_signal();
  memset(tftpd_root, '\0', sizeof(char) * PATH_SIZ);
  getcwd(tftpd_root, PATH_SIZ);
  strlcpy(program_name, argv[0], 256);

  /* initial settings */
#ifdef TFTPD_V4ONLY
  serv_port = SERV_PORT;
#else
  strlcpy(serv_port, SERV_PORT, 8);
#endif
  socket_threads = DEFAULT_THREAD;
#ifdef _DEBUG
  while ((ch = getopt(argc, argv, "hmr:p:t:d:")) != EOF)
#else
  while ((ch = getopt(argc, argv, "hmr:p:t:")) != EOF)
#endif
    {
      switch (ch) 
	{
	case 'r': /* root change */
	  root = 1;
	  if (optarg[0] == '/') {
	    strlcpy(tftpd_root, optarg, sizeof(char)*PATH_SIZ);
	  }
	  else {
	    /*
	     * sprintf(tftpd_root, "%s/%s", tftpd_root, ...) may failed
 	     * in linux.
	     */
	    strncat(tftpd_root, "/", sizeof(char)*PATH_SIZ);
	    strncat(tftpd_root, optarg, sizeof(char)*PATH_SIZ);
	  }
	  printf("root: %s\n", tftpd_root);
	  break;
	case 'p': /* port change */
	  cnt = atoi(optarg);
	  if (cnt >= 0 && strlen (optarg) <= 8) {
#ifdef TFTPD_V4ONLY
	    serv_port = cnt;
#else
	    strlcpy(serv_port, optarg, 8);
#endif
	  }
	  else {
	    fprintf(stderr, "port number (%s)is not invalid.\n", optarg);
	  }
	  break;
	case 't': /* number of waitingthreads */
	  cnt = atoi(optarg);
	  if (cnt != 0) {
	    if (cnt < MAX_THREAD+1) 
	      socket_threads = cnt;
	    else {
	      fprintf(stderr, 
		      "threads are too many. it should be less than %d.\n", 
		      MAX_THREAD);
	      exit(0);
	    }
	  }
	  break;
#ifdef _DEBUG
        case 'd':
	  debug_level = atoi(optarg);
	  break;
#endif
	case 'm': /* use mmap() */
          use_mmap = 1;
	  break;
	case 'h': /* help */
	  err = 1;
	  break;
	case '?':
	  fprintf(stderr, "ignoring unknown option %c\n", ch);
	  err = 1;
	  break;
	}
    }

  /* openlog("t-tftpd", LOG_PID, LOG_TFTP);*/

  if (err == 1) {
    print_usage();
    exit(1);
  }

  root_node = get_node(tftpd_root, ".", 1, 1);
  if (root_node == NULL) {
    fprintf(stderr, "Can't access to %s", tftpd_root);
    exit(0);
  }

#ifdef PERFORMANCE_CHECK
  printf("mmap map size: %d\n", MMAP_FILE_MAP_SIZE);
#endif
  /* create a server socket and bind to port */
#ifdef TFTPD_V4ONLY
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  assert(sockfd!=-1);

  memset(&servaddr, '\0', sizeof(servaddr));
  svp = (struct sockaddr_in*)&servaddr;
  svp->sin_family = AF_INET;
  svp->sin_addr.s_addr = htonl(INADDR_ANY);
  svp->sin_port = htons(serv_port);
  if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
    perror("[t-tftpd] bind failed.");
    close (sockfd);
    exit(1);
  }
  printf("[t-ftpd] binds port: %s:%d\n", inet_ntoa(svp->sin_addr), serv_port);

#else /* for IPv6 */
  /* for protocol independent code.*/
  memset(&hints, 0,sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  hints.ai_flags = AI_PASSIVE;
  if ((err = getaddrinfo(NULL, serv_port, &hints, &res)) != 0) {
    printf("%s\n", gai_strerror(err));
    exit(1);
  }
	
  open_socket = 0;
  /* for each addresses (IPv4/IPv6/mapped?) we bind to a port.
   * server waits therefore multiple ports/addresses, we need to 
   * have a multiple server environments(pthread_t, or so.)
   */
  for (addpt = res; addpt; addpt = addpt->ai_next) {
    if ((sockfd = socket(addpt->ai_family, addpt->ai_socktype,
			 addpt->ai_protocol)) < 0) {
      continue;
    }
#ifdef IPV6_V6ONLY
    /* setsockopt should be in front of "bind". */
    if (addpt->ai_family == AF_INET6 &&
	setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY,
		   &on, sizeof(on)) < 0) {
      perror("setsockopt error.");
      continue;
    }
#endif /* #ifdef IPV6_V6ONLY*/
    if (bind(sockfd, addpt->ai_addr, addpt->ai_addrlen) < 0) {
      fprintf(stderr, "bind failed\n");
      close(sockfd);
      continue;
    } 

    getnameinfo(addpt->ai_addr, addpt->ai_addrlen, hbuf, sizeof(hbuf),
                sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
    printf("bind: %s:%s\n", hbuf, sbuf);

    /* create server thread */
    serv = (struct server_socket *)malloc(sizeof(struct server_socket));
    if (serv == NULL) {
      perror ("malloc error (of struct server_socket");
      continue;
    }

    serv->socket = sockfd;
    memcpy(&(serv->servaddr),addpt->ai_addr,sizeof(struct sockaddr));
    serv->addrlen = addpt->ai_addrlen; /* sockaddr ��len*/
    serv->socket_domain = addpt->ai_family;
    serv->socket_type = addpt->ai_socktype;
    serv->socket_protocol = addpt->ai_protocol;
    /* use GCC extension (for strucutre copy) */
    serv->thread_cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    serv->exit_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    serv->exited_tid = PTHREAD_T_NULL;

    /* create the server thread */
    if (pthread_create(&serv_tid, NULL, 
		       (void *(*)(void *))&server_main, serv) != 0)	{
      fprintf(stderr, "pthread_create failed\n");
    }
    open_socket++;
  }
  freeaddrinfo(res);

  /* If no socket cannot be opened */
  if (open_socket == 0)  {
    fprintf(stderr, "No sockets are available. quit.\n");
    exit(0);
  }
    
  d_printf(3, ("thread: (%d) / port: %s\n", socket_threads, serv_port));
#endif /* #ifdef TFTPD_V4ONLY ...*/
  /* for update tree. */
  pthread_create(&change_tree_tid, NULL,
		 (void *(*)(void *))&change_node_thread, NULL);

  /* inital creation for stat tree. */
#ifndef TFTPD_V4ONLY /* for IPv6 */
  /* waiting for server thread */
  pthread_join (serv_tid, NULL);

#else 
  /* IPv4 server creates the thread here(IPv6 is upper). */
  /* Create threads */
  for (cnt = 0; cnt < socket_threads; cnt++)
    {
      pthread_create(&thread_tid[cnt], NULL, 
		     (void *(*)(void *))&thread_main, NULL);
    }
    
  while (1)
    {
      pthread_mutex_lock(&exit_mutex);
      while(exited_tid == -1) {
	pthread_cond_wait(&thread_cond, &exit_mutex);
      }
      for (cnt = 0; cnt < socket_threads; cnt++)
	{
	  if (thread_tid[cnt] == exited_tid) 
	    {
	      pthread_join(thread_tid[cnt], NULL);
#ifndef _DEBUG
	      /*		printf("thread restart\n"); */
	      pthread_create(&thread_tid[cnt], NULL,
			     (void *(*)(void *))&thread_main, NULL);
#endif
	      exited_tid = -1;
	    }
	}
      pthread_mutex_unlock(&exit_mutex);
    }
    
  close(sockfd);
#endif /* #ifndef TFTPD_V4ONLY */
  return 0;
}

void change_node_thread(void)
{
  int cnt;

  while(1) {
    sleep(CHANGE_NODE_INTERVAL);
    change_node(0);
  }
}

void print_usage (void)
{
  fprintf(stdout, 
	  "\n"
	  "t-tftpd %s "
	  "copyright by Tomofumi Hayashi (s1061123@gmail.com)\n\n"
	  "Usage: %s [OPTION] ...\n"
	  "  -h \t\t\t display this help and exit\n"
          "  -m \t\t\t use mmap() for file sending (experimental)\n"
#ifdef TFTPD_V4ONLY
	  "  -p <num> \t\t port number (default: %d)\n"
#else
	  "  -p <num> \t\t port number (default: %s)\n"
#endif
	  "  -r <directory> \t tftpd's rootdir (default: \".\")\n"
	  "  -t <num> \t\t threads for waiting client (default: %d)\n"
	  "\n\n"
	  ,
	  VERSION, 
	  program_name,
	  SERV_PORT, DEFAULT_THREAD);
}

void change_node(int sig)
{
  f_node *ptr, *old;
    
  ptr = get_node(tftpd_root, ".", 1, 1);
  pthread_mutex_lock(&node_mutex);
  old = root_node;
  root_node = ptr;
  pthread_mutex_unlock(&node_mutex);
  free_node(old);
}

void fun_thread_once(void)
{
  d_printf(3, ("[%d]thrad_once called\n", pthread_self()));
  pthread_key_create(&thread_key, thread_destructor);
}

void thread_destructor(void *ptr)
{
  free(ptr);
}

void thread_packet_parse(void)
{
  char *cp;
  char *filename, *mode;
  int fd, rw_flag;
  tftpd_thread *ptr;
  struct tftphdr *hdr;

  ptr = pthread_getspecific(thread_key);
  hdr = (struct tftphdr *)ptr->buf;
  filename = hdr->th_stuff;

  if (ntohs(hdr->th_opcode) == WRQ)
    rw_flag = 1;
  else 
    rw_flag = 0;

  for (cp = hdr->th_data; cp < ptr->buf + ptr->buflen; cp++) {
    if (*cp == '\0') {
      break;
    }
  }
  /* error packet check */
  if (*cp != '\0') {
    send_error(EBADOP);
    thread_quit();
  }

  mode = cp + 1;
  for (cp = cp + 1; cp < ptr->buf + ptr->buflen; cp++) {
    if (*cp == '\0') {
      break;
    }
  }
  /* error packet check */
  if (*cp != '\0') {
    send_error(EBADOP);
    thread_quit();
  }

  for (cp = mode; *cp != '\0';  cp++) {
    if(isupper((int)*cp)) {
      *cp = tolower(*cp);
    }
  }
    
  if (strcmp(mode, "netascii") == 0) {
    ptr->mode = NETASCII;
  }
  else if (strcmp(mode, "octet") == 0) {
    ptr->mode = OCTET;
  }
  else {
    fprintf(stderr, "invalid mode.\n");
    send_error(EBADOP);
    thread_quit();
  }

#ifdef TFTPD_OPTION_PACKET
  { 
    /* Option is recognized in this routine . */
    char opt_name[100]; /* improvement is required! */
    char opt_value[100];
    int value;
    cp++;
      
    if (cp != (ptr->buf)+(ptr->buflen)) {
      fprintf(stdout, "some option is define!\n");
    }
    while (cp!=(ptr->buf)+(ptr->buflen)) {
      fprintf(stdout, "option name: %s\n", cp);
      opt_name = cp;

      cp = cp + strlen(cp) + 1;
      fprintf(stdout, "option value: %s\n", cp);
      opt_value = cp;
      cp = cp + strlen(cp) + 1;

      /* for block size (rfc 2348) */
      strncmp(opt_name, TFTP_OPTION_BLOCK_SIZE, 
	      sizeof(TFTP_OPTION_BLOCK_SIZE));

      value = atoi(opt_value);
      if (value >= TFTP_OPTION_BLOCK_SIZE_MIN && 
          value <= TFTP_OPTION_BLOCK_SIZE_MAX) {
        ptr->block_size = value;
      }
      else {
        ptr->block_size = -1;
      }

    }
  }
#endif

  /* file validation */
  if ((fd = file_open(filename, rw_flag, ptr->mode)) == -1) {
    fprintf(stderr, "can't access\n");
    send_error(EACCESS);
    pthread_mutex_unlock(&node_mutex);
    thread_quit();
  }

  hdr->th_opcode = ntohs(hdr->th_opcode);
  /* File transfer */
  if (hdr->th_opcode == WRQ) {
    /*
      syslog(LOG_NOTICE, "tftpd WRQ: %s", filename);
    */
    recv_file(fd); 
  }
  else if (hdr->th_opcode == RRQ) {
    /*
      syslog(LOG_NOTICE, "tftpd RRQ: %s", filename);
    */
    if (use_mmap) {
      send_file_mmap(fd);
    } else {
      send_file(fd);
    }
  }
  else {
    /*
      syslog(LOG_NOTICE, "tftpd RRQ: %s\n", filename); 
    */
    send_error(EBADOP);
  }

  close(fd);

  return;
}

size_t read_data_ascii(FILE *fp, char *buf, size_t siz)
{
  char *cptr;
  int size, ch, read_size = 0;
  tftpd_thread *thread_ptr;
  thread_ptr = pthread_getspecific(thread_key);
  int newline = thread_ptr->newline;
  int prevchar = thread_ptr->prevchar;
  cptr = buf;

  memset(buf, '\0', sizeof(char)*siz);
  for (size = 0; size < siz; size++) {
    if (newline) {
      if(prevchar == '\n')
	ch = '\n';
      else
	ch = '\0';
      newline = 0;
    }
    else {
      ch = getc(fp);
      if (ch == EOF) {
	break;
      }
      if (ch == '\n' || ch == '\r') {
	prevchar = ch;
	ch = '\r';
	newline = 1;
      }
      read_size++;
    }
    *cptr = ch;
    cptr++;
  }

  thread_ptr->newline = newline;
  thread_ptr->prevchar = prevchar;
  return size;
}

/*
 * lf->cr,lf    cr->cr,nul
 * fill_size means that the filled buffer size.
 */
size_t read_data_ascii_mmap(char *fbuf, char *buf, size_t buf_size,
                            size_t max_read, size_t *read_size)
{
  char *cptr;
  char *fptr;
  int size, ch, rd_size = 0;
  tftpd_thread *thread_ptr;
  thread_ptr = pthread_getspecific(thread_key);
  int newline = thread_ptr->newline;
  int prevchar = thread_ptr->prevchar;
  cptr = buf;
  fptr = fbuf;

  memset(buf, '\0', sizeof(char)*buf_size);
  for (size = 0; size < buf_size; size++) {
    if (newline) {
      if(prevchar == '\n')
	ch = '\n';
      else
	ch = '\0';
      newline = 0;
    }
    else {
      if (rd_size >= max_read) {
        break;
      }
      ch = *fptr; fptr++;
      if (ch == '\n' || ch == '\r') {
	prevchar = ch;
	ch = '\r';
	newline = 1;
      }
      rd_size++;
    }
    *cptr = ch;
    cptr++;
  }

  thread_ptr->newline = newline;
  thread_ptr->prevchar = prevchar;
  *read_size = rd_size;
  return size;
}

size_t write_data_ascii(int fd, char *buf, size_t size)
{
  char *cptr, ch;
  size_t wr_size, actual_data;

  cptr = buf;
  actual_data = size;

  for (wr_size = 0; wr_size < actual_data ; wr_size++) {
    ch = *cptr;
    if (ch == '\r')	{
      ch = *(cptr+1);
      if (ch == '\0') {
	ch = '\r';
	cptr++;
	actual_data--;
      }
      else if (ch == '\n') {
	ch = '\n';
	cptr++;
	actual_data--;
      }
    }

    write(fd, &ch, sizeof(char));
    cptr++;
  }
  return wr_size;
}

void send_file(int fd)
{
  struct tftphdr *dp, *ack;
  FILE *fp;
  int read_buf, read_pkt;
  char *buf;
  uint16_t block;
  int tmp;
  tftpd_thread *thread_ptr;
  struct pollfd file_fds[1], sock_fds[1];
  int ret;

#ifdef _DEBUG
  int total = 0;
  struct stat st;

  if (fstat(fd, &st) == -1) {
	d_printf(10, ("fstat() failed!\n"));
  }
#endif


  thread_ptr = pthread_getspecific(thread_key);
  thread_ptr->total_timeout = 0;

  fp = fdopen(fd, "r");

  dp = malloc(sizeof(char)*PKTSIZE);
  memset(dp, '\0', sizeof(char)*PKTSIZE);
  dp->th_opcode = htons((u_short)DATA);
  block = 1;
    
  ack = malloc(sizeof(char)*PKTSIZE);
  memset(ack, '\0', sizeof(char)*PKTSIZE);

  file_fds[0].fd = fd;
  sock_fds[0].fd = thread_ptr->peer;
  do {
    buf = dp->th_data;
      
  read_file:
    file_fds[0].events = POLLIN;
    ret = poll(file_fds, 1, TIMEOUT * 1000);
    if (ret == 0 || ret == -1) {
      thread_ptr->total_timeout += TIMEOUT;
      if (thread_ptr->total_timeout >= MAXTIMEOUT) {
        d_printf(10, ("Quit due to timeout1.\n"));
        thread_quit();
        return;
      }
      goto read_file;
    }
    thread_ptr->total_timeout = 0;

    if (thread_ptr->mode == OCTET) {
      read_buf = read(fd, buf, SEGSIZE);
    }
    else {
      read_buf = read_data_ascii(fp, buf, SEGSIZE);
    }
    d_printf(10, ("%d bytes read.\n", read_buf));

    if (read_buf == -1) {
      fprintf(stderr, "read error.\n");
    }

  send_data: 
    dp->th_block = htons(block);

    sock_fds[0].events = POLLOUT;
    ret = poll(sock_fds, 1, TIMEOUT * 1000);
    if (ret == 0 || ret == -1) {
      thread_ptr->total_timeout += TIMEOUT;
      if (thread_ptr->total_timeout >= MAXTIMEOUT) {
        d_printf(10, ("Quit due to timeout2.\n"));
        thread_quit();
        return;
      }
      goto send_data;
    }
    thread_ptr->total_timeout = 0;

    tmp = send(thread_ptr->peer, dp, read_buf+4, 0) ;
    d_printf(10, ("[thread:%u]: ack (block:%d) %d byte send.\n",
		  pthread_self(),
		  dp->th_block, tmp));
#ifdef _DEBUG
    if (st.st_size != 0) {
      total += read_buf;
      d_printf(10, ("total: %d/%d bytes\n", total, (int)st.st_size));
    }
#endif
    if (tmp != read_buf + 4) {
      d_printf(1, ("err during send packet.\n"));
      perror("send_file:");
      thread_quit();
    }
    d_printf(10, ("send!\n"));

    for (;;)	{
      /* XXX: TIMEOUT must be exponatial increase. */
      sock_fds[0].events = POLLIN;
      ret = poll(sock_fds, 1, TIMEOUT * 1000);
      if (ret == 0 || ret == -1) {
        thread_ptr->total_timeout += TIMEOUT;
        if (thread_ptr->total_timeout >= MAXTIMEOUT) {
          d_printf(10, ("Quit due to timeout3.\n"));
          thread_quit();
          return;
        }
        goto send_data;
      }
      thread_ptr->total_timeout = 0;

      read_pkt = recv(thread_ptr->peer, ack, sizeof(ack), 0);
      d_printf(10, ("wait for recv.\n"));

      if (read_pkt < 0) {
	close(fd);
	thread_quit();
      }
      ack->th_opcode = ntohs((u_short)ack->th_opcode);
      ack->th_block = ntohs((u_short)ack->th_block);
	    
      if (ack->th_opcode == ERROR) {
	close(fd);
	thread_quit();
      }
	    
      if(ack->th_opcode == ACK) {
	if((u_short)ack->th_block == block)
	  break;
	/* If syncing is necessary, write here. */
      }
		
    }
    block++;
  }
  while (read_buf == 512);

  free(dp);
  free(ack);
  return ;
}

void send_file_mmap(int fd)
{
  struct tftphdr *dp, *ack;
  FILE *fp;
  int read_buf, read_pkt; 
  size_t read_buf_ascii;
  char *buf;
  uint16_t block;
  int tmp;
  tftpd_thread *thread_ptr;
  char *file_map = NULL;
  char *mmap_ptr = NULL;
  uint32_t mmap_off = 0;
  int ret;
  struct pollfd sock_fds[1];
  int total = 0;
  struct stat st;
  int read_ascii_done = 0;

  if (fstat(fd, &st) == -1) {
	d_printf(10, ("fstat() failed!\n"));
  }

  thread_ptr = pthread_getspecific(thread_key);
  thread_ptr->total_timeout = 0;

  fp = fdopen(fd, "r");

  dp = malloc(sizeof(char)*PKTSIZE);
  memset(dp, '\0', sizeof(char)*PKTSIZE);
  dp->th_opcode = htons((u_short)DATA);
  block = 1;
    
  ack = malloc(sizeof(char)*PKTSIZE);
  memset(ack, '\0', sizeof(char)*PKTSIZE);

  sock_fds[0].fd = thread_ptr->peer;

#ifdef PERFORMANCE_CHECK
  rdtsc1 = rdtsc();
#endif
  do {
    buf = dp->th_data;
      
    d_printf(10, ("%p %p\n", mmap_ptr+SEGSIZE, file_map + MMAP_FILE_MAP_SIZE));
    if (mmap_ptr == NULL ||
        mmap_ptr + SEGSIZE > file_map + MMAP_FILE_MAP_SIZE) {
        if (file_map != NULL) {
            d_printf(10, ("Unmap file\n"));
            munmap(file_map, MMAP_FILE_MAP_SIZE);
        }

        d_printf(10, ("Map file from (%d, size:%d)\n",
                      mmap_off, MMAP_FILE_MAP_SIZE));
        mmap_ptr = file_map = mmap(0, MMAP_FILE_MAP_SIZE, PROT_READ, 
			           MAP_FILE|MAP_SHARED, fd, mmap_off);
        if (file_map == MAP_FAILED) {
            fprintf(stderr, "read error:%s\n", strerror(errno));
        }
        mmap_off += MMAP_FILE_MAP_SIZE;
    }

    if (total + SEGSIZE >= st.st_size) {
        read_buf = st.st_size - total;
    } else {
        read_buf = SEGSIZE;
    }

    if (thread_ptr->mode == OCTET) {
        memcpy(buf, mmap_ptr, read_buf);
        d_printf(10, ("%d bytes read.\n", read_buf));
        mmap_ptr+=read_buf; 
    }
    else {
        read_buf = read_data_ascii_mmap(mmap_ptr, buf, SEGSIZE,
                                        read_buf, &read_buf_ascii);
        d_printf(10, ("%s\n", buf));
        mmap_ptr+=read_buf_ascii; 
        d_printf(10, ("%d (read:%d) bytes read.\n", read_buf, read_buf_ascii));
    }

  send_data: 
    dp->th_block = htons(block);
    sock_fds[0].events = POLLOUT;
    ret = poll(sock_fds, 1, TIMEOUT * 1000);
    if (ret == 0 || ret == -1) {
      thread_ptr->total_timeout += TIMEOUT;
      if (thread_ptr->total_timeout >= MAXTIMEOUT) {
        d_printf(10, ("Quit due to timeout4.\n"));
        thread_quit();
        return;
      }
      goto send_data;
    }
    thread_ptr->total_timeout = 0;

    tmp = send(thread_ptr->peer, dp, read_buf+4, 0) ;
    d_printf(10, ("[thread:%u]: ack (block:%d) %d byte send.\n",
		  pthread_self(),
		  dp->th_block, tmp));
#ifdef _DEBUG
    if (st.st_size != 0) {
      if (thread_ptr->mode == OCTET) {
        total += read_buf;
      } else {
        total += read_buf_ascii;
      }
      d_printf(10, ("total: %d/%d bytes\n", total, (int)st.st_size));
    }
#endif
    if (tmp != read_buf + 4) {
      d_printf(1, ("err during send packet.\n"));
      perror("send_file_mmap:");
      thread_quit();
    }
    d_printf(10, ("send!\n"));

    for (;;)	{
      /* XXX: TIMEOUT must be exponatial increase. */
      sock_fds[0].events = POLLIN;
      ret = poll(sock_fds, 1, TIMEOUT * 1000);
      if (ret == 0 || ret == -1) {
        thread_ptr->total_timeout += TIMEOUT;
        if (thread_ptr->total_timeout >= MAXTIMEOUT) {
          d_printf(10, ("Quit due to timeout5.\n"));
          thread_quit();
          return;
        }
        goto send_data;
      }
      thread_ptr->total_timeout = 0;

      read_pkt = recv(thread_ptr->peer, ack, sizeof(ack), 0);
      d_printf(10, ("wait for recv.\n"));

      if (read_pkt < 0) {
	close(fd);
	thread_quit();
      }
      ack->th_opcode = ntohs((u_short)ack->th_opcode);
      ack->th_block = ntohs((u_short)ack->th_block);
	    
      if (ack->th_opcode == ERROR) {
	close(fd);
	thread_quit();
      }
	    
      if(ack->th_opcode == ACK) {
	if((u_short)ack->th_block == block)
	  break;
	/* If syncing is necessary, write here. */
      }
		
    }
    block++;
  }
  while (read_buf == SEGSIZE);
#ifdef PERFORMANCE_CHECK
    rdtsc2 = rdtsc();
    printf("done: %llu - %llu = %llu\n", rdtsc2, rdtsc1, rdtsc2 - rdtsc1);
#endif
  munmap(file_map, MMAP_FILE_MAP_SIZE);
  free(dp);
  free(ack);
  return ;
}

void recv_file(int fd)
{
  struct tftphdr *dp, *ack;
  char *buf;
  size_t read_pkt, write_data;
  size_t send_pkt;
  u_short ack_block;
  tftpd_thread *thread_ptr;
  struct pollfd file_fds[1], sock_fds[1];
  int ret;
    
  thread_ptr = pthread_getspecific(thread_key);
  thread_ptr->total_timeout = 0;

  dp = malloc(sizeof(char)*PKTSIZE);
  memset(dp, '\0', sizeof(char)*PKTSIZE);

  ack = malloc(sizeof(char)*PKTSIZE);
  memset(ack, '\0', sizeof(char)*PKTSIZE);
  ack->th_opcode = htons((u_short)ACK);
  ack_block = 0;
    
  file_fds[0].fd = fd;
  sock_fds[0].fd = thread_ptr->peer;

  do {
    buf = dp->th_data;

    /*	setjmp(thread_ptr->jmpbuf); */
  send_ack:
    ack->th_block = htons(ack_block);

    sock_fds[0].events = POLLOUT;
    ret = poll(sock_fds, 1, TIMEOUT * 1000);
    if (ret == 0 || ret == -1) {
      thread_ptr->total_timeout += TIMEOUT;
      if (thread_ptr->total_timeout >= MAXTIMEOUT) {
        d_printf(10, ("Quit due to timeout6.\n"));
        thread_quit();
        return;
      }
      goto send_ack;
    }
    thread_ptr->total_timeout = 0;
    if((send_pkt = send(thread_ptr->peer, ack, 4, 0)) != 4)	{
      d_printf(3, ("[send_ack] send failed.\n"));
      close(fd);
      thread_quit();
    }
    d_printf(10, ("[send_ack] ack (%d) send.\n", ack->th_block));
    d_printf(9, ("send ack...\n"));
    ack_block++;

    for (;;) {
      sock_fds[0].events = POLLIN;
      ret = poll(sock_fds, 1, TIMEOUT * 1000);
      if (ret == 0 || ret == -1) {
        thread_ptr->total_timeout += TIMEOUT;
        if (thread_ptr->total_timeout >= MAXTIMEOUT) {
          d_printf(10, ("Quit due to timeout7.\n"));
          thread_quit();
          return;
        }
        goto send_ack;
      }
      thread_ptr->total_timeout = 0;

      read_pkt = recv(thread_ptr->peer, dp, PKTSIZE, 0);
	    
      if (read_pkt < 0) {
	close(fd);
	thread_quit();
      }
      dp->th_opcode = ntohs((u_short)dp->th_opcode);
      dp->th_block = ntohs((u_short)dp->th_block);

      if(dp->th_opcode == DATA) {
	if(dp->th_block == ack_block) {
	  break;
	}
	/* If syncing is necessary, write here. */
	if (dp->th_block == (ack_block-1)) {
	  goto send_ack;
	}
      }
    }
	
  write_file:
    sock_fds[0].events = POLLOUT;
    ret = poll(sock_fds, 1, TIMEOUT * 1000);
    if (ret == 0 || ret == -1) {
      thread_ptr->total_timeout += TIMEOUT;
      if (thread_ptr->total_timeout >= MAXTIMEOUT) {
        d_printf(10, ("Quit due to timeout8.\n"));
        thread_quit();
        return;
      }
      goto write_file;
    }
    thread_ptr->total_timeout = 0;
    if (thread_ptr->mode == OCTET) {
      write_data = write(fd, dp->th_data, read_pkt-4);
    }
    else {
      write_data = write_data_ascii(fd, dp->th_data, read_pkt-4);
    }

  }
  while (read_pkt == PKTSIZE);

  close(fd);
  ack->th_block = htons(ack_block); 
send_last_ack:
  sock_fds[0].events = POLLOUT;
  ret = poll(sock_fds, 1, TIMEOUT * 1000);
  if (ret == 0 || ret == -1) {
    thread_ptr->total_timeout += TIMEOUT;
    if (thread_ptr->total_timeout >= MAXTIMEOUT) {
      d_printf(10, ("Quit due to timeout.9\n"));
      thread_quit();
      return;
    }
    goto send_last_ack;
  }
  thread_ptr->total_timeout = 0;

  if((send_pkt = send(thread_ptr->peer, ack, 4, 0)) != 4) {
    d_printf(9, ("send_final ack(%d) failed.\n", ack->th_block));
  }
  free(dp);
  free(ack);

  return ;
}


/*
 * Return: -1 when error.
 * file discriptor id when success.
 */
int file_open(char *filename, int wr, enum mode mode)
{
  int fd;
  char f_path[PATH_SIZ];
  char *cptr, *last;
  f_node *fptr, *prev;

  memset(f_path, '\0', sizeof(char) * PATH_SIZ);
  snprintf(f_path, PATH_SIZ, "./%s", filename);

  fptr = root_node;
  last = divide_token(f_path,'/');
  for (cptr = f_path; cptr < last; cptr += strlen(cptr) + 1) {
    if (*cptr != '.') {
      fptr = get_leaf(fptr, cptr);
      if(fptr == NULL) {
	if (wr == 1 && cptr+strlen(cptr) >= last) {
	  fptr = NULL;
	  break;
	}
	else 
	  return -1;
      }
      else {
	prev = fptr;
	fptr = fptr ->child;
      }
	
      /* Permition check is only just previous directory. */
      if (wr == 1) {
	if (fptr == NULL) {
	  if(prev->is_wr == 0) {
	    return -1;
	  }
	}
	else {
	  if(fptr->is_wr == 0) {
	    return -1;
	  }
	}
      }
    }

  }
  snprintf(f_path, PATH_SIZ, "%s/%s", tftpd_root, filename);
  if (wr == 1) {
    fd = open(f_path, (O_WRONLY|O_TRUNC|O_CREAT), 0777);
  }
  else {
    fd = open(f_path, O_RDONLY);
  }

  return fd;
}

#ifdef TFTPD_V4ONLY
void thread_main(void *param)
{
  int read;
  tftpd_thread *ptr;
  socklen_t len;
  struct sockaddr_in sin;
    
  pthread_once(&thread_once, fun_thread_once);
  ptr = pthread_getspecific(thread_key);
  if (ptr == NULL) {
    /* Initialize of tftd_thread structure. */
    ptr = (tftpd_thread *)malloc(sizeof(tftpd_thread));
    ptr->cond = RUNNING;
    ptr->block_size = -1;	
    ptr->newline = 0;
    ptr->prevchar = 0;
    memset(ptr->buf, 0, sizeof(char)*BUFSIZ);
    pthread_setspecific(thread_key, ptr);
  }

  len = sizeof(ptr->client_addr);

  d_printf(3, ("waiting....(%d)\n", pthread_self()));

  read = recvfrom(sockfd, ptr->buf, BUFSIZ, 0, 
		  (struct sockaddr *)&(ptr->client_addr), &len);
  ptr->buflen = read;

  ptr->peer = socket(AF_INET, SOCK_DGRAM, 0);
  if (ptr->peer == -1) {
    /*	syslog(LOG_ERR, "error socket: %m"); */
    fprintf(stderr, "error socket.\n");
  }
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  if(bind(ptr->peer, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
    /*	syslog(LOG_ERR, "error bind: %m"); */
    fprintf(stderr, "error bind.\n");
    thread_quit();
  }
  if(connect(ptr->peer, (struct sockaddr *)&(ptr->client_addr), 
	     sizeof(ptr->client_addr)) < 0) {
    /*	syslog(LOG_ERR, "error connect: %m"); */
    thread_quit();
  }

  thread_packet_parse();
  close(ptr->peer);

  d_printf(1, ("client process finished(%d).\n", pthread_self()));

  thread_quit();

  return ;
}

#else

void server_main (void *param)
{
  struct server_socket *ptr;
  int cnt;

  pthread_once (&thread_once, fun_thread_once);
  ptr = pthread_getspecific(thread_key);
  if (ptr == NULL) {
    /* Initialize of tftd_thread structure. */
    if (pthread_setspecific(thread_key, ptr) != 0) {
      perror("pthread_setspecific error");
      thread_quit();
    }
    ptr = (struct server_socket *)param;
  }

  if (param == NULL) {
    printf("param is NULL!\n");
    exit(0);
  }
  printf("server_main.server_socket: %p\n", param);

  for (cnt = 0; cnt < socket_threads; cnt++) {
    pthread_create(&(ptr->thread_tid[cnt]), NULL, 
		   (void *(*)(void *))&thread_main, (void *)ptr);
  }

  while (1) {
    pthread_mutex_lock(&(ptr->exit_mutex));
    while(ptr->exited_tid == PTHREAD_T_NULL) {
      pthread_cond_wait(&(ptr->thread_cond), &(ptr->exit_mutex));
    }
    for (cnt = 0; cnt < socket_threads; cnt++) {
      if (ptr->thread_tid[cnt] == ptr->exited_tid) {
	pthread_join(ptr->thread_tid[cnt], NULL);
	d_printf(1, ("[%d] is exited.\n",ptr->exited_tid));
#ifndef _DEBUG
	d_printf(1, "thread restart\n");
	pthread_create(&(ptr->thread_tid[cnt]), NULL,
		       (void *(*)(void *))&thread_main, (void *)ptr);
#endif
	ptr->exited_tid = PTHREAD_T_NULL;
      }
    }
    pthread_mutex_unlock(&(ptr->exit_mutex));
  }


}

void thread_main(void *param)
{
  int read;
  tftpd_thread *ptr;
  socklen_t len;
  struct sockaddr_storage ss;
  struct sockaddr *sa;
  struct server_socket *ssocket;
    
  sa = (struct sockaddr *)&ss;
  ssocket = (struct server_socket *)param;

  pthread_once(&thread_once, fun_thread_once);
  ptr = pthread_getspecific(thread_key);
  if (ptr == NULL) {
    /* Initialize of tftd_thread structure. */
    ptr = (tftpd_thread *)malloc(sizeof(tftpd_thread));
    ptr->cond = RUNNING;
    ptr->block_size = -1;
    ptr->newline = 0;
    ptr->prevchar = 0;
    ptr->ssocket = ssocket;
    memset(ptr->buf, 0, sizeof(char)*BUFSIZ);
    pthread_setspecific(thread_key, ptr);
  }

  read = recvfrom(ssocket->socket, ptr->buf, BUFSIZ, 0,
		  (struct sockaddr *)&(ptr->client_addr), &ssocket->addrlen);
  ptr->buflen = read;
  d_printf(5, ("thread (%d) reading (%d) byte...\n", pthread_self(), read));
  ptr->peer = socket(ssocket->socket_domain, 
		     ssocket->socket_type, 
		     ssocket->socket_protocol);
  if (ptr->peer == -1) {
    fprintf(stderr, "error socekt. \n");
    thread_quit();
  }
  sa->sa_family = ssocket->socket_domain;
  if(bind(ptr->peer, sa, ssocket->addrlen)< 0) {
    fprintf(stderr, "error bind.\n");
    thread_quit();
  }
  if(connect(ptr->peer, (struct sockaddr *)&(ptr->client_addr), 
	     ssocket->addrlen) < 0) {
    fprintf(stderr, "error connect.\n");
    thread_quit();
  }

  thread_packet_parse();
  close(ptr->peer);

  d_printf(1, ("client process finished(%d).\n", pthread_self()));

  thread_quit();

  return ;
}
#endif /* #ifdef TFTPD_V4ONLY */

#ifdef TFTPD_V4ONLY
void thread_quit(void)
{
  pthread_mutex_lock(&exit_mutex);
  exited_tid = pthread_self();
  pthread_cond_signal(&thread_cond);
  pthread_mutex_unlock(&exit_mutex);
  pthread_exit(NULL);
}
#else /* IPv6 */
void thread_quit(void)
{
  struct server_socket *ptr;
  tftpd_thread *tt;
  tt = pthread_getspecific(thread_key);
  ptr = tt->ssocket;
  d_printf(3, ("[%d]: server_socket = %p\n",pthread_self(), ptr));
  pthread_mutex_lock(&(ptr->exit_mutex));
  ptr->exited_tid = pthread_self();
  pthread_cond_signal(&(ptr->thread_cond));
  pthread_mutex_unlock(&(ptr->exit_mutex));
  pthread_exit(NULL);
}
#endif  /* #ifdef TFTPD_V4ONLY */

void send_error(int error)
{
  tftpd_thread *ptr;
  struct tftphdr *tphdr;
  struct errmsg *errptr;
  size_t len, send_len;

  ptr = pthread_getspecific(thread_key);
  tphdr = (struct tftphdr*)ptr->buf;
  tphdr->th_opcode = htons((u_short)ERROR);
  tphdr->th_code = htons((u_short)error);

  for (errptr = errmsgs; errptr->e_code != -1; errptr++) {
    if (errptr->e_code == error)  
      break;
  }

  if (errptr->e_code == -1) {
    tphdr->th_code = EUNDEF;
  }

  len = strlen(errptr->e_msg);
  strlcpy(tphdr->th_msg, errptr->e_msg, len);
  tphdr->th_msg[len] = '\0';
  len += 5; /* for opcode(2bytes) + errorcode(2bytes) + null(1byte) */
    
  if ((send_len = send(ptr->peer, ptr->buf, len, 0)) != len) {
    fprintf(stderr, "error when sending error packet.\n");
  }

  /*    syslog(LOG_ERR, "nak: %m"); */
  return ;
}

char *divide_token(char *src, char delim)
{
  char *ptr;
  size_t siz;
  siz = strlen(src);
  for (ptr = src; (ptr < src + siz || *ptr != '\0'); ptr++) {
    if (*ptr == delim) {
      *ptr = '\0';
    }
  }
  return ptr;
}

/* for being the daemon */
int serv_init(void)
{
  pid_t pid;
  
  if ((pid = fork()) < 0) {
    return -1;
  }
  else if (pid !=0) {
    exit(0);
  }
  setsid();
  /*  chdir("/"); */
  umask(0);
  init_signal();

  return 0;
}

void init_signal(void)
{
  signal(SIGKILL, quit);
  signal(SIGTERM, quit);
}

void quit(int sig)
{
#ifndef TFTPD_V4ONLY
  close(sockfd);
#endif
  exit(0);
}
