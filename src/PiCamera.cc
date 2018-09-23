#include "PiBuffer.h"
#include "PiCamera.h"
#include "PiFrame.h"
#include <stdio.h>
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

#define MMAL_CAMERA_PREVIEW_PORT 0
#define MMAL_CAMERA_VIDEO_PORT 1
#define MMAL_CAMERA_CAPTURE_PORT 2

// Stills format information
#define STILLS_FRAME_RATE_NUM 3
#define STILLS_FRAME_RATE_DEN 1
/// Video render needs at least 2 buffers.
#define VIDEO_OUTPUT_BUFFERS_NUM 3
// Layer that preview window should be displayed on
#define PREVIEW_LAYER      2
// Frames rates of 0 implies variable, but denominator needs to be 1 to prevent div by 0
#define PREVIEW_FRAME_RATE_NUM 0
#define PREVIEW_FRAME_RATE_DEN 1

#define DBG

static MMAL_STATUS_T connect_ports(MMAL_PORT_T* output_port, MMAL_PORT_T* input_port,
        MMAL_CONNECTION_T** connection) {
    if (connection == NULL) {
        return MMAL_EINVAL;
    }
    *connection = NULL;

    MMAL_STATUS_T status;
    status = mmal_connection_create(connection, output_port, input_port,
            MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT);
    if (status == MMAL_SUCCESS) {
        status = mmal_connection_enable(*connection);
        if (status != MMAL_SUCCESS) {
            mmal_connection_destroy(*connection);
            fprintf(stderr, "Error enabling mmal connection\n");
        }
    } else {
        fprintf(stderr, "Error creating mmal connection\n");
    }

    return status;
}

