#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int
memtestchild(void *x, void *y)
{
  char *sharedmem = (char *)y;
  char *a = (char *)x;

  while(sharedmem[0] == (int)0)
    sleep(1);

  sharedmem[1] = 1;
  a[0] = 1;

  while(sharedmem[2] == 0)
   sleep(1);

  exit();

}

int
memtest1(void)
{
  char *buf;
  char *a = 0;
  char *b1, *b2;
  char *sharedmem = malloc(10);
  for (int i = 0; i < 10; ++i) sharedmem[i] = 0;
  int retval;

  b1 = sbrk(0);
  b2 = sbrk(4096);
  if (b2 < 0) {
    printf(1, "sbrk failed\n");
    return 1;
  }

  buf = b1;

  a = sbrk(10);

  retval = clone(memtestchild, (void *)a, (void *)sharedmem, buf + 4096, 0);
  if (retval < 0) {
    printf(1, "Clone failed, retval = %d\n", retval);
    return 1;
  }

  sharedmem[0] = 1;

  while(sharedmem[1] == (int)0)
    sleep(1);

  sbrk(-10);
  
  sharedmem[2] = 1;

  join(retval);
  sbrk(-4096);

  return 0;
}

int
jointestchild(void *a, void *b)
{
  int *x = (int *)a;
  int *y = (int *)b;
  int temp;
  temp = *x;
  *x = *y;
  *y = temp;
  sleep(100);
  exit();
}

int
jointest(void)
{
  int a, b;
  a = 42;
  b = 25;
  int retval;
  char *buffer;
  buffer = sbrk(4096);
  if (buffer == (char *)-1)
    return -1;
  retval = clone(jointestchild, (void *)&a, (void *)&b, buffer + 4096, 0);
  join(retval);
  sbrk(-4096);
  if (a == 25 && b == 42)
    printf(1, "Join test succeeded\n");
  else
    printf(1, "Join test failed\n");
  return 0;
}

int
jointestchild1(void *a, void *b)
{
  int x = *((int *)a);
  sleep(x);
  exit();
}

int
jointest1(void)
{
  int tid1, tid2;
  int ret1, ret2;
  int one, two;
  char *buffer1, *buffer2;
  buffer1 = sbrk(4096);
  buffer2 = sbrk(4096);
  one = 50;
  two = 100;
  tid1 = clone(jointestchild1, (void *)&one, 0, buffer1 + 4096, 0);
  tid2 = clone(jointestchild1, (void *)&two, 0, buffer2 + 4096, 0);
  sbrk(-8192);
  ret1 = join(tid1);
  ret2 = join(tid2);
  if (ret1 == tid1 && ret2 == tid2)
    printf(1, "Joining order test succeeded\n");
  else
    printf(1, "Joining order test failed\n");
  return 0;
}

int
waitjointest(void)
{
  int tgid1, tgid2;
  int tid11, tid12, tid21, tid22;
  int ret11, ret12, ret21, ret22;
  int one1, two1, one2, two2;
  char *stack11, *stack12, *stack21, *stack22;
  one1 = 1;
  two1 = 1;
  one2 = 1;
  two2 = 1;

  tgid1 = fork();
  if (!tgid1){
    // child
    stack11 = sbrk(4096);
    stack12 = sbrk(4096);
    tid11 = clone(jointestchild1, &one1, 0, stack11 + 4096, 0);
    tid12 = clone(jointestchild1, &two1, 0, stack12 + 4096, 0);
    ret11 = join(tid11);
    ret12 = join(tid12);
    sbrk(-8192);
    sleep(5);
    if (ret11 != tid11 || ret12 != tid12)
      printf(1, "Wait reaped before join, wait-join exclusivity test failed\n");
    exit();
  }

  tgid2 = fork();
  if (!tgid2){
    // child
    stack21 = sbrk(4096);
    stack22 = sbrk(4096);
    tid21 = clone(jointestchild1, &one2, 0, stack21 + 4096, 0);
    tid22 = clone(jointestchild1, &two2, 0, stack22 + 4096, 0);
    ret21 = join(tid21);
    ret22 = join(tid22);
    sbrk(-8192);
    if (ret21 != tid21 || ret22 != tid22)
      printf(1, "Wait reaped before join, wait-join exclusivity test failed\n");
    exit();
  }

  int ret;
  while((ret = wait()) != -1);

  return 0;

}

int
wait_er(void *a, void *b)
{
  int ret;
  while((ret = wait()) != -1);
  exit();
}

