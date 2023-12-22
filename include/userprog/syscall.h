#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);
int syscall_wait (tid_t tid);
void syscall_close (int fd);
struct file *find_file_by_fd (int fd);

#endif /* userprog/syscall.h */
