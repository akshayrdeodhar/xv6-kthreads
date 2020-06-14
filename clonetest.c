#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

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

// creates a thread, passes the address of two variables
// calls join to wait on the thread
// thread swaps the values of the two variables (shared memory)
// if values are swapped when main thread comes out of join(), test OK
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
    printf(1, "jointest ok\n");
  else
    printf(1, "jointest failed\n");
  return 0;
}

int
jointestchild1(void *a, void *b)
{
  int x = *((int *)a);
  sleep(x);
  exit();
}

// creates two threads using clone, the first one sleeps for 100ms
// second one sleeps for 50ms. 
// join is called on the first thread. join should return on first thread 
// even if the second thread exits first
// also ensures that wait is not cleaning up before join
int
jointest1(void)
{
  int tid1, tid2;
  int ret1, ret2;
  int one, two;
  char *buffer1, *buffer2;
  buffer1 = sbrk(4096);
  buffer2 = sbrk(4096);
  one = 100;
  two = 50;
  tid1 = clone(jointestchild1, (void *)&one, 0, buffer1 + 4096, 0);
  tid2 = clone(jointestchild1, (void *)&two, 0, buffer2 + 4096, 0);
  ret1 = join(tid1);
  ret2 = join(tid2);
  if (ret1 == tid1 && ret2 == tid2)
    printf(1, "joining order test ok\n");
  else
    printf(1, "joining order test failed\n");
  return 0;
}

// forks
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

// process forks, and the child is made to sleep for 30ms
// parent clones, and thread is made to call wait
// parent joins the cloned thread 
// parent calls wait. wait should return -1
// thread created using clone should be able to wait on child processes
int 
childwaittest(void)
{
  char *stack;
  stack = sbrk(4096);  
  int tid;
  int ret;
  ret = fork();
  if (!ret){
    sleep(30);
    exit();
  }
  tid = clone(wait_er, 0, 0, stack + 4096, 0);
  ret = join(tid);
  ret = wait();
  if (ret != -1){
    printf(1, "Waiter thread test failed\n");
  }
  else {
    printf(1, "Waiter thread test ok\n");
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

// process forks
// int he child process, two threads are created, one dummy thread, and one
// which exec()s
// the child process tries to *join* the thread which calls exec. 
// this join should fail as all threads should be terminated by exec
// the parent process waits. wait should return the pid of the child process
// because the exec() should transfer the pid to the thread calling exec()
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
      printf(1, "exec test ok\n");
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

// meant to test that sbrk() work properly in multithreaded env
// process creates two threads, which sbrk(1) in a loop, 100 times
// process joins the two threads
// the process size after the join should be initial process size + 200 
// (if sz were not protected by a lock, there might be a race, which will mean
// that the process size is different)
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
    printf(1, "memtest ok\n");
  }
  free(stack1);
  free(stack2);
  return 0;
}

int
cottonthread(void *a, void *b)
{
  //printf(1, "%d\n", *((int *)a));
  cthread_exit();
}

#define NT 61
// size of ptable.proc is 64
// so there are 61 remaining slots after init, sh, clonetests
// 61 threads should be created and joined
int
cottontest1(void)
{
  cthread_t threads[NT];
  int arr[NT];
  int i;
  int ret;
  int failed = 0;

  for (i = 0; i < NT; i++){
    arr[i] = i;
    if((ret = cthread_create(&threads[i], cottonthread, (void *)&arr[i], 0)) == -1){
      printf(1, "cotton thread test failed\n");
      failed = 1;
    }
  }
  
  for (i = 0; i < NT; i++){
    if((ret = cthread_join(&threads[i])) != threads[i].pid){
      printf(1, "cotton thread test failed\n");
      failed = 1;
    }
  }

  if(!failed)
    printf(1, "cotton thread test ok\n");

  return 0;
}

// this was for weeding out a specific issue where two threads called exec() at
// the same time, one of them would *not* get killed
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
  int time = 100;
  for(i = 0; i < TOO_MANY; i++){
    ret = cthread_create(&arr[i], jointestchild1, &time, 0);
    if(ret == -1){
      break;
    }
  }
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
  tlock_t *lk = (tlock_t *)b;
  tlock_acquire(lk);
  printf(1, "%d\t", getpid());
  tlock_release(lk);
  exit();
}

