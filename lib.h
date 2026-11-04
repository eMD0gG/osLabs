#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE proc_t;
#else
    #include <sys/types.h>
    typedef pid_t proc_t;
#endif


int run_background(char *program, proc_t *proc);

int wait_for(proc_t proc);