int 
childwaittest(void)
{
  char *stack;
  stack = sbrk(4096);  
  int tid;
  int ret;
  ret = fork();
  if (!ret){
    printf(1, "Child process in waiter thread test\n");
    sleep(20);
    exit();
  }
  tid = clone(wait_er, 0, 0, stack + 4096, 0);
  ret = join(tid);
  ret = wait();
  if (ret != -1){
    printf(1, "Waiter thread test failed\n");
  }
  else {
    printf(1, "Waiter thread test succeeded\n");
  }
  sbrk(-4096);
  return 0;
}

char *execargs[] = {"ls", 0};
int
execchild(void *a, void *b)
{
  int ret;
  ret = exec("ls", execargs);
  if(ret == -1){
    printf(1, "exec(ls) failed\n");
  }
  return 0;
}

int
exectest(void)
{
  int ret1, ret2;
  int tid1, tid2;
  char *stack1, *stack2;
  int thousand = 10;
  ret1 = fork();
  if(!ret1){
    stack1 = sbrk(4096);
    stack2 = sbrk(4096);
    tid1 = clone(jointestchild1, &thousand, 0, stack1 + 4096, 0);
    tid2 = clone(execchild, 0, 0, stack2 + 4096, 0);
    join(tid2);
    printf(1, "exec test failed\n");
    join(tid1);
    exit();
  }
  else{
    ret2 = wait();
    if(ret1 == ret2){
      printf(1, "exec test succeeded\n");
    }
    else{
      printf(1, "exec test failed (%d, %d)\n", ret1, ret2);
    }
  }
  return 0;
}

int
racer(void *count, void *dummy)
{
  int i;
  int countt = *((int *)count);
  for(i = 0; i < countt; i++){
    sbrk(1);
  }
  exit();
}

int 
memtest(void)
{
  char *stack1, *stack2;
  stack1 = malloc(4096);
  stack2 = malloc(4096);
  int count = 100;
  char *initialstack = sbrk(0);
  int ret1, ret2;
  ret1 = clone(racer, (void *)&count, 0, stack1, 0);
  ret2 = clone(racer, (void *)&count, 0, stack2, 0);
  join(ret1);
  join(ret2);
  char *finalstack = sbrk(0);
  if((finalstack - initialstack) != (count * 2)){
    printf(1, "memtest failed\n");
  }
  else{
    printf(1, "memtest succeeded\n");
  }
  return 0;
}

int
cottonthread(void *a, void *b)
{
  printf(1, "%d\n", *((int *)a));
  cthread_exit();
}

#define NT 60
int
cottontest1(void)
{
  cthread_t threads[NT];
  int arr[NT];
  int i;

  for (i = 0; i < NT; i++){
    arr[i] = i;
    cthread_create(&threads[i], cottonthread, (void *)&arr[i], 0);
  }
  
  for (i = 0; i < NT; i++){
    cthread_join(&threads[i]);
  }

  return 0;
}

int
twoexectest(void)
{
  cthread_t t0, t1, t2;
  int ret;
  ret = fork();
  if(!ret){
    cthread_create(&t0, wait_er, 0, 0);
    cthread_create(&t1, execchild, 0, 0);
    cthread_create(&t2, execchild, 0, 0);
    cthread_join(&t2);
  }
  wait();
  printf(1, "twoexec test passed\n");
  return 0;
}

#define TOO_MANY 100
int
toomanythreadstest(void)
{
  cthread_t arr[TOO_MANY];
  int i, j, ret;
  char *initial = sbrk(0);
  char *final;
  int flag = 0;
  int time = 1000;
  for(i = 0; i < TOO_MANY; i++){
    ret = cthread_create(&arr[i], jointestchild1, &time, 0);
    if(ret == -1){
      break;
    }
  }
  printf(1, "%d threads spawned\n", i);
  if(i < 61){
    // NPROC is 64
    printf(1, "toomanythreads test failed\n");
    flag = 1;
  }
  for (j = 0; j < i; j++){
    ret = cthread_join(&arr[j]);
  }
  final = sbrk(0);
  printf(1, "toomanythreads: %d -> %d\n", initial, final);
  if(!flag)
    printf(1, "toomanythreads test passed\n");
  return 0;
}

int
cottonticket(void *a, void *b)
{
  slock_t *lk = (slock_t *)b;
  slock_acquire(lk);
  printf(1, "%d\n", getpid());
  slock_release(lk);
  exit();
}

