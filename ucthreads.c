#include "types.h"
#include "param.h"
#include "user.h"

// Cotton Threads

#define PGSIZE 4096

int 
cthread_create(cthread_t *thread, int (*fn)(void *, void *), 
	       void *arg1, void *arg2)
{
  thread->stack = (char *)malloc(PGSIZE);
  if(thread->stack == 0){
    return -1;
  }

  // little magic cheating here?
  // breaking the kernel abstraction?
  // top - 04: arg2
  // top - 08: arg1
  // top - 12: return address..

  //*(thread_stack - 12) = exit;

  thread->pid = clone(fn, arg1, arg2, thread->stack + PGSIZE, 0);
  if(thread->pid == -1){
    return -1;
  }

  return thread->pid;
}

int 
cthread_cut(cthread_t *thread)
{
  return kill(thread->pid);
}

int cthread_join(cthread_t *thread)
{
  if(join(thread->pid) != thread->pid){
    free(thread->stack);
    return -1;
  }
  free(thread->stack);
  return thread->pid;
}
  
void cthread_exit()
{
  exit();
}  

static inline uint
xaddl(volatile uint *addr, uint value)
{
  // stolen from: https://en.wikipedia.org/wiki/Fetch-and-add

  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xaddl %0, %1" :
               "+r" (value), "+m" (*addr) :
                : // no input-only
               "memory");
  return value;
}

static inline uint
xchg(volatile uint *addr, uint newval)
{
  uint result;

  // The + in "+m" denotes a read-modify-write operand.
  asm volatile("lock; xchgl %0, %1" :
               "+m" (*addr), "=a" (result) :
               "1" (newval) :
               "cc");
  return result;
}

void
tlock_init(tlock_t *lk)
{
  lk->turn = 0;
  lk->ticket = 0;
}

void 
tlock_acquire(tlock_t *lk)
{
  uint ticket;
  ticket = xaddl(&lk->ticket, 1);

  __sync_synchronize();

  while(lk->turn != ticket)
    ;

}

void
tlock_release(tlock_t *lk)
{
  __sync_synchronize();
  lk->turn += 1;
}

void
slock_init(slock_t *lk)
{
  *lk = 0;
}

void 
slock_acquire(slock_t *lk)
{
  while(xchg(lk, 1) != 0)
    ;

  __sync_synchronize();
}

void
slock_release(slock_t *lk)
{
  __sync_synchronize();
  *lk = 0;
}

void qinit(queue *q)
{
  q->start = 0;
  q->end = 0;
  q->count = 0;
}

void enq(queue *q, int x)
{
  q->arr[q->end] = x;
  q->end = (q->end + 1) % QMAX;
  q->count++;
}

int deq(queue *q)
{
  int x;
  x = q->arr[q->start];
  q->start = (q->start + 1) % QMAX;
  q->count--;
  return x;
}

int qisfull(queue *q)
{
  return (q->count == QMAX);
}

int qisempty(queue *q)
{
  return !q->count;
}


/*(void qinit(queue *q)
{
  q->len = 0;
  q->head = 0;
  q->tail = 0;
}

void enq(queue *q, int x)
{
  qnode *n = malloc(sizeof(qnode));
  n->val = x;
  n->next = 0;
  if(q->tail)
    q->tail->next = n;
  else
    q->head = n;
  q->tail = n;
  q->len++;
}

int qisempty(queue *q)
{
  return (q->len == 0);
}

int qisfull(queue *q)
{
  return 0;
}

int deq(queue *q)
{
  qnode *n;
  int x;
  n = q->head;
  x = n->val;
  q->head = n->next;
  free(n);
  if(!q->head)
    q->tail = 0;
  q->len--;
  return x;
}
*/

slock_t printlock;

void sem_init(semaphore_t *s, int n)
{
  slock_init(&s->guard);
  s->count = n;
  qinit(&s->waitq);
}

void sem_up(semaphore_t *s)
{
  int pid;
  slock_acquire(&(s->guard));
  s->count++;
  if(s->count <= 0){
    pid = deq(&(s->waitq));
    unpark(pid, &(s->count));
  }
  slock_release(&(s->guard));
}

void sem_down(semaphore_t *s)
{
  int pid;
  slock_acquire(&(s->guard));
  s->count--;
  if(s->count < 0){
    pid = getpid();
    enq(&(s->waitq), pid);
    slock_release(&(s->guard));
    park(&(s->count));
  }
  else{
   slock_release(&(s->guard));
  }
}
