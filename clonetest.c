#include "types.h"
#include "stat.h"
#include "user.h"

int memtestchild(void *x, void *y) {
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

int memtest1(void) {
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

  retval = clone(memtestchild, (void *)a, (void *)sharedmem, buf, 0);
  if (retval < 0) {
    printf(1, "Clone failed, retval = %d\n", retval);
    return 1;
  }

  sharedmem[0] = 1;

  while(sharedmem[1] == (int)0)
    sleep(1);

  sbrk(-10);
  
  sharedmem[2] = 1;

  return 0;
}

int main(void) {
  memtest1();
  exit();
}