/** Constructor */
PiCamera::PiCamera(const PiCamSettings& settings, PiCameraListener* listener, int* ret_status) :
        mCamera(NULL), mPreview(NULL), mCameraPreviewPort(NULL),
        mCameraVideoPort(NULL), mCameraStillPort(NULL), mPreviewInputPort(NULL),
        mCameraPreviewConnection(NULL), mEncoder(NULL), mEncoderInput(NULL),
        mEncoderOutput(NULL), mPool(NULL), mEncoderConnection(NULL),
        mListener(listener), mBuffer(NULL), mSettings(settings), last_encode_error(0) {

    // initialize a return code
    if (ret_status) *ret_status = MMAL_SUCCESS;

    if (mListener == NULL) {
        *ret_status = MMAL_EINVAL;
        return;
    }

    // initialize the temporary buffer for storing a jpeg-frame.
    mBuffer = new DinamicBuffer();
    if (mBuffer == NULL) {
        if (ret_status) *ret_status = MMAL_ENOMEM;
        return;
    }

    RASPICAM_CAMERA_PARAMETERS c_params;
    // Get default settings
    raspicamcontrol_set_defaults(&c_params);
    // Set camera parameters
    c_params.rotation = settings.rotation;
    // Dump parameters
    raspicamcontrol_dump_parameters(&c_params);

    // initialize gpu
    bcm_host_init();
    
    int status = 0;
    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_CAMERA, &mCamera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Error create camera\n");
        if (ret_status) *ret_status = status;
        return;
    }

    if (!mCamera->output_num) {
        fprintf(stderr, "Camera doesn't have output ports\n");
        if (ret_status) *ret_status = MMAL_EIO;
        return;
    }

    mCameraPreviewPort = mCamera->output[MMAL_CAMERA_PREVIEW_PORT];
    mCameraVideoPort     = mCamera->output[MMAL_CAMERA_VIDEO_PORT];
    mCameraStillPort        = mCamera->output[MMAL_CAMERA_CAPTURE_PORT];

    //Enable camera control port
    // Enable the camera, and tell it its control callback function
    status = mmal_port_enable(mCamera->control, camera_control_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to enable camera port\n");
        if (ret_status) *ret_status = status;
        return;
    }

    {
        MMAL_PARAMETER_CAMERA_CONFIG_T cam_config = {
        { MMAL_PARAMETER_CAMERA_CONFIG, sizeof (cam_config)},
            .max_stills_w = settings.width,
            .max_stills_h = settings.height,
            .stills_yuv422 = 0,
            .one_shot_stills = 0,
            .max_preview_video_w = settings.width,
            .max_preview_video_h = settings.height,
            .num_preview_video_frames = 3,
            .stills_capture_circular_buffer_height = 0,
            .fast_preview_resume = 0,
            .use_stc_timestamp = MMAL_PARAM_TIMESTAMP_MODE_RESET_STC
        };
        mmal_port_parameter_set(mCamera->control, &cam_config.hdr);
    }

    //Set camera parameters
    status = raspicamcontrol_set_all_parameters(mCamera, &c_params);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "camera parameters couldn't be set\n");
        if (ret_status) *ret_status = status;
        return;
    }

    MMAL_ES_FORMAT_T *format;

    // Set the encode format on the Preview port
    format = mCameraPreviewPort->format;
    format->encoding             = MMAL_ENCODING_OPAQUE;
    format->es->video.width  = VCOS_ALIGN_UP(settings.width, 32);
    format->es->video.height = VCOS_ALIGN_UP(settings.height, 16);
    format->es->video.crop.x  = 0;
    format->es->video.crop.y  = 0;
    format->es->video.crop.width = settings.width;
    format->es->video.crop.height = settings.height;
    format->es->video.frame_rate.num = PREVIEW_FRAME_RATE_NUM;
    format->es->video.frame_rate.den   = PREVIEW_FRAME_RATE_DEN;

    status = mmal_port_format_commit(mCameraPreviewPort);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "failed to commit preview\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Create preview component
    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &mPreview);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to create preview component\n");
        if (ret_status) *ret_status = status;
        return;
    }

    if (!mPreview->input_num) {
        fprintf(stderr, "No input ports found on preview component\n");
        if (ret_status) *ret_status = MMAL_EIO;
        return;
    }

    mPreviewInputPort = mPreview->input[0];

    MMAL_DISPLAYREGION_T param;
    param.hdr.id = MMAL_PARAMETER_DISPLAYREGION;
    param.hdr.size = sizeof(MMAL_DISPLAYREGION_T);

    param.set = MMAL_DISPLAY_SET_LAYER;
    param.layer = PREVIEW_LAYER;

    param.set |= MMAL_DISPLAY_SET_ALPHA;
    param.alpha = 255;

    param.set |= MMAL_DISPLAY_SET_FULLSCREEN;
    param.fullscreen = 1;

    status = mmal_port_parameter_set(mPreviewInputPort, &param.hdr);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to set preview port parameters\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Set encode format on the video port
    format = mCameraVideoPort->format;
    format->encoding_variant = MMAL_ENCODING_I420;
    format->encoding = MMAL_ENCODING_I420;
    format->es->video.width = settings.width;
    format->es->video.height = settings.height;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = settings.width;
    format->es->video.crop.height = settings.height;
    format->es->video.frame_rate.num = settings.fps;
    format->es->video.frame_rate.den = 1;
    status = mmal_port_format_commit(mCameraVideoPort);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "camera video format couldn't be set\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Ensure there are enough buffers to avoid dropping frames
    if (mCameraVideoPort->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM) {
        mCameraVideoPort->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM;
    }

    // Set our stills format on the stills (for encoder) port
    format = mCameraStillPort->format;
    format->encoding = MMAL_ENCODING_OPAQUE;
    format->es->video.width = settings.width;
    format->es->video.height = settings.height;
    format->es->video.crop.x = 0;
    format->es->video.crop.y = 0;
    format->es->video.crop.width = settings.width;
    format->es->video.crop.height = settings.height;
    format->es->video.frame_rate.num = STILLS_FRAME_RATE_NUM;
    format->es->video.frame_rate.den = STILLS_FRAME_RATE_DEN;

    status = mmal_port_format_commit(mCameraStillPort);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "format couldn't be set\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Ensure there are enough buffers to avoid dropping frames
    if (mCameraStillPort->buffer_num < VIDEO_OUTPUT_BUFFERS_NUM) {
        mCameraStillPort->buffer_num = VIDEO_OUTPUT_BUFFERS_NUM;
    }

    // Enable component
    status = mmal_component_enable(mCamera);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "camera couldn't be enabled\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Enable component
    status = mmal_component_enable(mPreview);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to enable preview/null sink component\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Create Encoder
    status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_ENCODER, &mEncoder);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to create JPEG encoder component\n");
        if (ret_status) *ret_status = status;
        return;
    }

    if (!mEncoder->input_num || !mEncoder->output_num) {
        fprintf(stderr, "Unable to create JPEG encoder input/output ports\n");
        if (ret_status) *ret_status = MMAL_EIO;
        return;
    }

    mEncoderInput = mEncoder->input[0];
    mEncoderOutput = mEncoder->output[0];

    // We want same format on input and output
    mmal_format_copy(mEncoderOutput->format,  mEncoderInput->format);

    // Specify out output format JPEG
    mEncoderOutput->format->encoding = MMAL_ENCODING_JPEG;

    mEncoderOutput->buffer_size = mEncoderOutput->buffer_size_recommended;

    if (mEncoderOutput->buffer_size < mEncoderOutput->buffer_size_min) {
        mEncoderOutput->buffer_size = mEncoderOutput->buffer_size_min;
    }

    printf("Encoder Buffer Size %i\n", mEncoderOutput->buffer_size);

    mEncoderOutput->buffer_num = mEncoderOutput->buffer_num_recommended;

    if (mEncoderOutput->buffer_num < mEncoderOutput->buffer_num_min) {
        mEncoderOutput->buffer_num = mEncoderOutput->buffer_num_min;
    }

    // Commit the port changes to the output port
    status = mmal_port_format_commit(mEncoderOutput);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to set video format output ports\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Set the JPEG quality level
    status = mmal_port_parameter_set_uint32(mEncoderOutput,
            MMAL_PARAMETER_JPEG_Q_FACTOR, settings.quality);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to set JPEG quality\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Enable encoder component
    status = mmal_component_enable(mEncoder);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to enable encoder component\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Create pool of buffer headers for the output port to consume
    mPool = mmal_port_pool_create(mEncoderOutput, mEncoderOutput->buffer_num, mEncoderOutput->buffer_size);
    if (!mPool) {
        fprintf(stderr, "Failed to create buffer header pool for encoder output port\n");
        if (ret_status) *ret_status = MMAL_ENOMEM;
        return;
    }

    // Now connect the camera to the encoder
    status = connect_ports(mCameraVideoPort, mEncoder->input[0], &mEncoderConnection);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to connect components\n");
        if (ret_status) *ret_status = status;
        return;
    }

    // Set up our userdata - this is passed though to the callback where we need the information
    mEncoder->output[0]->userdata = (MMAL_PORT_USERDATA_T *)this;

    // Enable the encoder output port and tell it its callback function
    status = mmal_port_enable(mEncoder->output[0], encoder_buffer_callback);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "Unable to enable encoder component\n");
        if (ret_status) *ret_status = status;
        return;
    }

    int num = mmal_queue_length(mPool->queue);
    for (int q = 0; q < num; q++) {
        MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(mPool->queue);
        if (!buffer) {
            fprintf(stderr, "Unable to get a required buffer from pool queue\n");
            if (ret_status) *ret_status = MMAL_ENOMEM;
            return;
        }

        status = mmal_port_send_buffer(mEncoder->output[0], buffer);
        if (status != MMAL_SUCCESS) {
            fprintf(stderr, "Unable to send a buffer to encoder output port\n");
            if (ret_status) *ret_status = status;
            return;
        }
    }

    status = mmal_port_parameter_set_boolean(mCameraVideoPort, MMAL_PARAMETER_CAPTURE, 1);
    if (status != MMAL_SUCCESS) {
        fprintf(stderr, "starting capture failed\n");
        if (ret_status) *ret_status = status;
        return;
    }

    printf("Starting capture\n");
}

