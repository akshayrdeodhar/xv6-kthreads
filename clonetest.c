// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "user.h"

#define N  1000

char buffer[4096];
char buffer2[4096];

int
dummyprint2(void *x, void *y)
{
  int a = *((int *)x) + *((int *)y);
  printf(1, "I am 2, I calculated %d\n", a);
  exit();
}

int
dummyprint(void *x, void *y)
{
  int a = *((int *)x) + *((int *)y);
  printf(1, "I calculated %d\n", a);
  clone(dummyprint2, x, y, (void *)(buffer2 + 4096), 0);
  exit();
}




void
clonetest(void)
{
  int x;
  int a, b;
  a = 4;
  b = 2;
  x = clone(dummyprint, (void *)&a, (void *)&b, (void *)(buffer + 4096), 0);
  printf(1, "PID: %d\n", x);
}


int
main(void)
{
  clonetest();
  exit();
}
