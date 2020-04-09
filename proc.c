#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "proc.h"
#include "traps.h"

struct spinlock tlblock;
int initiator;

struct table ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

void
tlbinit(void)
{
  initlock(&tlblock, "tlb");
  initiator = -1;
}

// INCOMPLETE:
// Send an interrupt to all other CPUs
// tell them to reload their page tables
// to be called with interrupts disabled
static void
tlbinitiate(void)
{
  if(initiator != -1)
    panic("initiator");
  acquire(&tlblock);
  lapicexclbcast(T_TLBFLUSH);
}

static void
tlbconclude(void)
{
  initiator = -1;
  release(&tlblock);
}

// handle tlb flush interrupt from other processor
// this is an interrupt handler, no interrupts will hit
void
tlbhandler(void)
{
  struct proc *p;
  acquire(&tlblock);
  p = myproc();
  if(p)
    lcr3(V2P(p->pgdir));
  release(&tlblock);
}
  

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  p->tgid = p->pid;
  p->process = p;
  initlock(&p->vlock, "vlock");

  p->threadcount = 1;
  
  initproc = p;

  acquire(&p->vlock);
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  release(&p->vlock);
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  initlock(&p->cwdlock, "cwdlock");
  acquire(&p->cwdlock);
  p->cwd = namei("/");
  release(&p->cwdlock);


  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  //TOCTOU: delay the sbrk till check happens
  //int i;
  //for(i = 0; i < 100; i++)
  //  yield();

  acquire(&curproc->process->vlock);
  // interrupts got disabled here
  tlbinitiate();

  sz = curproc->process->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&curproc->process->vlock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&curproc->process->vlock);
      return -1;
    }
  }
  curproc->process->sz = sz;
  switchuvm(curproc);

  tlbconclude();
  release(&curproc->process->vlock);

  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct spinlock *vlock;
  struct inode *cwd;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Thread group
  np->tgid = np->pid;
  np->process = np;

  initlock(&np->vlock, "vlock");
  vlock = &(curproc->process->vlock);
  acquire(vlock);
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->process->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    release(vlock);
    return -1;
  }
  np->sz = curproc->process->sz;
  release(vlock);

  np->parent = curproc->process;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  
  acquire(&curproc->process->cwdlock);
  cwd = curproc->process->cwd;
  release(&curproc->process->cwdlock);

  initlock(&np->cwdlock, "cwdlock");
  acquire(&np->cwdlock);
  np->cwd = idup(cwd);
  release(&np->cwdlock);


  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  np->threadcount = 1;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  int threadcount;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  acquire(&ptable.lock);
  threadcount = curproc->process->threadcount;
  release(&ptable.lock);
  if(threadcount == 1){
    // if threadcount is 1, 
    // the 1 thread is in this syscall
    // and we may proceed without holding locks
    begin_op();
    iput(curproc->process->cwd);
    end_op();
    curproc->cwd = 0;
  }

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // some thread from group might be sleeping in wait() 
  wakeup1(curproc->process);

  curproc->process->threadcount -= 1;

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process (with all its threads)
// to exit and return its pid.
// *a* child process -> all threads of one child
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, tgid, found;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for a child process which has exited
    havekids = 0;
    found = 0;
    tgid = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc->process)
        continue;
      havekids = 1;
      if (p->process->threadcount == 0){
        // the one to be harvested
        tgid = p->tgid;
        found = 1;
        break;
      }
    }
    
    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }
     
    if(!found){
      // Wait for children to exit.  (See wakeup1 call in proc_exit.)
      sleep(curproc->process, &ptable.lock);  //DOC: wait-sleep
    }
    else{
      break;
    }
  }

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->tgid != tgid)
      continue;
    // assert: this *p* is to be reaped
    if(p->state == ZOMBIE){
      if(p->pid == p->tgid && p->process == p){
        freevm(p->pgdir);

	// process is dead, so access without locks is OK
      }
      kfree(p->kstack);
      p->kstack = 0;
      p->context = 0;
      p->pid = 0;
      p->tgid = 0;
      p->parent = 0;
      p->name[0] = 0;
      p->killed = 0;
      p->state = UNUSED;
      p->process = 0;
      p->pgdir = 0;
      p->threadcount = 0;
    } else{
      panic("undead");
    }
  }
  release(&ptable.lock);
  return tgid;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the thread with the given pid.
