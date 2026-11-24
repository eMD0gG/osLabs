#pragma once

#include <string>
#include <errno.h>

#if defined (_WIN32)

#   include <windows.h>

#   define MAP_NAME_PREFIX "Local\\"
#   define INV_HANDLE (NULL)
#   define CSEM HANDLE
#else
#   include <sys/mman.h>
#   include <sys/stat.h>
#   include <fcntl.h>
#   include <unistd.h>
#   include <semaphore.h>
#   define HANDLE int
#   define INV_HANDLE (-1)
#   define MAP_NAME_PREFIX "/"
#   define CSEM sem_t*
#endif

#define SEM_NAME_POSTFIX "_sem"

namespace cplib {
    template<typename T>
    class SharedMem {
    public:
        SharedMem(const char *name, bool create_if_not_exists = true)
                : _fname(std::string(MAP_NAME_PREFIX) + name),
                  _semname(_fname + SEM_NAME_POSTFIX)
        {
            bool is_new = false;
            bool ret = OpenMem(false, is_new);
            if (!ret && create_if_not_exists) {
                ret = OpenMem(true, is_new);
            }
            if (ret) {
                ret = MapMem();
            }
            if (ret) {
                LockSema();
                if (is_new) {
                    _mem->str = T{};
                    _mem->cnt = 0;
                }
                _mem->cnt++;
                UnlockSema();
            }
            else {
                CloseMem(is_new);
            }
        }
        ~SharedMem() {
            if (IsValid()) {
                LockSema();
                int cnt = --_mem->cnt;
                UnlockSema();
                CloseMem(cnt <= 0);
            }
        }

        bool IsValid() { return _fd != INV_HANDLE && _sem != NULL && _mem != NULL; }

        void Lock() { LockSema(); }

        void Unlock() { UnlockSema(); }

        T *Data() {
            if (!IsValid())
                return NULL;
            return &_mem->str;
        }

    private:
        bool OpenMem(bool create, bool &is_new) {
            const char *mem_name = _fname.c_str();
            const char *sem_name = _semname.c_str();
#if defined (_WIN32)
            if (create) {
                _fd = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                         0, sizeof(shmem_contents), mem_name);
                is_new = (_fd != INV_HANDLE && GetLastError() != ERROR_ALREADY_EXISTS);
                if (_fd != INV_HANDLE) {
                    _sem = CreateSemaphoreA(NULL, 1, 1, sem_name);
                }
            }
            else {
                _fd = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mem_name);
                if (_fd != INV_HANDLE) {
                    _sem = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, sem_name);
                }
            }
#else
            if (create) {
                _fd = shm_open(mem_name, O_CREAT | O_EXCL | O_RDWR, 0644);
                if (_fd != INV_HANDLE) {
                    is_new = true;
                    ftruncate(_fd, sizeof(shmem_contents));
                    sem_unlink(sem_name);
                    _sem = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
                }
            }
            else {
                _fd = shm_open(mem_name, O_RDWR, 0644);
                if (_fd != INV_HANDLE) {
                    _sem = sem_open(sem_name, 0);
                }
            }
            if (_sem == SEM_FAILED) {
                _sem = NULL;
            }
#endif
            return (_fd != INV_HANDLE && _sem != NULL);
        }

        bool MapMem() {
            if (_fd == INV_HANDLE) {
                return false;
            }
#if defined (_WIN32)
            _mem = reinterpret_cast<shmem_contents *>(MapViewOfFile(_fd, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shmem_contents)));
#else
            void* res = mmap(NULL, sizeof(shmem_contents), PROT_WRITE | PROT_READ,
                             MAP_SHARED, _fd, 0);
            _mem = (res == MAP_FAILED) ? NULL : reinterpret_cast<shmem_contents*>(res);

#endif
            return (_mem != NULL);
        }

        void CloseMem(bool destroy) {
            if (_mem != NULL) {
#if defined (_WIN32)
                UnmapViewOfFile(_mem);
#else
                munmap(_mem, sizeof(shmem_contents));
#endif
                _mem = NULL;
            }
            if (_fd != INV_HANDLE) {
#if defined (_WIN32)
                CloseHandle(_fd);
#else
                close(_fd);
#endif
            }
            if (_sem != NULL) {
#if defined (_WIN32)
                CloseHandle(_sem);

#else
                sem_close(_sem);
#endif
                _sem = NULL;
            }
#if !defined (_WIN32)
            if (destroy) {
                shm_unlink(_fname.c_str());
                sem_unlink(_semname.c_str());
            }
#else
            (void) destroy;
#endif
        }

        void LockSema() {
#if defined (_WIN32)
            WaitForSingleObject(_sem, INFINITE);
#else
            while (sem_wait(_sem) == -1 && errno == EINTR);
#endif
        }

        void UnlockSema() {
#if defined (_WIN32)
            ReleaseSemaphore(_sem, 1, NULL);
#else
            sem_post(_sem);
#endif
        }

        struct shmem_contents {
            T str;
            int cnt;
        };

        std::string _fname;
        std::string _semname;
        shmem_contents *_mem = NULL;
        CSEM _sem = NULL;
        HANDLE _fd = INV_HANDLE;

    };
}