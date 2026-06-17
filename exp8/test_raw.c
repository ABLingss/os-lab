/* raw syscall test */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <string.h>

#define DEADCHECK 0xDEAD
#define DEADBREAK 0xBEAC

int main() {
    int semid = semget(0x20250608, 2, IPC_CREAT | 0666);
    if (semid < 0) { perror("semget"); return 1; }
    semctl(semid, 0, SETVAL, 1);
    semctl(semid, 1, SETVAL, 1);

    printf("semid=%d\n", semid);
    printf("Testing DEADCHECK=0x%x via raw syscall...\n", DEADCHECK);

    /* Try raw syscall */
    long ret = syscall(__NR_semctl, semid, 0, DEADCHECK, (void*)0);
    printf("raw syscall ret=%ld errno=%d (%s)\n", ret, errno, strerror(errno));

    /* Try glibc semctl */
    errno = 0;
    ret = semctl(semid, 0, DEADCHECK);
    printf("glibc semctl ret=%ld errno=%d (%s)\n", ret, errno, strerror(errno));

    semctl(semid, 0, IPC_RMID);
    return 0;
}
