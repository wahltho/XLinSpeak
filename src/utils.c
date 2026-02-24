#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <spawn.h>

#include "utils.h"

#define XPLM200
#define APL 0
#define IBM 0
#define LIN 1
#include "XPLMUtilities.h"

#ifdef USE_SPEECHD
#include <speech-dispatcher/libspeechd.h>
#endif

extern char **environ;

#define TTS_QUEUE_CAP 64
#define TTS_MAX_TEXT 4096

struct tts_queue {
  char *items[TTS_QUEUE_CAP];
  int head;
  int tail;
  int count;
  bool stop;
  pthread_mutex_t mtx;
  pthread_cond_t cv;
};

struct tts_cmd {
  char **argv;
  int argc;
};

enum tts_backend {
  TTS_NONE = 0,
  TTS_PIPER = 1,
  TTS_SPEECHD = 2
};

static struct tts_queue queue_state;
static pthread_t worker_thread;
static bool worker_started = false;
static bool tts_ready = false;
static enum tts_backend backend = TTS_NONE;

static struct tts_cmd piper_cmd;
static struct tts_cmd sink_cmd;

#ifdef USE_SPEECHD
static SPDConnection *conn = NULL;
#endif

static char *msg = NULL;
static size_t msgSize = 0;

static void xcDebugInt(const char *format, va_list va)
{
  va_list vc;
  int res;
  if(msg == NULL){
    msgSize = 2;
    msg = (char *)malloc(msgSize);
  }
  if(msg == NULL){
    XPLMDebugString("XLinSpeak: Couldn't allocate buffer for messages!\n");
    return;
  }
  while(1){ /*looping once, in case of string too big*/
    /*copy, in case we need another go*/
    va_copy(vc, va);
    res = vsnprintf(msg, msgSize, format, vc);
    va_end(vc);

    if((res > -1) && ((size_t)res < msgSize)){
      XPLMDebugString(msg);
      return;
    }else{
      void *tmp;
      msgSize *= 2;
      if((tmp = realloc(msg, msgSize)) == NULL){
        break;
      }
      msg = (char *)tmp;
    }
  }

  XPLMDebugString("XLinSpeak: Problem with debug message formatting!\n");
  msg = NULL;
  msgSize = 0;
  return;
}

void xcDebug(const char *format, ...)
{
  va_list ap;
  va_start(ap,format);
  xcDebugInt(format, ap);
  va_end(ap);
}

static void argv_free(struct tts_cmd *cmd)
{
  int i;
  if(cmd == NULL || cmd->argv == NULL){
    return;
  }
  for(i = 0; i < cmd->argc; ++i){
    free(cmd->argv[i]);
  }
  free(cmd->argv);
  cmd->argv = NULL;
  cmd->argc = 0;
}

static bool argv_add_owned(struct tts_cmd *cmd, char *arg)
{
  char **tmp;
  if(cmd == NULL || arg == NULL){
    return false;
  }
  tmp = (char **)realloc(cmd->argv, sizeof(char *) * (cmd->argc + 2));
  if(tmp == NULL){
    return false;
  }
  cmd->argv = tmp;
  cmd->argv[cmd->argc] = arg;
  cmd->argc += 1;
  cmd->argv[cmd->argc] = NULL;
  return true;
}

static bool argv_add(struct tts_cmd *cmd, const char *arg)
{
  char *dup;
  if(arg == NULL){
    return false;
  }
  dup = strdup(arg);
  if(dup == NULL){
    return false;
  }
  if(!argv_add_owned(cmd, dup)){
    free(dup);
    return false;
  }
  return true;
}

static bool argv_add_split(struct tts_cmd *cmd, const char *args)
{
  const char *p;
  const char *start;
  if(args == NULL || *args == '\0'){
    return true;
  }
  p = args;
  while(*p){
    while(*p && isspace((unsigned char)*p)){
      ++p;
    }
    if(*p == '\0'){
      break;
    }
    start = p;
    while(*p && !isspace((unsigned char)*p)){
      ++p;
    }
    size_t len = (size_t)(p - start);
    char *tok = (char *)malloc(len + 1);
    if(tok == NULL){
      return false;
    }
    memcpy(tok, start, len);
    tok[len] = '\0';
    if(!argv_add_owned(cmd, tok)){
      free(tok);
      return false;
    }
  }
  return true;
}