// threads should print their PID in the order which they were spawned (ideally)
// could not think of a way to test this, could verify visually.
#define NT 61
int
tickettest(void)
{
  cthread_t threads[NT];
  int arr[NT];
  int i;
  tlock_t lock;
  tlock_init(&lock);

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

// 61 threads race on incrementing a single integer 1000 times each, with access
// syncrhonised using a lock
// the value of the variable after joining should be (1000 * NT)
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
    printf(1, "racetest ok\n");
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

// child should be killed before it changes the value of the flag
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
    printf(1, "vmsynctest ok\n");
  else
    printf(1, "vmsynctest failed\n");
  
  cthread_exit();
  close(fp);
}

// process creates child, passes it a buffer
// sets flag and immediately sbrk's the buffer out of the memory space
// read should fail in the child. 
// yields() have been added in the kernel which cause wrong access when safe copy primitives
// are not used
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

// buffer which spans multiple pages in heap created, ands set to some value
// the buffer should have the same value in the child
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
    printf(1, "vmemtest ok\n");
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

// the child creates a directory, changes working directory
// creates a file in the new working directory, writes bytes to it and exits
// the parent joins the child thread. the parent should be able to open the file
// and the file should have the same contents
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
    unlink("cwdtest");
    chdir("../");
    unlink("testdir");
    printf(1, "cwdsynctest ok\n");
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
    printf(1, "clonevmsynctest ok\n");
  
  cthread_exit();
  close(fp);
}

// similar to the filewrite safe copy primitives test, except for pipes
// not using safe copy primitives causes TOCTOU panics in the kernel
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

// create children which repeatedly access the same location in a page allocated
// in the parent resulting in a tlb entry. Then sets a flag
// the parent on seeing the flag invalidates the page, and sets another flag
// which causes the thread to proceed, and try to access the same location
// this should kill the thread, and not allow it to proceed, (because the TLB
// entry was invalidated)
// this test fails, as of now. too many complications in using multiprocessor
// interrupts
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
    printf(1, "tlbtest ok\n");
  return 0;
}

int
condvarchild(void *var, void *b)
{
  int *val2 = (int *)b;
  park(var);
  *val2 = 1;
  cthread_exit();
}

// parent waits for 5 second. The child parks
// The child should not change the variable till
// it is unparked
int
parkunparktest(void)
{
  cthread_t child;
  int var = 0;
  int other = 0;
  cthread_create(&child, condvarchild, (void *)&var, (void *)&other);
  var = 0;
  sleep(50);
  if(other){
    printf(1, "parkunparktest failed\n");
    kill(child.pid);
    return 0;
  }
  var = 1;
  unpark(child.pid, (void *)&var);
  cthread_join(&child);
  printf(1, "parkunparktest ok\n");
  return 0;
}

int
lostchild(void *var, void *b)
{
  sleep(10);
  park(var);
  cthread_exit();
}

// parent waits for 5 second. The child parks
// The child should not change the variable till
// it is unparked
int
wakeuptest(void)
{
  cthread_t child;
  int var = 0;
  int other = 0;
  cthread_create(&child, lostchild, (void *)&var, (void *)&other);
  unpark(child.pid, (void *)&var);
  cthread_join(&child);
  printf(1, "wakeuptest ok\n");
  return 0;
}

// for testing the implementation of the queue used in the semaphore, checks
// FIFO
int 
queuetest(void)
{
  queue q;
  qinit(&q);
  int i;
  for(i = 0; i < 10; i++)
   enq(&q, i);

  for(i = 0; i < 10; i++){
    if(deq(&q) != i){
      printf(1, "queuetest failed\n");
      break;
    }
  }
  if(i == 10)
    printf(1, "queuetest ok\n");
  return 0;
}

#define LIMIT 1
#define NTIMES 10
#define THREADSN 10
char buf[LIMIT];
int next;
int first;
slock_t vallock;
int data;
slock_t reclock;
int expected;
semaphore_t buflock;
int pcfailed;
int producer(void *a, void *b)
{
  semaphore_t *slots = (semaphore_t *)a;
  semaphore_t *items = (semaphore_t *)b;
  int i;
  int x;
  for(i = 0; i < NTIMES; i++){
    slock_acquire(&vallock);
    x = data;
    data++;
    slock_release(&vallock);
    sem_down(slots);
    sem_down(&buflock);
    buf[next] = x;
    next = (next + 1) % LIMIT;
    sem_up(&buflock);
    sem_up(items);
  }
  exit();
}

