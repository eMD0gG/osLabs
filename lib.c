#include "lib.h"
#include <stdio.h>

#ifdef _WIN32

int run_background(char *program, proc_t *proc) {
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, program, NULL, NULL, FALSE,
                        0, NULL, NULL, &si, &pi))
        return -1;

    CloseHandle(pi.hThread);
    *proc = pi.hProcess;
    return 0;
}

int wait_for(proc_t proc) {
    DWORD code = 0;
    WaitForSingleObject(proc, INFINITE);
    GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return (int)code;
}

#else

#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

int run_background(char *program, proc_t *proc) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", program, (char *)NULL);
        _exit(127);
    }
    *proc = pid;
    return 0;
}

int wait_for(proc_t proc) {
    int status = 0;
    if (waitpid(proc, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#endif