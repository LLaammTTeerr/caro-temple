/* ai_worker.c - background AI search (pthreads on POSIX, Win32 threads on Windows). */
#include <string.h>
#include "ai_worker.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE thread_t;
#else
#include <pthread.h>
typedef pthread_t thread_t;
#endif

typedef struct {
    Board board;
    Cell me;
    Difficulty diff;
    unsigned rng;
    Pos result;
    volatile int done;     /* set by the worker when result is valid */
} Job;

static Job s_job;
static thread_t s_thread;
static bool s_running;     /* a thread handle exists and has not been joined */
static bool s_wanted;      /* the current job's result should be delivered */

#ifdef _WIN32
static unsigned __stdcall worker_main(void *arg)
#else
static void *worker_main(void *arg)
#endif
{
    Job *j = (Job *)arg;
    unsigned rng = j->rng ? j->rng : 1u;
    j->result = ai_choose_move(&j->board, j->me, j->diff, &rng);
#ifdef _WIN32
    InterlockedExchange((volatile LONG *)&j->done, 1);
    return 0;
#else
    __sync_synchronize();
    j->done = 1;
    return NULL;
#endif
}

static void join_thread(void)
{
    if (!s_running) return;
#ifdef _WIN32
    WaitForSingleObject(s_thread, INFINITE);
    CloseHandle(s_thread);
#else
    pthread_join(s_thread, NULL);
#endif
    s_running = false;
}

void ai_worker_start(const Board *b, Cell me, Difficulty d, unsigned rng_seed)
{
    join_thread(); /* at most one search; an old one finishes within its time budget */
    memset(&s_job, 0, sizeof s_job);
    s_job.board = *b;
    s_job.me = me;
    s_job.diff = d;
    s_job.rng = rng_seed;
    s_wanted = true;
#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, worker_main, &s_job, 0, NULL);
    s_running = h != 0;
    if (s_running) s_thread = (HANDLE)h;
#else
    s_running = pthread_create(&s_thread, NULL, worker_main, &s_job) == 0;
#endif
    if (!s_running) { /* no threads available: compute synchronously */
        worker_main(&s_job);
    }
}

bool ai_worker_poll(Pos *out)
{
    if (!s_wanted || !s_job.done) return false;
#ifndef _WIN32
    __sync_synchronize();
#endif
    join_thread();
    s_wanted = false;
    if (out) *out = s_job.result;
    return true;
}

void ai_worker_cancel(void) { s_wanted = false; }

void ai_worker_shutdown(void)
{
    s_wanted = false;
    join_thread();
}
