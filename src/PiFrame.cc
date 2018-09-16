// FrameMemory.cpp

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <bcm_host.h>
#include <interface/vcos/vcos.h>
#include <mmal/mmal.h>
#include <mmal/util/mmal_util.h>
#include <mmal/util/mmal_default_components.h>
#include <mmal/util/mmal_connection.h>
#include <mmal/util/mmal_util_params.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "RaspiCamControl.h"
#ifdef __cplusplus
}
#endif

#include "PiFrame.h"

/** Constructor */
PiFrame::PiFrame(size_t initial_mem_size, int* status)
        : buffer(NULL), length(0), mAllocatedSize(0) {

    if (status) *status = 0;

    memset(&mMemMutex, 0, sizeof(mMemMutex));
    memset(&mSignalMutex, 0, sizeof(mSignalMutex));
    memset(&mSignalCond, 0, sizeof(mSignalCond));

    // Allocate the buffer for storing JPEG-frame
    buffer = (uint8_t*)malloc(initial_mem_size);
    if (buffer) {
        mAllocatedSize = initial_mem_size;
    } else {
        if (status) *status = ENOMEM;
        return;
    }

    int ret = 0;

    // Initialize the mutex for locking before buffer is accessed.
    ret = pthread_mutex_init(&mMemMutex, NULL);
    if (ret) {
        fprintf(stderr, "PiFrame() mMemMutex err=%d\n", ret);
        if (status) *status = ret;
        return;
    }

    // Initialize the mutex for locking the condition value.
    ret = pthread_mutex_init(&mSignalMutex, NULL);
    if (ret) {
        fprintf(stderr, "PiFrame() mSignalMutex err=%d\n", ret);
        if (status) *status = ret;
        return;
    }

    // Initialize the condition value.
    ret = pthread_cond_init(&mSignalCond, NULL);
    if (ret) {
        fprintf(stderr, "PiFrame() cond init err=%d\n", ret);
        if (status) *status = ret;
        return;
    }
}

/** Destructor */
PiFrame::~PiFrame() {
    int ret;
    ret = pthread_cond_destroy(&mSignalCond);
    if (ret) fprintf(stderr, "~PiFrame() cond destroy err=%d\n", ret);
    ret = pthread_mutex_destroy(&mSignalMutex);
    if (ret) fprintf(stderr, "~PiFrame() sig mutex destroy err=%d\n", ret);
    ret = pthread_mutex_destroy(&mMemMutex);
    if (ret) fprintf(stderr, "~PiFrame() mem mutex destroy err=%d\n", ret);

    free(buffer);
}

/** Wait until called sendReadySignal() */
int PiFrame::waitForReady(int sec, long nsec) {
    int ret = 0;

    // Lock the mutex
    ret = pthread_mutex_lock(&mSignalMutex);
    if (ret) {
        fprintf(stderr, "PiFrame::waitForReady() lock mMemMutex err=%d\n", ret);

        // If error occured, finish immediately.
        return ret;
    }

    // Wait the thread.
    // If arguments have a timeout, wait until timeout. 
    if (sec <= 0 && nsec <= 0) {
        ret = pthread_cond_wait(&mSignalCond, &mSignalMutex);
    } else {
        timespec t;
        t.tv_sec = time(NULL) + sec;
        t.tv_nsec = nsec;
        ret = pthread_cond_timedwait(&mSignalCond, &mSignalMutex, &t);
    }

    // Check whether pthread_cond_timedwait() was success.
    if (ret != 0 && ret != ETIMEDOUT) {
        fprintf(stderr, "PiFrame::waitForReady() cond timed err=%d\n", ret);

        // If error occured, finish without confirmation whether pthread_mutex_unlock() is success
        pthread_mutex_unlock(&mSignalMutex);
        return ret;
    }

    // Check whether pthread_cond_timedwait() was timeout.
    const bool isTimeout = (ret == ETIMEDOUT);

    // Unlock the mutex
    ret = pthread_mutex_unlock(&mSignalMutex);
    if (ret) {
        fprintf(stderr, "PiFrame::waitForReady() unlock mMemMutex err=%d\n", ret);
        return ret;
    }

    return isTimeout? ETIMEDOUT : 0;
}

/** Send signals for wakeup waiting threads */
void PiFrame::sendReadySignal() {
    int ret = 0;
    ret = pthread_cond_broadcast(&mSignalCond);
    if (ret) fprintf(stderr, "PiFrame::sendReadySignal() cond broadcast err=%d\n", ret);
}

/** Before call write() or read(), lock the memory stored jpeg-image */
int PiFrame::lock(int sec, long nsec) {
    int ret = 0;
    if (sec < 0 || nsec < 0) {
        // Error case: Invalid arguments
        return EINVAL;
    } else if (sec > 0 || nsec > 0) {
        // Wait until timeout
        timespec t;
        t.tv_sec = sec; t.tv_nsec = nsec;
        ret = pthread_mutex_timedlock(&mMemMutex,  &t);
        if (ret == ETIMEDOUT) {
            // Semi-normal case: occured timeout
        } else if (ret != 0) {
            // Error case: unknown error
            fprintf(stderr, "PiFrame::lockMemory s:%d n:%d err=%d\n", sec, nsec, ret);
        }
    }  else {
        // wait infinitely
        ret = pthread_mutex_lock(&mMemMutex);
        if (ret != 0) {
            // Error case: unknown error
            fprintf(stderr, "PiFrame::lockMemory err=%d\n", ret);
        }
    }

    return ret;
}

/** Unlock the memory */
void PiFrame::unlock() {
        int ret = pthread_mutex_unlock(&mMemMutex);
        if (ret != 0) {
            // Error case: unknown error
            fprintf(stderr, "PiFrame::unlockMemory err=%d\n", ret);
        }
}

/** Return the size needs to read the memory */
size_t PiFrame::requiredMemSize() const {
    return mAllocatedSize;
}

/** Write a jpeg to the memory */
size_t PiFrame::write(void* src_buffer, size_t src_length) {
    if (buffer == NULL || src_length > mAllocatedSize) {
        // If the allocated buffer size is smaller than the required size or buffer is not allocated,
        // get the new memory.
        if (buffer) {
            free(buffer);
            buffer = NULL;
        }
        buffer = (uint8_t*)malloc(src_length);
        mAllocatedSize = src_length;
    }

    if (buffer == NULL) {
        // Error case: out of memory
        return -1;
    }

    memcpy(buffer, src_buffer, src_length);
    length = src_length;
    return src_length;
}