static bool build_piper_cmd(void)
{
  const char *bin = getenv("PIPER_BIN");
  const char *args = getenv("PIPER_ARGS");
  const char *model = getenv("PIPER_MODEL");

  if(bin == NULL || *bin == '\0'){
    bin = "piper";
  }
  if(args == NULL || *args == '\0'){
    args = "--output_file -";
  }

  if(!argv_add(&piper_cmd, bin)){
    return false;
  }
  if(!argv_add_split(&piper_cmd, args)){
    return false;
  }
  if(model != NULL && *model != '\0'){
    if(!argv_add(&piper_cmd, "--model")){
      return false;
    }
    if(!argv_add(&piper_cmd, model)){
      return false;
    }
  }else{
    xcDebug("XLinSpeak: PIPER_MODEL not set, relying on PIPER_ARGS.\\n");
  }
  return true;
}

static bool build_sink_cmd(void)
{
  const char *sink = getenv("PIPER_SINK");
  if(sink == NULL || *sink == '\0'){
    sink = "aplay -q";
  }
  if(!argv_add_split(&sink_cmd, sink)){
    return false;
  }
  if(sink_cmd.argc == 0){
    return false;
  }
  return true;
}

static void queue_init(struct tts_queue *q)
{
  memset(q, 0, sizeof(*q));
  pthread_mutex_init(&q->mtx, NULL);
  pthread_cond_init(&q->cv, NULL);
}

static void queue_destroy(struct tts_queue *q)
{
  int i;
  if(q == NULL){
    return;
  }
  for(i = 0; i < q->count; ++i){
    int idx = (q->head + i) % TTS_QUEUE_CAP;
    free(q->items[idx]);
    q->items[idx] = NULL;
  }
  pthread_mutex_destroy(&q->mtx);
  pthread_cond_destroy(&q->cv);
  memset(q, 0, sizeof(*q));
}

static void queue_stop(struct tts_queue *q)
{
  pthread_mutex_lock(&q->mtx);
  q->stop = true;
  pthread_cond_broadcast(&q->cv);
  pthread_mutex_unlock(&q->mtx);
}

static void queue_push(struct tts_queue *q, const char *text)
{
  size_t len;
  char *copy;
  if(text == NULL || *text == '\0'){
    return;
  }
  len = strnlen(text, TTS_MAX_TEXT);
  copy = (char *)malloc(len + 1);
  if(copy == NULL){
    return;
  }
  memcpy(copy, text, len);
  copy[len] = '\0';

  pthread_mutex_lock(&q->mtx);
  if(q->stop){
    pthread_mutex_unlock(&q->mtx);
    free(copy);
    return;
  }
  if(q->count == TTS_QUEUE_CAP){
    free(q->items[q->head]);
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % TTS_QUEUE_CAP;
    q->count -= 1;
  }
  q->items[q->tail] = copy;
  q->tail = (q->tail + 1) % TTS_QUEUE_CAP;
  q->count += 1;
  pthread_cond_signal(&q->cv);
  pthread_mutex_unlock(&q->mtx);
}

static char *queue_pop(struct tts_queue *q)
{
  char *item = NULL;
  pthread_mutex_lock(&q->mtx);
  while(q->count == 0 && !q->stop){
    pthread_cond_wait(&q->cv, &q->mtx);
  }
  if(q->count > 0){
    item = q->items[q->head];
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % TTS_QUEUE_CAP;
    q->count -= 1;
  }
  pthread_mutex_unlock(&q->mtx);
  return item;
}

static void write_all(int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while(off < len){
    ssize_t res = write(fd, buf + off, len - off);
    if(res < 0){
      if(errno == EINTR){
        continue;
      }
      break;
    }
    if(res == 0){
      break;
    }
    off += (size_t)res;
  }
}

