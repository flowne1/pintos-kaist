#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
struct file *find_file_by_fd (int fd);

#endif /* userprog/syscall.h */