/** Destructor */
PiCamera::~PiCamera() {
    printf("will cleanup components\n");

    if (mCameraVideoPort && mCameraVideoPort->is_enabled) mmal_port_disable(mCameraVideoPort);
    if (mCameraStillPort && mCameraStillPort->is_enabled) mmal_port_disable(mCameraStillPort);
    if (mCameraPreviewConnection) mmal_connection_destroy(mCameraPreviewConnection);
    if (mEncoder && mEncoder->output[0] && mEncoder->output[0]->is_enabled) mmal_port_disable(mEncoder->output[0]);
    if (mEncoderConnection) mmal_connection_destroy(mEncoderConnection);
    
    // Disable components
    if (mEncoder) mmal_component_disable(mEncoder);
    if (mPreview) mmal_component_disable(mPreview);
    if (mCamera) mmal_component_disable(mCamera);

    // Disable encoder component
    // Get rid of any port buffers first
    if (mPool) mmal_port_pool_destroy(mEncoder->output[0], mPool);

    // Destroy components
    if (mEncoder) mmal_component_destroy(mEncoder);
    if (mPreview) mmal_component_destroy(mPreview);
    if (mCamera) mmal_component_destroy(mCamera);

    delete mBuffer;

    printf("finished\n");
}

void PiCamera::encoder_buffer_callback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer) {
    PiCamera* self = (PiCamera*)port->userdata;

    if (self) {
        // If error have occured, ignore to write JPEG-frame to the tmp buffer
        if (!self->last_encode_error) {

            // Lock the memory stored JPEG frame.
            mmal_buffer_header_mem_lock(buffer);

            int status = self->mBuffer->append(buffer->data, buffer->length);

            // Unlock the memory
            mmal_buffer_header_mem_unlock(buffer);
            
            if (status) {
                // To Ignore until the next frame is started, set a error code.
                self->last_encode_error = status;
            }
        }

        if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_FRAME_END) {
            if (self->last_encode_error) {
                fprintf(stderr, "Ignore to send signal, for error occured. last err=%d\n", self->last_encode_error);
            } else {
                self->mListener->onFrame(*self->mBuffer);
            }

            // Initialize starting frame.
            self->mBuffer->resetOffset();
            self->last_encode_error = 0;

        } else if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED) {
            fprintf(stderr, "MMAL_BUFFER_HEADER_FLAG_TRANSMISSION_FAILED ...\n");
            // To Ignore until the next frame is started, set a error code.
            self->last_encode_error = -1;

            // Initialize starting frame.
            self->mBuffer->resetOffset();
        }
    }

    mmal_buffer_header_release(buffer);

    if (port->is_enabled) {
        MMAL_STATUS_T status;
        MMAL_BUFFER_HEADER_T *new_buffer;
        new_buffer = mmal_queue_get(self->mPool->queue);

        if (new_buffer) {
            status = mmal_port_send_buffer(port, new_buffer);
            if (status != MMAL_SUCCESS) {
                DBG("Failed returning a buffer to the encoder port\n");
            }
         } else {
            DBG("Unable to return a buffer to the encoder port\n");
         }
    }
}

void PiCamera::camera_control_callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer) {
    DBG("Received a camera event %d\n", buffer->cmd);
    mmal_buffer_header_release(buffer);
}

