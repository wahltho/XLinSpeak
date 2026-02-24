#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
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
#ifdef USE_PULSE
#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/sample.h>
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

#ifdef USE_PULSE
static bool pulse_enabled = false;
static pa_simple *pulse_stream = NULL;
static pa_sample_spec pulse_spec;
#endif

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

static bool env_is_true(const char *name)
{
  const char *val = getenv(name);
  if(val == NULL){
    return false;
  }
  if((strcasecmp(val, "1") == 0) ||
     (strcasecmp(val, "true") == 0) ||
     (strcasecmp(val, "yes") == 0) ||
     (strcasecmp(val, "on") == 0)){
    return true;
  }
  return false;
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

static bool read_exact(int fd, void *buf, size_t len)
{
  size_t off = 0;
  while(off < len){
    ssize_t res = read(fd, (uint8_t *)buf + off, len - off);
    if(res < 0){
      if(errno == EINTR){
        continue;
      }
      return false;
    }
    if(res == 0){
      return false;
    }
    off += (size_t)res;
  }
  return true;
}

static bool skip_bytes(int fd, size_t len)
{
  uint8_t tmp[512];
  while(len > 0){
    size_t chunk = len > sizeof(tmp) ? sizeof(tmp) : len;
    if(!read_exact(fd, tmp, chunk)){
      return false;
    }
    len -= chunk;
  }
  return true;
}

static uint16_t le16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

struct wav_info {
  uint16_t format;
  uint16_t channels;
  uint32_t sample_rate;
  uint16_t bits_per_sample;
};

static bool wav_read_header(int fd, struct wav_info *info)
{
  uint8_t hdr[12];
  bool got_fmt = false;
  bool got_data = false;

  if(!read_exact(fd, hdr, sizeof(hdr))){
    return false;
  }
  if(memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0){
    return false;
  }

  while(!got_data){
    uint8_t chunk[8];
    uint32_t size;
    if(!read_exact(fd, chunk, sizeof(chunk))){
      return false;
    }
    size = le32(chunk + 4);

    if(memcmp(chunk, "fmt ", 4) == 0){
      uint8_t fmt[16];
      if(size < 16){
        return false;
      }
      if(!read_exact(fd, fmt, 16)){
        return false;
      }
      info->format = le16(fmt + 0);
      info->channels = le16(fmt + 2);
      info->sample_rate = le32(fmt + 4);
      info->bits_per_sample = le16(fmt + 14);
      got_fmt = true;
      if(size > 16){
        if(!skip_bytes(fd, size - 16)){
          return false;
        }
      }
      if(size & 1){
        if(!skip_bytes(fd, 1)){
          return false;
        }
      }
    }else if(memcmp(chunk, "data", 4) == 0){
      got_data = true;
    }else{
      if(!skip_bytes(fd, size)){
        return false;
      }
      if(size & 1){
        if(!skip_bytes(fd, 1)){
          return false;
        }
      }
    }
  }

  return got_fmt;
}

#ifdef USE_PULSE
static bool pulse_open(const struct wav_info *info)
{
  int err;
  pa_sample_spec spec;

  if(info->format != 1){
    xcDebug("XLinSpeak: Pulse only supports PCM WAV from Piper.\n");
    return false;
  }
  if(info->bits_per_sample != 16){
    xcDebug("XLinSpeak: Pulse only supports 16-bit PCM from Piper.\n");
    return false;
  }
  if(info->channels == 0 || info->sample_rate == 0){
    xcDebug("XLinSpeak: Invalid WAV header from Piper.\n");
    return false;
  }

  spec.format = PA_SAMPLE_S16LE;
  spec.rate = info->sample_rate;
  spec.channels = (uint8_t)info->channels;

  if(!pa_sample_spec_valid(&spec)){
    xcDebug("XLinSpeak: Invalid Pulse sample spec.\n");
    return false;
  }

  if(pulse_stream != NULL){
    if(pulse_spec.rate == spec.rate &&
       pulse_spec.channels == spec.channels &&
       pulse_spec.format == spec.format){
      return true;
    }
    pa_simple_free(pulse_stream);
    pulse_stream = NULL;
  }

  pulse_stream = pa_simple_new(NULL, "XLinSpeak", PA_STREAM_PLAYBACK, NULL,
                               "Piper", &spec, NULL, NULL, &err);
  if(pulse_stream == NULL){
    xcDebug("XLinSpeak: Pulse open failed: %s\n", pa_strerror(err));
    return false;
  }
  pulse_spec = spec;
  return true;
}
#endif

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

  close(inpipe[0]);
  close(outpipe[1]);

  write_all(inpipe[1], text, strlen(text));
  write_all(inpipe[1], "\n", 1);
  close(inpipe[1]);

#ifdef USE_PULSE
  if(pulse_enabled){
    struct wav_info info;
    uint8_t buf[4096];
    ssize_t r;
    int err;
    bool ok = wav_read_header(outpipe[0], &info);

    if(ok && pulse_open(&info)){
      while((r = read(outpipe[0], buf, sizeof(buf))) > 0){
        if(pa_simple_write(pulse_stream, buf, (size_t)r, &err) < 0){
          xcDebug("XLinSpeak: Pulse write failed: %s\n", pa_strerror(err));
          break;
        }
      }
      pa_simple_drain(pulse_stream, &err);
    }else{
      xcDebug("XLinSpeak: Piper WAV header invalid or Pulse unavailable.\n");
      while(read(outpipe[0], buf, sizeof(buf)) > 0){
        /* discard */
      }
    }
    close(outpipe[0]);
    waitpid(piper_pid, NULL, 0);
    return;
  }
#endif

  if(!spawn_process(sink_cmd.argv, outpipe[0], -1, close_all, 4, &sink_pid)){
    xcDebug("XLinSpeak: Sink spawn failed: %d\n", errno);
    kill(piper_pid, SIGTERM);
    waitpid(piper_pid, NULL, 0);
    close(outpipe[0]);
    return;
  }

  close(outpipe[0]);
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
#ifdef USE_PULSE
    pulse_enabled = env_is_true("PIPER_PULSE");
    if(pulse_enabled){
      xcDebug("XLinSpeak: Pulse backend enabled (PIPER_PULSE=1).\n");
    }
#endif
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

#ifdef USE_PULSE
  if(pulse_stream != NULL){
    int err;
    pa_simple_drain(pulse_stream, &err);
    pa_simple_free(pulse_stream);
    pulse_stream = NULL;
  }
#endif

#ifdef USE_SPEECHD
  if(backend == TTS_SPEECHD){
    speechd_close();
  }
#endif

  backend = TTS_NONE;
}
