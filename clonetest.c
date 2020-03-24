// Test that fork fails gracefully.
// Tiny executable so that the limit can be filling the proc table.

#include "types.h"
#include "stat.h"
#include "user.h"

#define N  1000

int
dummyprint(void *x, void *y)
{
  int a = *((int *)x) + *((int *)y);
  printf(1, "I calculated %d\n", a);
  exit();
}


char buffer[4096];

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
  //clonetest();
  int x;
  x = fork();
  if (x) 
    printf(1, "The PARENT\n");
  else
    printf(1, "The CHILD\n");
  exit();
}
