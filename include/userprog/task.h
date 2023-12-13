#ifndef USERPROG_TASK_H
#define USERPROG_TASK_H
#include <stdbool.h>
#include <list.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"

typedef int pid_t;
typedef int fd_t;

#define MAX_FD 64
#define PID_ERROR ((pid_t)-1)
#define FD_ERROR ((fd_t)-1)

struct fd {
    fd_t fd;
    fd_t fd_map;
    bool closed;
    bool duplicated;
    struct file *file;
    size_t dup_count;
    fd_t stdio;
};

enum process_status {
    PROCESS_MIN,
    PROCESS_INIT,
    PROCESS_READY,
    PROCESS_WAIT,
    PROCESS_EXITED,
    PROCESS_DYING,
    PROCESS_FAIL,
    PROCESS_MAX
};

struct task {
    char *name;                 /* Name of the process. */
    pid_t pid;                  /* Process ID. */
    struct thread *thread;      /* The thread currently running the task. */
    pid_t parent_pid;           /* PID of parent process. */
    struct list_elem elem;      /* List element for PCB */
    struct list_elem celem;     /* List element for child process. */
    struct fd fds[MAX_FD];      /* File descriptor table. */
    struct semaphore fork_lock; /* Lock for fork system call. */
    struct semaphore wait_lock; /* Lock for wait system call. */
    struct list children;       /* List of child processes. */
    struct intr_frame *if_;     /* Temporary interrupt frame. */
    struct file* executable;    /* Executable file. */
    enum process_status status; /* Status of the process. */
    int exit_code;              /* Exit code. */
    void *args;                 /* Temporary argument for deterministic
                                   creation of processes. */
};

void task_init (void);
struct task *task_create (const char *file_name, struct thread* thread);
bool task_set_thread (struct task *task, struct thread *thrd);
bool task_set_status (struct task *task, enum process_status status);
void task_inherit_initd (struct task *t);
void task_free (struct task *t);
void task_cleanup (struct task *t);
size_t task_child_len (struct task *t);
void task_duplicate_fd (struct task *parent, struct task *child);
void task_exit (int status);
struct task *task_find_by_pid (pid_t pid);
struct task *task_find_by_tid (tid_t tid); 
fd_t task_find_original_fd (struct task* task, int fd);
fd_t task_find_fd_map (struct task *task, int fd);
bool task_inherit_fd (struct task *task, int fd);
#endif