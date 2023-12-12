#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "threads/thread.h"
#include "threads/synch.h"

typedef int pid_t;
typedef int fd_t;

#define MAX_FD 64
#define PID_ERROR ((pid_t)-1)
#define FD_ERROR ((fd_t)-1)

struct fd {
    fd_t fd;
    bool closed;
    struct file *file;
};

struct task {
    char *name;                     /* Name of the process. */
    pid_t pid;                      /* Process ID. */
    struct thread *thread;          /* The thread currently running the task. */
    struct task *parent;            /* Parent Process */
    struct list_elem elem;          /* List element for PCB */
    struct fd fds[MAX_FD];          /* File descriptor table. */
    struct intr_frame *if_;         /* Temporary interrupt frame. */
    int exit_code;                  /* Exit Codes. */
    void *args;                     /* Temporary argument for deterministic creation of processes. */
    struct list child_list;         // List of child
    struct list_elem c_elem;        // List elem for child_list
    struct semaphore sema_fork;     // Semaphore for fork
};

void process_init (void);
pid_t process_create_initd (const char *file_name);
pid_t process_fork (const char *name, struct intr_frame *if_);
struct task *process_find_by_pid (pid_t pid);
struct task *process_find_by_tid (tid_t tid); 
int process_exec (void *f_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
