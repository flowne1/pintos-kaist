#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "file.h"
#include "threads/thread.h"
typedef int pid_t;
typedef int fd_t;

#define MAX_FD 256
#define PID_ERROR ((pid_t)-1)
#define FD_ERROR ((fd_t)-1)

struct fd {
    fd_t fd;
    bool closed;
    struct file *file;
};

struct task {
    char *name;
    pid_t pid;
    struct thread *thread;
    struct list_elem elem;
    struct fd fds[MAX_FD];
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
