#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *p;

  p = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((p = (struct pipe*)kalloc()) == 0)
    goto bad;
  p->readopen = 1;
  p->writeopen = 1;
  p->nwrite = 0;
  p->nread = 0;
  initlock(&p->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = p;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = p;
  return 0;

//PAGEBREAK: 20
 bad:
  if(p)
    kfree((char*)p);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *p, int writable)
{
  acquire(&p->lock);
  if(writable){
    p->writeopen = 0;
    wakeup(&p->nread);
  } else {
    p->readopen = 0;
    wakeup(&p->nwrite);
  }
  if(p->readopen == 0 && p->writeopen == 0){
    release(&p->lock);
    kfree((char*)p);
  } else
    release(&p->lock);
}

#define MIN(a, b) ((a) < (b) ? (a) : (b))

//PAGEBREAK: 40
int
pipewrite(struct pipe *p, char *addr, int n)
{
  int i;
  int chunk1, chunk2, bytes;
  acquire(&p->lock);
  for(i = 0; i < n;){
    while(p->nwrite == p->nread + PIPESIZE){  //DOC: pipewrite-full
      if(p->readopen == 0 || myproc()->killed){
        release(&p->lock);
        return -1;
      }
      wakeup(&p->nread);
      sleep(&p->nwrite, &p->lock);  //DOC: pipewrite-sleep
    }
    bytes = MIN(p->nread + PIPESIZE - p->nwrite, n - i);
    if(bytes > (PIPESIZE - (p->nwrite % PIPESIZE))){
      chunk1 = PIPESIZE - (p->nwrite % PIPESIZE);
    }else{
      chunk1 = bytes;
    }
    chunk2 = bytes - chunk1;

    if(chunk1 && (copy_from_user((void *)&p->data[p->nwrite % PIPESIZE], (void *)&addr[i], chunk1) == 0)){
      release(&p->lock);
      return -1;
    }
    if(chunk2 && (copy_from_user((void *)p->data, (void *)&addr[i + chunk1], chunk2) == 0)){
      release(&p->lock);
      return -1;
    }

    p->nwrite += bytes;
    i += bytes;
    //p->data[p->nwrite++ % PIPESIZE] = addr[i];
  }
  wakeup(&p->nread);  //DOC: pipewrite-wakeup1
  release(&p->lock);
  return n;
}

int
piperead(struct pipe *p, char *addr, int n)
{
  //int i;
  int bytes, chunk1, chunk2;

  acquire(&p->lock);
  while(p->nread == p->nwrite && p->writeopen){  //DOC: pipe-empty
    if(myproc()->killed){
      release(&p->lock);
      return -1;
    }
    sleep(&p->nread, &p->lock); //DOC: piperead-sleep
  }
  bytes = MIN(p->nwrite - p->nread, n);
  if(bytes > (PIPESIZE - (p->nread % PIPESIZE))){
    chunk1 = PIPESIZE - (p->nread % PIPESIZE);
  }else{
    chunk1 = bytes;
  }
  chunk2 = bytes - chunk1;
  if(chunk1 && (copy_to_user((void *)addr, (void *)&p->data[p->nread % PIPESIZE], chunk1) == 0)){
    release(&p->lock);
    return -1;
  }
  if(chunk2 && (copy_to_user((void *)(addr + chunk1), (void *)&p->data, chunk2) == 0)){
    release(&p->lock);
    return -1;
  }
  p->nread += bytes;
  wakeup(&p->nwrite);  //DOC: piperead-wakeup
  release(&p->lock);
  return bytes;
}
