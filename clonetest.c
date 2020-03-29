#include "types.h"
#include "stat.h"
#include "user.h"

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
  printf(1, "Child %d will return\n", x);
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
main(void)
{
  //memtest1();
  //jointest();
  //jointest1();
  //waitjointest();
  //childwaittest();
  exectest();
  //memtest();
  //cottontest1();
  exit();
}