static int set_cloexec(int fd)
{
  int flags = fcntl(fd, F_GETFD);
  if(flags < 0){
    return -1;
  }
  return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static bool spawn_process(char *const argv[], int stdin_fd, int stdout_fd,
                          const int *close_fds, size_t close_count,
                          pid_t *pid_out)
{
  posix_spawn_file_actions_t actions;
  size_t i;
  int res;

  if(argv == NULL || argv[0] == NULL){
    return false;
  }

  if(posix_spawn_file_actions_init(&actions) != 0){
    return false;
  }

  if(stdin_fd >= 0){
    posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
  }
  if(stdout_fd >= 0){
    posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
  }
  for(i = 0; i < close_count; ++i){
    posix_spawn_file_actions_addclose(&actions, close_fds[i]);
  }

  res = posix_spawnp(pid_out, argv[0], &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);

  if(res != 0){
    errno = res;
    return false;
  }
  return true;
}

static void speak_piper(const char *text)
{
  int inpipe[2];
  int outpipe[2];
  pid_t piper_pid;
  pid_t sink_pid;
  int close_all[4];

  if(text == NULL || *text == '\0'){
    return;
  }

  if(pipe(inpipe) != 0){
    xcDebug("XLinSpeak: Piper pipe(in) failed: %d\n", errno);
    return;
  }
  if(pipe(outpipe) != 0){
    xcDebug("XLinSpeak: Piper pipe(out) failed: %d\n", errno);
    close(inpipe[0]);
    close(inpipe[1]);
    return;
  }

  set_cloexec(inpipe[0]);
  set_cloexec(inpipe[1]);
  set_cloexec(outpipe[0]);
  set_cloexec(outpipe[1]);

  close_all[0] = inpipe[0];
  close_all[1] = inpipe[1];
  close_all[2] = outpipe[0];
  close_all[3] = outpipe[1];

  if(!spawn_process(piper_cmd.argv, inpipe[0], outpipe[1], close_all, 4, &piper_pid)){
    xcDebug("XLinSpeak: Piper spawn failed: %d\n", errno);
    close(inpipe[0]);
    close(inpipe[1]);
    close(outpipe[0]);
    close(outpipe[1]);
    return;
  }

  if(!spawn_process(sink_cmd.argv, outpipe[0], -1, close_all, 4, &sink_pid)){
    xcDebug("XLinSpeak: Sink spawn failed: %d\n", errno);
    kill(piper_pid, SIGTERM);
    waitpid(piper_pid, NULL, 0);
    close(inpipe[0]);
    close(inpipe[1]);
    close(outpipe[0]);
    close(outpipe[1]);
    return;
  }

  close(inpipe[0]);
  close(outpipe[0]);
  close(outpipe[1]);

  write_all(inpipe[1], text, strlen(text));
  write_all(inpipe[1], "\n", 1);
  close(inpipe[1]);

  waitpid(piper_pid, NULL, 0);
  waitpid(sink_pid, NULL, 0);
}

#ifdef USE_SPEECHD
static bool speechd_init(void)
{
  conn = spd_open("XLinSpeak", "Main", "player", SPD_MODE_SINGLE);
  if(conn == NULL){
    xcDebug("XLinSpeak: Couldn't init speech-dispatcher!\n");
    return false;
  }
  return true;
}

static void speechd_say(const char *str)
{
  if(conn != NULL){
    spd_say(conn, SPD_MESSAGE, str);
  }
}

static void speechd_close(void)
{
  if(conn != NULL){
    spd_close(conn);
    conn = NULL;
  }
}
#endif

static void backend_say(const char *text)
{
  switch(backend){
    case TTS_PIPER:
      speak_piper(text);
      break;
    case TTS_SPEECHD:
#ifdef USE_SPEECHD
      speechd_say(text);
#endif
      break;
    default:
      break;
  }
}

static void *tts_worker(void *arg)
{
  (void)arg;
  while(1){
    char *text = queue_pop(&queue_state);
    if(text == NULL){
      break;
    }
    backend_say(text);
    free(text);
  }
  return NULL;
}

bool speech_init(void)
{
  if(tts_ready){
    return true;
  }

  queue_init(&queue_state);

  if(build_piper_cmd() && build_sink_cmd()){
    backend = TTS_PIPER;
    xcDebug("XLinSpeak: Piper backend enabled.\n");
  }else{
    argv_free(&piper_cmd);
    argv_free(&sink_cmd);
#ifdef USE_SPEECHD
    if(speechd_init()){
      backend = TTS_SPEECHD;
      xcDebug("XLinSpeak: speech-dispatcher backend enabled.\n");
    }else{
      backend = TTS_NONE;
    }
#else
    backend = TTS_NONE;
#endif
  }

  if(backend == TTS_NONE){
    xcDebug("XLinSpeak: No TTS backend available.\n");
    queue_destroy(&queue_state);
    return false;
  }

  if(pthread_create(&worker_thread, NULL, tts_worker, NULL) != 0){
    xcDebug("XLinSpeak: Couldn't start TTS worker thread.\n");
    queue_destroy(&queue_state);
    if(backend == TTS_PIPER){
      argv_free(&piper_cmd);
      argv_free(&sink_cmd);
    }
#ifdef USE_SPEECHD
    if(backend == TTS_SPEECHD){
      speechd_close();
    }
#endif
    backend = TTS_NONE;
    return false;
  }

  worker_started = true;
  tts_ready = true;
  return true;
}

void speech_say(char *str)
{
  if(!tts_ready || str == NULL){
    return;
  }
  queue_push(&queue_state, str);
}

void speech_close(void)
{
  if(!tts_ready){
    return;
  }
  tts_ready = false;
  queue_stop(&queue_state);
  if(worker_started){
    pthread_join(worker_thread, NULL);
    worker_started = false;
  }

  queue_destroy(&queue_state);

  if(backend == TTS_PIPER){
    argv_free(&piper_cmd);
    argv_free(&sink_cmd);
  }

#ifdef USE_SPEECHD
  if(backend == TTS_SPEECHD){
    speechd_close();
  }
#endif

  backend = TTS_NONE;
}
