#pragma once

#include <stdint.h>
#include <pthread.h>

class PiFrame {
public:
    PiFrame(size_t initial_mem_size, int* status);
    ~PiFrame();

    // sync functions
    int waitForReady(int sec = 0, long nsec = 0);
    void sendReadySignal();
    int lock(int sec = 0, long nsec = 0);
    void unlock();

   // getter functions
    size_t requiredMemSize() const;
    size_t write(void* src_buffer, size_t length);

    uint8_t* buffer;
    size_t length;

private:
    size_t mAllocatedSize;
    
    pthread_mutex_t mMemMutex;
    pthread_mutex_t mSignalMutex;
    pthread_cond_t mSignalCond;
};
