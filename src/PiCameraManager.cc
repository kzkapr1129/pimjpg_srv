#include "PiCameraManager.h"
#include "PiFrame.h"
#include "PiException.h"
#include <stdio.h>
#include <pthread.h>
#include <algorithm>

#define MUTEX_TIMEOUT_SEC 3

PiCameraManager::PiCameraManager(const PiCamSettings& settings)
        : mSettings(settings), mCamera(NULL) {
    mFramesMutexTimeout.tv_sec = MUTEX_TIMEOUT_SEC;
    mFramesMutexTimeout.tv_nsec = 0;
    pthread_mutex_init(&mFramesMutex, NULL);
}

PiCameraManager::~PiCameraManager() {
    delete mCamera;

    if (mFrames.size()) {
        fprintf(stderr, "warn: mFrames has values when called Destructor. size=%d\n", mFrames.size());
    }

    std::vector<PiFrame*>::iterator it = mFrames.begin();
    for (; it != mFrames.end(); it++) {
        PiFrame* frame = *it;
        delete frame;
    }
}

PiFrame* PiCameraManager::attach() {
     int status = ENOMEM;

     // Initialize PiFrame
     PiFrame* frame = new PiFrame(mSettings.height * mSettings.width * 3, &status);
     if (frame == NULL || status != 0) {
         fprintf(stderr, "Faild to initialize PiFrame status=%d\n", status);
         delete frame;
         return NULL;
     }

    // Lock
    status = pthread_mutex_timedlock(&mFramesMutex,  &mFramesMutexTimeout);
    if (status == 0) {

        // Initialize PiCamera If not constructed.
        if (mCamera == NULL) {
            mCamera = new PiCamera(mSettings, this, &status);
            if (mCamera == NULL || status != 0) {
                fprintf(stderr, "Faild to initialize PiCamera status=%d\n", status);
                delete mCamera; mCamera = NULL;
                delete frame; frame = NULL;
            }
        }

        if (frame) {
            TRAP1(catched, msg, mFrames.push_back(frame););
             if (catched) {
                fprintf(stderr, "Error in mFrames.push_back msg=%s\n", msg.c_str());
                delete mCamera; mCamera = NULL;
                delete frame; frame = NULL;
             }
         }

        // Unlock
        status = pthread_mutex_unlock(&mFramesMutex);
        if (status) fprintf(stderr, "Failed to unlock mFramesMutex status=%d\n", status);
    } else {
        fprintf(stderr, "Failed to lock mFramesMutex err=%d\n", errno);
        delete frame;
        frame = NULL;
    }

    return frame;
}

void PiCameraManager::detach(PiFrame*& frame) {
    if (frame != NULL) {

        bool removed = false;
        size_t numFrames = -1;

        // Lock
        int status = pthread_mutex_lock(&mFramesMutex);
        if (status == 0) {
            std::vector<PiFrame*>::iterator it = std::find(mFrames.begin(), mFrames.end(), frame);
            if (it != mFrames.end()) {
                mFrames.erase(it);
                removed = true;
            }

            // Get num of frames.
            numFrames = mFrames.size();

            status = pthread_mutex_unlock(&mFramesMutex);
        }

        if (status) {
            fprintf(stderr, "Erro in mFramesMutex status=%d\n", status);
        }

        // Check whether removing is success.
        if (!removed) {
            fprintf(stderr, "Couldn't find PiFrame %p\n", frame);
        } else {
            delete frame;
        }
       frame = NULL;

        if (numFrames == 0) {
            delete mCamera;
            mCamera = NULL;
        }
    }
}

void PiCameraManager::onFrame(const DinamicBuffer& buffer) {
    // Lock
    int status = pthread_mutex_timedlock(&mFramesMutex,  &mFramesMutexTimeout);
    if (status == 0) {

        // Copy buffer to each mFrames
        std::vector<PiFrame*>::iterator it = mFrames.begin();
        for (; it != mFrames.end(); it++) {

            PiFrame* frame = *it;

            // Lock
            status = frame->lock(3);
            if (status == 0) {

                size_t buf_length = buffer.offset;
                size_t wrote_size = 0;

                TRAP1(catched, msg, wrote_size = frame->write(buffer.values, buf_length););
                if (catched) {
                    fprintf(stderr, "Exception in PiFrame#write %s\n", msg.c_str());
                }

                // Unlock
                frame->unlock();

                if (buf_length == wrote_size) {
                    TRAP2(catched, msg, frame->sendReadySignal(););
                    if (catched) {
                        fprintf(stderr, "Exception in sendReadySignal %s\n", msg.c_str());
                    }

                } else {
                    fprintf(stderr, "Failed to write JPEG-frame to PiFrame buf_len=%d, wrote_size=%d\n",
                            buf_length, wrote_size);
                }
            }
        }

        // Unlock
        status = pthread_mutex_unlock(&mFramesMutex);
    } else {
        fprintf(stderr, "onFrame: mFrameMutex lock err=%d\n", status);
    }
}