#define NT 60
int
tickettest(void)
{
  cthread_t threads[NT];
  int arr[NT];
  int i;
  slock_t lock;
  slock_init(&lock);

  for (i = 0; i < NT; i++){
    arr[i] = i;
    cthread_create(&threads[i], cottonticket, (void *)&arr[i], (void *)&lock);
  }
  
  for (i = 0; i < NT; i++){
    cthread_join(&threads[i]);
  }

  return 0;
}

#define TIMES 1000
int
incracer(void *a, void *b)
{
  int *x = (int *)a;
  int i;
  slock_t *lk = (slock_t *)b;
  for(i = 0; i < TIMES; i++){
    slock_acquire(lk);
    *x += 1;
    slock_release(lk);
  }
  cthread_exit();
}

int
racetest(void){
  cthread_t threads[NT];
  int i;
  int val = 0;
  slock_t lock;
  slock_init(&lock);

  for (i = 0; i < NT; i++){
    cthread_create(&threads[i], incracer, (void *)&val, (void *)&lock);
  }
  
  for (i = 0; i < NT; i++){
    cthread_join(&threads[i]);
  }

  if(val != NT * TIMES){
    printf(1, "racetest failed\n");
  }else{
    printf(1, "racetest succeeded\n");
  }
  return 0;
}

int 
wayvard_child(void *time, void *flag)
{
  int *x, *y;
  x = (int *)time;
  y = (int *)flag;
  sleep(*x);
  *y = 1;
  exit();
}

int 
childkilltest(void)
{
  cthread_t child;
  int count = 1000;
  int value = 0;
  int pid;
  pid = cthread_create(&child, wayvard_child, (void *)count, (void *)value);
  kill(pid);
  join(pid);
  if(value)
    printf(1, "childkilltest failed\n");
  else
    printf(1, "childkilltest suceeded\n");
  return 0;
}

// T1 increases process size
// T2 checks arguments of a system call, finds them to be fine
// T1 decreases the process size
// T2 dereferences the argument (which is a buffer), and gets killed due to out
// of bounds access

struct baton{
  int turn;
  slock_t lock;
};
#define BUFFERSIZE (4096 * 4)

int
vmmessfunc(void *a, void *b){
  char *buf = (char *)b;
  char *flag = (char *)a;
  int fp;
  int ret;
  fp = open("data.bin", O_RDONLY);
  while(!(*flag));
  ret = read(fp, buf, BUFFERSIZE);

  if(ret == -1)
    printf(1, "vmsynctest succeeded\n");
  
  cthread_exit();
  close(fp);
}

int
vmsynctest(void)
{
  char *stack = sbrk(4096);
  char *buf = sbrk(BUFFERSIZE);
  int ret;
  char flag = 0;
  int fp;
  memset(buf, '0', BUFFERSIZE);
  fp = open("data.bin", O_CREATE | O_WRONLY);
  write(fp, buf, BUFFERSIZE);
  memset(buf, '1', BUFFERSIZE);
  ret = clone(vmmessfunc, (void *)&flag, (void *)buf, stack + 4096, 0);
  flag = 1;
  sbrk(-BUFFERSIZE); 
  join(ret);
  close(fp);
  return 0;
}

int
checker(void *a, void *b){
  char *buf = (char *)a;
  char *ch = (char *)b;
  int i;
  for(i = 0; i < 20000; ++i){
     if(buf[i] != '0'){
       printf(1, "vmemtest failed at %d\n", i);
       *ch = 1;
       break;
     }
  }
  exit();
}

int
vmemtest(void){
  char *buffer;
  buffer = malloc(20000);
  memset(buffer, '0', 20000);
  char ch = 0;
  cthread_t thread;
  cthread_create(&thread, checker, (void *)buffer, (void *)&ch);
  cthread_join(&thread);
  if(!ch)
    printf(1, "vmemtest succeeded\n");
  free(buffer);
  return 0;
}

#define BUF 1024

int 
cwdchild(void *a, void *b)
{
  char *buf;
  int fp;
  buf = (char *)malloc(BUF);
  memset(buf, '0', BUF);
  mkdir("testdir");
  chdir("testdir");
  fp = open("cwdtest", O_CREATE | O_WRONLY);
  if(fp == -1)
    printf(1, "unable to create 'cwdtest'\n");
  write(fp, buf, BUF);
  free(buf);
  close(fp);
  exit();
}