int consumer(void *a, void *b)
{
  semaphore_t *slots = (semaphore_t *)a;
  semaphore_t *items = (semaphore_t *)b;
  int i;
  for(i = 0; i < NTIMES; i++){
    sem_down(items);
    sem_down(&buflock);
    first = (first + 1) % LIMIT;
    sem_up(&buflock);
    sem_up(slots);
  }
  exit();
}

// the item created by the producer should be the one recieved by the consumer
// otherwise, simply sees that there are no deadlocks happening due to lost
// wakeups
int
producerconsumertest(void){
  first = 0;
  next = 0;
  data = 0;
  slock_init(&vallock);
  semaphore_t slots;
  semaphore_t items;
  cthread_t prod[THREADSN], cons[THREADSN];
  int i;
  sem_init(&slots, LIMIT);
  sem_init(&items, 0);
  sem_init(&buflock, 1);
  for(i = 0; i < THREADSN; i++)
    cthread_create(&cons[i], consumer, (void *)&slots, (void *)&items);
  for(i = 0; i < THREADSN; i++)
    cthread_create(&prod[i], producer, (void *)&slots, (void *)&items);
  for(i = 0; i < THREADSN; i++){
    cthread_join(&prod[i]);
    cthread_join(&cons[i]);
  }
  if(!pcfailed)
    printf(1, "producerconsumertest ok\n");
  return 0;
}

#define PHILOSOPHERS 5
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
semaphore_t forks[PHILOSOPHERS];
int ate[PHILOSOPHERS];
slock_t printlock;

int 
philosopher(void *a, void *b)
{
 int no;
 int i;
 no = (*((int *)a));
 int fork1 = MIN(no, (no + 1) % PHILOSOPHERS);
 int fork2 = MAX(no, (no + 1) % PHILOSOPHERS);
 for(i = 0; i < 10; i++){
   /*slock_acquire(&printlock);
   printf(1, "%d thinking\n", no);
   slock_release(&printlock);
   */
   sem_down(&forks[fork1]);
   sleep(1);
   sem_down(&forks[fork2]);
   /*slock_acquire(&printlock);
   printf(1, "%d eating\n", no);
   slock_release(&printlock);
   */
   ate[no] = 1;
   sem_up(&forks[fork2]);
   sleep(1);
   sem_up(&forks[fork1]);
 }
 exit();
}

// no deadlocks in dining philosophers and everyone is able to eat
// (as the while loop is not infinite, there will not be starvation anyway)
int
diningphilosophers(void)
{
  int i;
  cthread_t philosophers[PHILOSOPHERS];
  int numbers[PHILOSOPHERS];
  for(i = 0; i < PHILOSOPHERS; i++)
    sem_init(&forks[i], 1);

  for(i = 0; i < PHILOSOPHERS; i++){
    numbers[i] = i;
    cthread_create(&philosophers[i], philosopher, (void *)&numbers[i], 0);
  }

  for(i = 0; i < PHILOSOPHERS; i++)
    cthread_join(&philosophers[i]);

  for(i = 0; i < PHILOSOPHERS; i++)
    if(!ate[i])
      break;

  if(i != PHILOSOPHERS)
    printf(1, "diningphilosopherstest failed\n");
  else
    printf(1, "diningphilosopherstest ok\n");

  return 0;
}


  
int 
main(void)
{
  jointest1();
  jointest1();
  waitjointest();
  childwaittest();
  //exectest();
  memtest();
  cottontest1();
  //twoexectest();
  toomanythreadstest();  
  tickettest();
  racetest();
  childkilltest();
  vmemtest();
  vmsynctest();
  cwdsynctest();
  pipevmsynctest();
  //tlbtest(); --failes, no TLB shootdown
  parkunparktest();
  wakeuptest();
  queuetest();
  producerconsumertest();
  diningphilosophers();
  exit();
}
