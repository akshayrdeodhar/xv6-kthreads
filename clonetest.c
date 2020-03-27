#include "types.h"
#include "stat.h"
#include "user.h"

int memtestchild(void *x, void *y) {
  char *a, *sharedmem;
  a = (char *)x;
  sharedmem = (char *)y;

  while(!sharedmem[0])
    printf(1, "Child still running\n");

  printf(1, "Dereferencing the pointer passed\n");
  a[0] = 42;
  printf(1, "Dereferenced the pointer passed\n");

  sharedmem[1] = 1;

  exit();
}

int memtest1(void) {
  char *buf;
  char *a = 0;
  char *b1, *b2;
  char sharedmem[10];
  int retval;

  b1 = sbrk(0);
  b2 = sbrk(4096);
  if (b2 < 0) {
    printf(1, "sbrk failed\n");
    return 1;
  }

  buf = b1;

  retval = clone(memtestchild, (void *)a, (void *)sharedmem, buf + 4096, 0);
  if (retval < 0) {
    printf(1, "Clone failed, retval = %d\n", retval);
    return 1;
  }
  printf(1, "Freed the pointer passed\n");
  sharedmem[0] = 1;

  while(!sharedmem[1]);

  return 0;
}

int main(void) {
  memtest1();
  
  exit();
}