int 
cwdsynctest(void)
{
  char *stack = sbrk(4096);
  int ret;
  int fp;
  
  ret = clone(cwdchild, 0, 0, stack + 4096, 0);
  if(ret == -1)
    printf(1, "unable to clone\n");
  join(ret);
  fp = open("cwdtest", O_RDONLY);
  if(fp == -1){
    printf(1, "cwdsynctest failed\n");
    chdir("testdir");
    unlink("cwdtest");
    chdir("../");
    unlink("testdir");
  }else {
    close(fp);
    printf(1, "cwdsynctest succeeded\n");
  }
  return 0;
}

#define SMALLBUFFERSIZE 512

struct container{
  char *buffer;
  int *pipefd;
};

int
clonevmmessfunc(void *a, void *b){
  char *flag = (char *)a;
  int fp;
  int ret;
  struct container *x = (struct container *)b;
  char *buf = x->buffer;
  int *pipefd = x->pipefd;
  while(!(*flag));
  ret = read(pipefd[0], buf, SMALLBUFFERSIZE);

  if(ret == -1)
    printf(1, "clonevmsynctest succeeded\n");
  
  cthread_exit();
  close(fp);
}

// To be performed while having the TOCTOU blocks in proc.c and somewhere else
// uncommented
int
pipevmsynctest(void)
{
  char *stack = sbrk(4096);
  char *buf = sbrk(SMALLBUFFERSIZE);
  int ret;
  char flag = 0;
  int pipefd[2];

  struct container cont;
  cont.buffer = buf;
  cont.pipefd = pipefd;

  memset(buf, '0', SMALLBUFFERSIZE);
  pipe(pipefd);
  write(pipefd[1], buf, SMALLBUFFERSIZE);
  memset(buf, '1', SMALLBUFFERSIZE);
  ret = clone(clonevmmessfunc, (void *)&flag, (void *)&cont, stack + 4096, 0);
  flag = 1;
  sbrk(-SMALLBUFFERSIZE); 
  join(ret);
  close(pipefd[0]);
  close(pipefd[1]);
  return 0;
}

typedef struct{
  slock_t lockt;
  int threadsready;
  int parentready;
}baton;

int 
tlb_child(void *ready, void *page_p)
{
  baton *b = (baton *)ready;
  char *page = (char *)page_p;
  int i;
  /* fill TLB with this entry */
  for(i = 0; i < 1024; i++){
    page[0]++;
  }
  slock_acquire(&(b->lockt));
  b->threadsready++;
  slock_release(&(b->lockt));
  while(!b->parentready)
    ;
  /* check whether TLB entry accessed or new entry */
  for(i = 0; i < 1024; i++){
    page[0]--;
  }
  slock_acquire(&(b->lockt));
  b->threadsready--;
  slock_release(&(b->lockt));
  exit();
}

#define TLB_N 1
#define REPS 1
int
tlbtest(void)
{
  char *pages[TLB_N];
  int pid[TLB_N];
  baton bt;
  int i;
  int reps;
  for(reps = 0; reps < REPS; reps++){
    bt.threadsready = 0;
    bt.parentready = 0;
    slock_init(&bt.lockt);
    for(i = 0; i < TLB_N; i++){
      pages[i] = sbrk(4096);
    }
    char *page = sbrk(4096);
    for(i = 0; i < TLB_N; i++){
      pid[i] = clone(tlb_child, (void *)&bt, (void *)&page[i], (void *)pages[i] + 4096, 0);
    }
    while(bt.threadsready != TLB_N)
      ;
    /* children have loaded TLB, now invalidate the page */
    sbrk(-4096);
    /* allow threads to access the page */
    bt.parentready = 1;
    /* wait for threads to die or to access and increment */
    sleep(5);
    if(!bt.threadsready){
      printf(1, "tlbtest failed\n");
      break;
    }
    sbrk(-TLB_N * 4096);
    for(i = 0; i < TLB_N; i++)
      join(pid[i]);
  }
  if(bt.threadsready)
    printf(1, "tlbtest succeeded\n");
  exit();
}

  
int 
main(void)
{
  memtest1();
  jointest();
  jointest1();
  waitjointest();
  childwaittest();
  exectest();
  memtest();
  cottontest1();
  twoexectest();
  //toomanythreadstest();
  tickettest();
  racetest();
  childkilltest();
  vmemtest();
  vmsynctest();
  cwdsynctest();
  pipevmsynctest();
  tlbtest();
  exit();
}
