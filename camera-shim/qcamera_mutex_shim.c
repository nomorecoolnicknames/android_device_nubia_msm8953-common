/*
 * ISOLATION: keep legacy camera-daemon pthread objects from entering bionic's
 * destroyed/invalid state. NX549J camera blobs later lock or join these objects
 * during snapshot teardown/startup and Android 11 bionic aborts instead of
 * tolerating it.
 */

#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>

#define LOG_TAG "NX549J_QCAM_SHIM"
#define NX549J_TRACKED_THREADS 512

static pthread_mutex_t g_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_threads[NX549J_TRACKED_THREADS];
static int g_threads_used[NX549J_TRACKED_THREADS];

static int (*real_pthread_create)(pthread_t *, const pthread_attr_t *,
                                  void *(*)(void *), void *);
static int (*real_pthread_detach)(pthread_t);
static int (*real_pthread_join)(pthread_t, void **);

static void nx549j_qcamera_resolve_symbols(void)
{
    if (!real_pthread_create) {
        real_pthread_create = dlsym(RTLD_NEXT, "pthread_create");
    }
    if (!real_pthread_detach) {
        real_pthread_detach = dlsym(RTLD_NEXT, "pthread_detach");
    }
    if (!real_pthread_join) {
        real_pthread_join = dlsym(RTLD_NEXT, "pthread_join");
    }
}

static int nx549j_qcamera_find_thread_locked(pthread_t thread)
{
    int i;

    for (i = 0; i < NX549J_TRACKED_THREADS; i++) {
        if (g_threads_used[i] && g_threads[i] == thread) {
            return i;
        }
    }

    return -1;
}

static void nx549j_qcamera_track_thread(pthread_t thread)
{
    int i;

    pthread_mutex_lock(&g_threads_lock);
    if (nx549j_qcamera_find_thread_locked(thread) >= 0) {
        pthread_mutex_unlock(&g_threads_lock);
        return;
    }
    for (i = 0; i < NX549J_TRACKED_THREADS; i++) {
        if (!g_threads_used[i]) {
            g_threads[i] = thread;
            g_threads_used[i] = 1;
            pthread_mutex_unlock(&g_threads_lock);
            return;
        }
    }
    pthread_mutex_unlock(&g_threads_lock);

    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "thread registry full, cannot track pthread_t=%p",
                        (void *)thread);
}

static int nx549j_qcamera_untrack_thread_if_known(pthread_t thread)
{
    int idx;

    pthread_mutex_lock(&g_threads_lock);
    idx = nx549j_qcamera_find_thread_locked(thread);
    if (idx >= 0) {
        g_threads_used[idx] = 0;
    }
    pthread_mutex_unlock(&g_threads_lock);

    return idx >= 0;
}

__attribute__((constructor)) static void nx549j_qcamera_mutex_shim_init(void)
{
    nx549j_qcamera_resolve_symbols();
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "loaded: pthread destroy/join guards enabled");
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    int rc;

    nx549j_qcamera_resolve_symbols();
    if (!real_pthread_create) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "pthread_create real symbol missing");
        return -1;
    }

    rc = real_pthread_create(thread, attr, start_routine, arg);
    if (rc == 0 && thread) {
        nx549j_qcamera_track_thread(*thread);
    }

    return rc;
}

int pthread_detach(pthread_t thread)
{
    int known;

    nx549j_qcamera_resolve_symbols();
    known = nx549j_qcamera_untrack_thread_if_known(thread);
    if (!known) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "skip invalid pthread_detach pthread_t=%p",
                            (void *)thread);
        return 0;
    }
    if (!real_pthread_detach) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "pthread_detach real symbol missing");
        return 0;
    }

    return real_pthread_detach(thread);
}

int pthread_join(pthread_t thread, void **retval)
{
    int rc;
    int known;

    nx549j_qcamera_resolve_symbols();
    known = nx549j_qcamera_untrack_thread_if_known(thread);
    if (!known) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "skip invalid pthread_join pthread_t=%p",
                            (void *)thread);
        if (retval) {
            *retval = NULL;
        }
        return 0;
    }
    if (!real_pthread_join) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "pthread_join real symbol missing");
        if (retval) {
            *retval = NULL;
        }
        return 0;
    }

    rc = real_pthread_join(thread, retval);
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "pthread_join real rc=%d pthread_t=%p",
                        rc, (void *)thread);
    return rc;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    (void)mutex;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
    (void)cond;
    return 0;
}
