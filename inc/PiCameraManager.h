#pragma once

#include "PiCamera.h"
#include <sys/time.h>
#include <vector>

class PiFrame;
class PiCameraManager : public PiCameraListener {
public:
    PiCameraManager(const PiCamSettings& settings);
    ~PiCameraManager();

    PiFrame* attach();
    void detach(PiFrame*& );

private:
    void onFrame(const DinamicBuffer& buffer);

    const PiCamSettings& mSettings;
    PiCamera* mCamera;
    std::vector<PiFrame*> mFrames;
    pthread_mutex_t mFramesMutex;
    timespec mFramesMutexTimeout;
};
