#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include "shared_mem.hpp"

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
  #define get_pid() ((long)GetCurrentProcessId())
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/wait.h>
  #include <signal.h>
  #include <cerrno>
  #define get_pid() ((long)getpid())
#endif

struct SharedState {
    long long counter;
    int master_pid;
    int copy1_pid;
    int copy2_pid;
};

static const char* SHM_NAME = "myapp_state";

static std::string now_str() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto ms = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms.count());
    return buf;
}

static bool is_process_alive(int pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    DWORD res = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return res == WAIT_TIMEOUT;
#else
    if (kill((pid_t)pid, 0) == 0) {
        int status;
        pid_t w = waitpid((pid_t)pid, &status, WNOHANG);
        if (w == (pid_t)pid) return false;
        return true;
    }
    return errno == EPERM;
#endif
}

static int spawn_copy(const char* exe_path, const char* arg) {
#ifdef _WIN32
    std::string cmd = std::string("\"") + exe_path + "\" " + arg;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return 0;
    }
    int pid = (int)pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pid;
#else
    pid_t pid = fork();
    if (pid < 0) return 0;
    if (pid == 0) {
        execl(exe_path, exe_path, arg, (char*)nullptr);
        _exit(127);
    }
    return (int)pid;
#endif
}

static std::atomic<bool> running{true};
static void on_sigint(int) { running = false; }

int main(int argc, char** argv) {
    std::signal(SIGINT, on_sigint);

    std::ofstream log("app.log", std::ios::app);

    cplib::SharedMem<SharedState> shm(SHM_NAME);
    if (!shm.IsValid()) {
        log << now_str() << " ERROR: cannot open shared memory\n";
        log.flush();
        return 1;
    }
    SharedState* st = shm.Data();


    if (argc > 1) {
        if (std::strcmp(argv[1], "--copy1") == 0) {
            log << now_str() << " COPY1 START pid=" << get_pid() << "\n";
            log.flush();

            shm.Lock();
            st->counter += 10;
            shm.Unlock();

            log << now_str() << " COPY1 EXIT pid=" << get_pid() << "\n";
            log.flush();

            shm.Lock();
            if (st->copy1_pid == (int)get_pid()) st->copy1_pid = 0;
            shm.Unlock();
            return 0;
        }
        if (std::strcmp(argv[1], "--copy2") == 0) {
            log << now_str() << " COPY2 START pid=" << get_pid() << "\n";
            log.flush();

            shm.Lock();
            st->counter *= 2;
            shm.Unlock();

            std::this_thread::sleep_for(std::chrono::seconds(2));

            shm.Lock();
            st->counter /= 2;
            shm.Unlock();

            log << now_str() << " COPY2 EXIT pid=" << get_pid() << "\n";
            log.flush();

            shm.Lock();
            if (st->copy2_pid == (int)get_pid()) st->copy2_pid = 0;
            shm.Unlock();
            return 0;
        }
    }

    log << now_str() << " START pid=" << get_pid() << "\n";
    log.flush();

    std::atomic<bool> is_master{false};
    {
        shm.Lock();
        int pid = (int)get_pid();
        if (st->master_pid == 0 || !is_process_alive(st->master_pid)) {
            st->master_pid = pid;
            is_master = true;
        }
        int master_pid = st->master_pid;
        shm.Unlock();

        log << now_str() << " pid=" << get_pid()
            << " master=" << (is_master ? "yes" : "no")
            << " (master_pid=" << master_pid << ")\n";
        log.flush();
    }

    std::thread ticker([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            if (!running) break;

            shm.Lock();
            st->counter++;
            shm.Unlock();
        }
    });

    std::thread logger([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!running || !is_master) continue;

            shm.Lock();
            long long c = st->counter;
            shm.Unlock();

            log << now_str() << " pid=" << get_pid()
                << " counter=" << c << "\n";
            log.flush();
        }
    });

    std::string exe_path = argv[0];
    std::thread spawner([&] {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (!running || !is_master) continue;

            shm.Lock();
            int c1_pid = st->copy1_pid;
            bool c1_alive = is_process_alive(c1_pid);
            if (!c1_alive) st->copy1_pid = 0;
            shm.Unlock();

            if (c1_alive) {
                log << now_str() << " pid=" << get_pid()
                    << " WARNING: copy1 is still running (pid=" << c1_pid << "), skipping\n";
                log.flush();
            } else {
                int new_pid = spawn_copy(exe_path.c_str(), "--copy1");
                if (new_pid > 0) {
                    shm.Lock();
                    st->copy1_pid = new_pid;
                    shm.Unlock();
                }
            }

            shm.Lock();
            int c2_pid = st->copy2_pid;
            bool c2_alive = is_process_alive(c2_pid);
            if (!c2_alive) st->copy2_pid = 0;
            shm.Unlock();

            if (c2_alive) {
                log << now_str() << " pid=" << get_pid()
                    << " WARNING: copy2 is still running (pid=" << c2_pid << "), skipping\n";
                log.flush();
            } else {
                int new_pid = spawn_copy(exe_path.c_str(), "--copy2");
                if (new_pid > 0) {
                    shm.Lock();
                    st->copy2_pid = new_pid;
                    shm.Unlock();
                }
            }
        }
    });

    std::thread input([&] {
        long long v;
        while (running && (std::cin >> v)) {
            shm.Lock();
            st->counter = v;
            shm.Unlock();
        }
    });

    std::thread master_watcher([&] {
        while (running) {
            if (!is_master) {
                bool became_master = false;
                shm.Lock();
                if (st->master_pid == 0 || !is_process_alive(st->master_pid)) {
                    st->master_pid = (int)get_pid();
                    became_master = true;
                }
                shm.Unlock();

                if (became_master) {
                    is_master = true;
                    log << now_str() << " pid=" << get_pid() << " PROMOTED TO MASTER\n";
                    log.flush();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    input.detach();
    ticker.join();
    logger.join();
    spawner.join();
    master_watcher.join();

    if (is_master) {
        shm.Lock();
        if (st->master_pid == (int)get_pid()) {
            st->master_pid = 0;
        }
        shm.Unlock();
    }

    log << now_str() << " STOP pid=" << get_pid()
        << " master=" << (is_master ? "yes" : "no") << "\n";
    log.flush();

    return 0;
}