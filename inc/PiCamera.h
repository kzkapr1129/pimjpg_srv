#pragma once
#include "PiBuffer.h"
#include <mmal/mmal.h>
#include <mmal/util/mmal_util.h>
#include <mmal/util/mmal_default_components.h>
#include <mmal/util/mmal_connection.h>
#include <mmal/util/mmal_util_params.h>

struct PiCamSettings {
    int width;
    int height;
    int fps;
    int quality;
    long timeout_writing_frame; // ex) 100000000 = 100ms

    PiCamSettings() : width(640), height(480), fps(15), quality(85),
            timeout_writing_frame(100000000) {}
};

class PiCameraListener {
public:
    virtual ~PiCameraListener() {}
    virtual void onFrame(const DinamicBuffer& buffer) = 0;
};

class PiCamera {
public:
    PiCamera(const PiCamSettings& settings, PiCameraListener* listener, int* status);
    ~PiCamera();

private:
    static void encoder_buffer_callback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer);
    static void camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer);

    MMAL_COMPONENT_T* mCamera;
    MMAL_COMPONENT_T* mPreview;
    MMAL_PORT_T* mCameraPreviewPort;
    MMAL_PORT_T* mCameraVideoPort;
    MMAL_PORT_T* mCameraStillPort;
    MMAL_PORT_T* mPreviewInputPort;
    MMAL_CONNECTION_T* mCameraPreviewConnection;
    MMAL_COMPONENT_T* mEncoder;
    MMAL_PORT_T* mEncoderInput;
    MMAL_PORT_T* mEncoderOutput;
    MMAL_POOL_T* mPool;
    MMAL_CONNECTION_T* mEncoderConnection;
    PiCameraListener* mListener;
    DinamicBuffer* mBuffer;
    const PiCamSettings mSettings;
    int last_encode_error;
};


