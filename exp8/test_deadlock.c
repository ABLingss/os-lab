/*
 * 实验8：死锁检测与解除 — 正式测试程序
 * 使用 raw syscall 绕过 glibc semctl 对自定义 cmd 的限制
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#define DEADCHECK 0xDEAD
#define DEADBREAK 0xBEAC

int main() {
    int semid = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666);
    semctl(semid, 0, SETVAL, 1);
    semctl(semid, 1, SETVAL, 1);
    printf("semid=%d\n", semid);

    /* P1: hold sem[0], wait sem[1] */
    pid_t p1 = fork();
    if (p1 == 0) {
        struct sembuf sb;
        sb.sem_num = 0; sb.sem_op = -1; sb.sem_flg = 0;
        semop(semid, &sb, 1);
        printf("[P1] got sem[0], sleeping...\n");
        sleep(1);
        sb.sem_num = 1;
        printf("[P1] waiting sem[1]...\n");
        semop(semid, &sb, 1);
        printf("[P1] got sem[1]!\n");
        _exit(0);
    }

    /* P2: hold sem[1], wait sem[0] */
    pid_t p2 = fork();
    if (p2 == 0) {
        struct sembuf sb;
        sb.sem_num = 1; sb.sem_op = -1; sb.sem_flg = 0;
        semop(semid, &sb, 1);
        printf("[P2] got sem[1], sleeping...\n");
        sleep(1);
        sb.sem_num = 0;
        printf("[P2] waiting sem[0]...\n");
        semop(semid, &sb, 1);
        printf("[P2] got sem[0]!\n");
        _exit(0);
    }

    /* Wait for deadlock to form */
    sleep(3);

    /* DEADCHECK */
    printf("=== DEADCHECK ===\n");
    long ret = syscall(__NR_semctl, semid, 0, DEADCHECK, (void*)0);
    printf("DEADCHECK -> %ld (1=deadlock 0=no -1=err)\n", ret);

    /* DEADBREAK */
    if (ret > 0) {
        printf("=== DEADLOCK! Calling DEADBREAK ===\n");
        sleep(1);
        ret = syscall(__NR_semctl, semid, 0, DEADBREAK, (void*)0);
        printf("DEADBREAK -> %ld (0=broken %s)\n", ret, ret < 0 ? strerror(errno) : "");
        sleep(1);
    }

    /* Reap children */
    waitpid(p1, NULL, WNOHANG);
    waitpid(p2, NULL, WNOHANG);
    semctl(semid, 0, IPC_RMID);
    printf("Done.\n");
    return 0;
}