// thread won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Create a new thread copying p as the parent.
// Sets up stack to return as if from system call.
int
clone(int (*fn)(void *, void*), void *arg1, void *arg2, 
      void *stack, int flags)
{
  int i, pid;
  uint *sp;
  struct proc *np;
  struct proc *curproc = myproc();
   
  // anyway, not touching anything below stack - 12
  // lock: this and the stack defererence for setup
  if((uint)(stack - 4096) >= curproc->process->sz || (uint)(stack) > curproc->process->sz) {
    cprintf("stack: %p, sz: %p\n", stack, curproc->process->sz);
    return -1;
  }
   
  // page-aligned stack: malloc() does not guarentee,
  // if ((PGROUNDDOWN((int)stack) != (int)stack))
  //  return -1;
   
  //stack += 4096; // top of the stack
   
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
   
  // Thread group
  np->tgid = curproc->tgid;
  np->process = curproc->process;
   
  initlock(&np->vlock, "vlock");
  struct spinlock *vlock = &(curproc->process->vlock);
  acquire(vlock);
  np->pgdir = curproc->pgdir;
  np->sz = 0;
  release(vlock);
   
  // one more thread using same address space
  // invariant: when exiting this system call
  // the threadcount will be equal to the number
  // of threads using the pgdir of the thread group
  // leader
  np->threadcount = 0;
   
  np->parent = curproc->parent;
  *np->tf = *curproc->tf;
   
  // lay out the stack for user program
  sp = (uint *)stack;
  sp -= 1;
  *sp = (uint)arg2;
  sp -= 1;
  *sp = (uint)arg1;
  sp -= 1;
  *sp = (uint)0xffffffff; // temperory, should be die()
   
  // In child, begin execution from fn, args are in stack
  np->tf->eip = (uint)fn;
  np->tf->esp = (uint)sp;
   
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = 0;
   
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
   
  pid = np->pid;
   
  acquire(&ptable.lock);
   
  if (curproc->killed) {
    np->state = ZOMBIE;
  } else {
    np->state = RUNNABLE;
    curproc->process->threadcount += 1;
  }
   
  release(&ptable.lock);
   
  return pid;
}

// Make the current thread zombie
// Do not return
// A dead thread remains in ZOMBIE
// state till some thread from the 
// parent process calls wait on it
void
die(void)
{
  struct proc *curproc = myproc();

  if(curproc == initproc)
    panic("init exiting");

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// if thread with pid = *pid* is in the same thread group
// wait of it to exit, and reap it
// if thread is the thread group leader, do not reap it
// return the *pid* If no such thread exists, return -1
int
join(int pid)
{
  struct proc *p;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      if(p->tgid == curproc->tgid && p->process == curproc->process){
        break;
      }
      else{
        release(&ptable.lock);
        return -1;
      }
    }
  }
  
  if(p->pid != pid){
    release(&ptable.lock);
    return -1;
  }

  while(p->state != ZOMBIE){
    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    sleep(curproc->process, &ptable.lock);
  }

  if(p->tgid != p->pid && p != p->process){
    kfree(p->kstack);
    p->kstack = 0;
    p->context = 0;
    p->pid = 0;
    p->tgid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->killed = 0;
    p->state = UNUSED;
    p->process = 0;
    p->pgdir = 0;
    p->threadcount = 0;
  }

  release(&ptable.lock);
  return pid;
}

void *
copy_from_user(void *dst, const void *src, uint n)
{
  void *dest;
  struct spinlock *vlock;
  struct proc *p = myproc();
  vlock = &p->process->vlock;
  acquire(vlock);
  if((uint)src >= p->process->sz || (uint)(src + n) > p->process->sz){
    release(vlock);
    return (void *)0;
  }
  dest = memmove(dst, src, n);
  release(vlock);
  return dest;
}

void *
copy_to_user(void *dst, const void *src, uint n)
{
  void *dest;
  struct spinlock *vlock;
  struct proc *p = myproc();
  vlock = &p->process->vlock;
  acquire(vlock);
  if((uint)dst >= p->process->sz || (uint)(dst + n) > p->process->sz){
    release(vlock);
    return (void *)0;
  }
  dest = memmove(dst, src, n);
  release(vlock);
  return dest;
}

int
copy_str_from_user(char *dst, const char *src, uint limit)
{
  struct spinlock *vlock;
  struct proc *p = myproc();
  vlock = &p->process->vlock;
  acquire(vlock);
  int i;
  for (i = 0; (uint)src < p->process->sz && i < limit && (*dst++ = *src++); i++);
  if((uint)src == p->process->sz){
    release(vlock);
    return -1;
  }
  release(vlock);
  return i;
}

