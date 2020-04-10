struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int clone(int (*fn)(void *, void *), void *arg1, void *arg2, void *child_stack, int flags);
int join(int pid);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);

// ucthreads.c
typedef struct{
  int pid;
  char *stack;
}cthread_t;
int cthread_create(cthread_t *thread, int (*fn)(void *, void *), void *arg1, void *arg2);
int cthread_cut(cthread_t *thread);
int cthread_join(cthread_t *thread);
void cthread_exit(void) __attribute__((noreturn));

// ticket lock
typedef struct{
  uint ticket;
  uint turn;
}tlock_t;

void tlock_init(tlock_t *);
void tlock_acquire(tlock_t *);
void tlock_release(tlock_t *);

// spinlock
typedef uint slock_t;

void slock_init(slock_t *);
void slock_acquire(slock_t *);
void slock_release(slock_t *);

