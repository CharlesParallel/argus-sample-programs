
/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include "ArgusHelpers.h"
#include "CommonOptions.h"
#include "Error.h"
#include "Thread.h"

#include <Argus/Argus.h>

#include <unistd.h>
#include <stdlib.h>
#include <sstream>
#include <iomanip>

#ifdef ANDROID
#define FILE_PREFIX "/sdcard/DCIM/"
#else
#define FILE_PREFIX ""
#endif

using namespace Argus;
using namespace EGLStream;

/*
 * This sample opens two independent camera sessions using 2 sensors it then uses the first sensor
 * to display a preview on the screen, while taking jpeg snapshots every second from the second
 * sensor. The Jpeg saving and Preview consumption happen on two consumer threads in the
 * PreviewConsumerThread and JPEGConsumerThread classes, located in the util folder.
 */

namespace ArgusSamples
{

// Debug print macros.
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)

// static const uint32_t BAYER_WITH_ISP_CAMERA_INDEX = 1;
// static const uint32_t BAYER_WITHOUT_ISP_CAMERA_INDEX = 0;
static const uint32_t BAYER_WITH_ISP_CAMERA_INDEX = 0;
static const uint32_t BAYER_WITHOUT_ISP_CAMERA_INDEX = 1;

class ConsumerThread : public Thread
{
public:
    explicit ConsumerThread(OutputStream* stream) :
        m_stream(stream)
    {
    }
    ~ConsumerThread()
    {
    }

private:
    /** @name Thread methods */
    /**@{*/
    virtual bool threadInitialize();
    virtual bool threadExecute();
    virtual bool threadShutdown();
    /**@}*/

    OutputStream* m_stream;
    UniqueObj<FrameConsumer> m_consumer;
};

bool ConsumerThread::threadInitialize()
{
    // Create the FrameConsumer.
    m_consumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_stream));
    if (!m_consumer)
        ORIGINATE_ERROR("Failed to create FrameConsumer");

    return true;
}

bool ConsumerThread::threadExecute()
{
    char bayerWithIspOutputFileName[] = "argus_bayerWithIsp.raw";
    IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(m_stream);
    IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(m_consumer);

    // Wait until the producer has connected to the stream.
    CONSUMER_PRINT("Waiting until producer is connected...\n");
    if (iStream->waitUntilConnected() != STATUS_OK)
        ORIGINATE_ERROR("Stream failed to connect.");
    CONSUMER_PRINT("Producer has connected; continuing.\n");

    while (true)
    {
        UniqueObj<Frame> frame(iFrameConsumer->acquireFrame());
        if (!frame)
            break;

        // Use the IFrame interface to print out the frame number/timestamp, and
        // to provide access to the Image in the Frame.
        IFrame *iFrame = interface_cast<IFrame>(frame);
        if (!iFrame)
            ORIGINATE_ERROR("Failed to get IFrame interface.");
        CONSUMER_PRINT("Acquired Frame: %llu, time %llu\n",
                       static_cast<unsigned long long>(iFrame->getNumber()),
                       static_cast<unsigned long long>(iFrame->getTime()));

        // Print out some capture metadata from the frame.
        IArgusCaptureMetadata *iArgusCaptureMetadata = interface_cast<IArgusCaptureMetadata>(frame);
        if (!iArgusCaptureMetadata)
            ORIGINATE_ERROR("Failed to get IArgusCaptureMetadata interface.");
        CaptureMetadata *metadata = iArgusCaptureMetadata->getMetadata();
        ICaptureMetadata *iMetadata = interface_cast<ICaptureMetadata>(metadata);
        if (!iMetadata)
            ORIGINATE_ERROR("Failed to get ICaptureMetadata interface.");
        CONSUMER_PRINT("\tSensor Timestamp: %llu, LUX: %f\n",
                       static_cast<unsigned long long>(iMetadata->getSensorTimestamp()),
                       iMetadata->getSceneLux());

        // Print out image details, and map the buffers to read out some data.
        Image *image = iFrame->getImage();
        IImage *iImage = interface_cast<IImage>(image);
        IImage2D *iImage2D = interface_cast<IImage2D>(image);
        for (uint32_t i = 0; i < iImage->getBufferCount(); i++)
        {
            const uint8_t *d = static_cast<const uint8_t*>(iImage->mapBuffer(i));
            if (!d)
                ORIGINATE_ERROR("\tFailed to map buffer\n");

            Size2D<uint32_t> size = iImage2D->getSize(i);
            CONSUMER_PRINT("\tIImage(2D): "
                           "buffer %u (%ux%u, %u stride), "
                           "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                           i, size.width(), size.height(), iImage2D->getStride(i),
                           d[0], d[1], d[2], d[3], d[4], d[5],
                           d[6], d[7], d[8], d[9], d[10], d[11]);
        }

        EGLStream::IImageHeaderlessFile *iBayerWithIspImageHeadelessFile =
            Argus::interface_cast<EGLStream::IImageHeaderlessFile>(image);
        if (!iBayerWithIspImageHeadelessFile)
            ORIGINATE_ERROR("Failed to get RGBA IImageHeaderlessFile");

        Argus::Status status = iBayerWithIspImageHeadelessFile->writeHeaderlessFile(bayerWithIspOutputFileName);
        if (status != Argus::STATUS_OK)
            ORIGINATE_ERROR("Failed to write output file");
        printf("Wrote bayerWithIsp file : %s\n", bayerWithIspOutputFileName);
    }

    CONSUMER_PRINT("Done.\n");

    PROPAGATE_ERROR(requestShutdown());

    return true;
}

bool ConsumerThread::threadShutdown()
{
    return true;
}

static bool execute(const CommonOptions& options)
{
    const uint64_t FIVE_SECONDS_IN_NANOSECONDS = 5000000000;
    char bayerWithIspOutputFileName[] = "argus_bayerWithIsp.raw";

    // Initialize the Argus camera provider.
    UniqueObj<CameraProvider> cameraProvider(CameraProvider::create());

    // Get the ICameraProvider interface from the global CameraProvider.
    ICameraProvider *iCameraProvider =
        interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    // Get the camera devices.
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() < 2)
    {
        ORIGINATE_ERROR("Insufficient camera devices.");
    }

    /****** Bayer capture with ISP enabled ******/
    Argus::CameraDevice* bayerWithIspDevice = cameraDevices[BAYER_WITH_ISP_CAMERA_INDEX];

    ICameraProperties *iBayerWithIspProperties =
        interface_cast<ICameraProperties>(bayerWithIspDevice);
    if (!iBayerWithIspProperties)
    {
        ORIGINATE_ERROR("Failed to get the JPEG camera device properties interface.");
    }

    std::vector<Argus::SensorMode*> bayerWithIspSensorModes;
    iBayerWithIspProperties->getAllSensorModes(&bayerWithIspSensorModes);
    if (!bayerWithIspSensorModes.size())
    {
        ORIGINATE_ERROR("Failed to get valid JPEG sensor mode list.");
    }
    ISensorMode *iBayerWithIspSensorMode = interface_cast<ISensorMode>(bayerWithIspSensorModes[0]);
    if (!iBayerWithIspSensorMode)
        ORIGINATE_ERROR("Failed to get the sensor mode.");

    // Create the JPEG capture session.
    UniqueObj<CaptureSession> bayerWithIspSession = UniqueObj<CaptureSession>(
            iCameraProvider->createCaptureSession(bayerWithIspDevice));
    if (!bayerWithIspSession)
        ORIGINATE_ERROR(
            "Failed to create JPEG session with camera index %d.", BAYER_WITH_ISP_CAMERA_INDEX);
    ICaptureSession *iBayerWithIspCaptureSession = interface_cast<ICaptureSession>(bayerWithIspSession);
    if (!iBayerWithIspCaptureSession)
        ORIGINATE_ERROR("Failed to get JPEG capture session interface");

    // Create JPEG stream.
    PRODUCER_PRINT("Creating the JPEG stream.\n");
    UniqueObj<OutputStreamSettings> bayerWithIspSettings(
        iBayerWithIspCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iBayerWithIspSettings =
        interface_cast<IEGLOutputStreamSettings>(bayerWithIspSettings);
    if (iBayerWithIspSettings)
    {
        iBayerWithIspSettings->setPixelFormat(PIXEL_FMT_RAW16);
        iBayerWithIspSettings->setResolution(iBayerWithIspSensorMode->getResolution());
        iBayerWithIspSettings->setMetadataEnable(true);
    }
    UniqueObj<OutputStream> bayerWithIspStream(
            iBayerWithIspCaptureSession->createOutputStream(
                bayerWithIspSettings.get()));
    if (!bayerWithIspStream.get())
        ORIGINATE_ERROR("Failed to create JPEG OutputStream");

    // Launch the FrameConsumer thread to consume frames from the OutputStream.
    PRODUCER_PRINT("Launching consumer thread\n");
    ConsumerThread frameConsumerThread(bayerWithIspStream.get());
    PROPAGATE_ERROR(frameConsumerThread.initialize());

    // Wait until the consumer is connected to the stream.
    PROPAGATE_ERROR(frameConsumerThread.waitRunning());

    // Create capture request and enable output stream.
    UniqueObj<Request> request(iBayerWithIspCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(bayerWithIspStream.get());

    // Set the sensor mode in the request.
    ISourceSettings *iSourceSettings = interface_cast<ISourceSettings>(request);
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get source settings request interface");
    iSourceSettings->setSensorMode(bayerWithIspSensorModes[0]);

    // Submit capture requests.
    PRODUCER_PRINT("Starting repeat capture requests.\n");
    if (iBayerWithIspCaptureSession->repeat(request.get()) != STATUS_OK)
        ORIGINATE_ERROR("Failed to start repeat capture request");

    // Wait for specified number of seconds.
    sleep(options.captureTime());

    // Stop the repeating request and wait for idle.
    iBayerWithIspCaptureSession->stopRepeat();
    iBayerWithIspCaptureSession->waitForIdle();

    // Destroy the output stream to end the consumer thread.
    bayerWithIspStream.reset();

    // Wait for the consumer thread to complete.
    PROPAGATE_ERROR(frameConsumerThread.shutdown());

    // Shut down Argus.
    cameraProvider.reset();

    PRODUCER_PRINT("Done -- exiting.\n");

    // Create the FrameConsumer to consume the output frames from the stream.
    // Argus::UniqueObj<EGLStream::FrameConsumer> bayerWithIspConsumer(
    //     EGLStream::FrameConsumer::create(bayerWithIspStream.get()));
    // EGLStream::IFrameConsumer *iBayerWithIspConsumer =
    //     Argus::interface_cast<EGLStream::IFrameConsumer>(bayerWithIspConsumer);
    // if (!iBayerWithIspConsumer)
    //     ORIGINATE_ERROR("Failed to initialize Consumer");
    //
    // UniqueObj<Request> bayerWithIspRequest(iBayerWithIspCaptureSession->createRequest(CAPTURE_INTENT_STILL_CAPTURE));
    // if (!bayerWithIspRequest)
    //     ORIGINATE_ERROR("Failed to create Request");
    //
    // IRequest *iBayerWithIspRequest = interface_cast<IRequest>(bayerWithIspRequest);
    // if (!iBayerWithIspRequest)
    //     ORIGINATE_ERROR("Failed to create Request interface");
    //
    // iBayerWithIspRequest->setEnableIspStage(true);
    // iBayerWithIspRequest->enableOutputStream(bayerWithIspStream.get());
    //
    // Argus::ISourceSettings *iBayerWithIspSourceSettings =
    //     Argus::interface_cast<Argus::ISourceSettings>(bayerWithIspRequest);
    // if (!iBayerWithIspSourceSettings)
    //     ORIGINATE_ERROR("Failed to get source settings request interface");
    // iBayerWithIspSourceSettings->setSensorMode(bayerWithIspSensorModes[0]);

    /****** Bayer capture with ISP disabled ******/
    // Argus::CameraDevice* bayerWithOutIspDevice = cameraDevices[BAYER_WITHOUT_ISP_CAMERA_INDEX];
    //
    // ICameraProperties *iBayerWithOutIspProperties =
    //     interface_cast<ICameraProperties>(bayerWithOutIspDevice);
    // if (!iBayerWithOutIspProperties)
    // {
    //     ORIGINATE_ERROR("Failed to get the JPEG camera device properties interface.");
    // }
    //
    // Argus::SensorMode* bayerWithOutIspSensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
    //         bayerWithOutIspDevice, options.sensorModeIndex());
    // // std::vector<Argus::SensorMode*> bayerWithOutIspSensorModes;
    // // iBayerWithOutIspProperties->getBasicSensorModes(&bayerWithOutIspSensorModes);
    // // if (!bayerWithOutIspSensorModes.size())
    // // {
    // //     ORIGINATE_ERROR("Failed to get valid JPEG sensor mode list.");
    // // }
    // ISensorMode *iBayerWithOutIspSensorMode = interface_cast<ISensorMode>(bayerWithOutIspSensorMode);
    // if (!iBayerWithOutIspSensorMode)
    //     ORIGINATE_ERROR("Failed to get the sensor mode.");
    //
    // printf("Capturing from Resolution (%dx%d)\n",
    //        iBayerWithOutIspSensorMode->getResolution().width(), iBayerWithOutIspSensorMode->getResolution().height());
    //
    // // Create the preview capture session.
    // UniqueObj<CaptureSession> bayerWithOutIspSession = UniqueObj<CaptureSession>(
    //         iCameraProvider->createCaptureSession(bayerWithOutIspDevice));
    // if (!bayerWithOutIspSession)
    //     ORIGINATE_ERROR(
    //         "Failed to create preview session with camera index %d.", BAYER_WITHOUT_ISP_CAMERA_INDEX);
    // ICaptureSession *iBayerWithOutIspCaptureSession = interface_cast<ICaptureSession>(bayerWithOutIspSession);
    // if (!iBayerWithOutIspCaptureSession)
    //     ORIGINATE_ERROR("Failed to get preview capture session interface");
    //
    // // Create preview stream.
    // PRODUCER_PRINT("Creating the preview stream.\n");
    // UniqueObj<OutputStreamSettings> bayerWithOutIspSettings(
    //     iBayerWithOutIspCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    // IEGLOutputStreamSettings *iBayerWithOutIspSettings =
    //     interface_cast<IEGLOutputStreamSettings>(bayerWithOutIspSettings);
    // if (iBayerWithOutIspSettings)
    // {
    //     iBayerWithOutIspSettings->setPixelFormat(PIXEL_FMT_RAW16);
    //     iBayerWithOutIspSettings->setResolution(iBayerWithOutIspSensorMode->getResolution());
    //     iBayerWithOutIspSettings->setMetadataEnable(true);
    //
    // }
    // UniqueObj<OutputStream> bayerWithOutIspStream(
    //         iBayerWithOutIspCaptureSession->createOutputStream(bayerWithOutIspSettings.get()));
    // IEGLOutputStream *iBayerWithOutIspStream = interface_cast<IEGLOutputStream>(bayerWithOutIspStream);
    // if (!iBayerWithOutIspStream)
    //     ORIGINATE_ERROR("Failed to create preview OutputStream");
    //
    // // Create the FrameConsumer to consume the output frames from the stream.
    // Argus::UniqueObj<EGLStream::FrameConsumer> bayerWithOutIspConsumer(
    //     EGLStream::FrameConsumer::create(bayerWithOutIspStream.get()));
    // EGLStream::IFrameConsumer *iBayerWithOutIspConsumer =
    //     Argus::interface_cast<EGLStream::IFrameConsumer>(bayerWithOutIspConsumer);
    // if (!iBayerWithOutIspConsumer)
    //     ORIGINATE_ERROR("Failed to initialize Consumer");
    //
    // // Create the two requests
    // UniqueObj<Request> bayerWithOutIspRequest(iBayerWithOutIspCaptureSession->createRequest());
    // if (!bayerWithOutIspRequest)
    //     ORIGINATE_ERROR("Failed to create Request");
    //
    // IRequest *iBayerWithOutIspRequest = interface_cast<IRequest>(bayerWithOutIspRequest);
    // if (!iBayerWithOutIspRequest)
    //     ORIGINATE_ERROR("Failed to create Request interface");
    //
    // iBayerWithOutIspRequest->setEnableIspStage(false);
    // iBayerWithOutIspRequest->enableOutputStream(bayerWithOutIspStream.get());
    //
    // Argus::ISourceSettings *iBayerWithOutIspSourceSettings =
    //     Argus::interface_cast<Argus::ISourceSettings>(bayerWithOutIspRequest);
    // if (!iBayerWithOutIspSourceSettings)
    //     ORIGINATE_ERROR("Failed to get source settings request interface");
    // iBayerWithOutIspSourceSettings->setSensorMode(bayerWithOutIspSensorMode);


    // Argus is now all setup and ready to capture
    // Submit capture requests.
    // PRODUCER_PRINT("Starting repeat capture requests.\n");
    //
    // uint32_t bayerWithIspRequestId = iBayerWithIspCaptureSession->capture(bayerWithIspRequest.get());
    // if (!bayerWithIspRequestId)
    //     ORIGINATE_ERROR("Failed to submit capture request");
    // uint32_t bayerWithOutIspRequestId = iBayerWithOutIspCaptureSession->capture(bayerWithOutIspRequest.get());
    // if (!bayerWithOutIspRequestId)
    //     ORIGINATE_ERROR("Failed to submit capture request");
    //
    // Argus::Status status;
    // Argus::UniqueObj<EGLStream::Frame> bayerWithIspFrame(
    //     iBayerWithIspConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));
    // if (status != Argus::STATUS_OK)
    //     ORIGINATE_ERROR("Failed to get consumer");
    // Argus::UniqueObj<EGLStream::Frame> bayerWithOutIspFrame(
    //     iBayerWithOutIspConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));
    // if (status != Argus::STATUS_OK)
    //     ORIGINATE_ERROR("Failed to get consumer");
    //
    // EGLStream::IFrame *iBayerWithIspFrame = Argus::interface_cast<EGLStream::IFrame>(bayerWithIspFrame);
    // if (!iBayerWithIspFrame)
    //     ORIGINATE_ERROR("Failed to get RGBA IFrame interface");
    // EGLStream::IFrame *iBayerWithOutIspFrame = Argus::interface_cast<EGLStream::IFrame>(bayerWithOutIspFrame);
    // if (!iBayerWithOutIspFrame)
    //     ORIGINATE_ERROR("Failed to get RGBA IFrame interface");
    //
    // EGLStream::Image *bayerWithIspImage = iBayerWithIspFrame->getImage();
    // if (!bayerWithIspImage)
    //     ORIGINATE_ERROR("Failed to get RGBA Image from iFrame->getImage()");
    // EGLStream::Image *bayerWithOutIspImage = iBayerWithOutIspFrame->getImage();
    // if (!bayerWithOutIspImage)
    //     ORIGINATE_ERROR("Failed to get RGBA Image from iFrame->getImage()");
    //
    // EGLStream::IImage *iBayerWithIspImage = Argus::interface_cast<EGLStream::IImage>(bayerWithIspImage);
    // if (!iBayerWithIspImage)
    //     ORIGINATE_ERROR("Failed to get RGBA IImage");
    // EGLStream::IImage *iBayerWithOutIspImage = Argus::interface_cast<EGLStream::IImage>(bayerWithOutIspImage);
    // if (!iBayerWithOutIspImage)
    //     ORIGINATE_ERROR("Failed to get RGBA IImage");
    //
    // EGLStream::IImage2D *iBayerWithIspImage2D = Argus::interface_cast<EGLStream::IImage2D>(bayerWithIspImage);
    // if (!iBayerWithIspImage2D)
    //     ORIGINATE_ERROR("Failed to get RGBA iImage2D");
    // EGLStream::IImage2D *iBayerWithOutIspImage2D = Argus::interface_cast<EGLStream::IImage2D>(bayerWithOutIspImage);
    // if (!iBayerWithOutIspImage2D)
    //     ORIGINATE_ERROR("Failed to get RGBA iImage2D");
    //
    // EGLStream::IImageHeaderlessFile *iBayerWithIspImageHeadelessFile =
    //     Argus::interface_cast<EGLStream::IImageHeaderlessFile>(bayerWithIspImage);
    // if (!iBayerWithIspImageHeadelessFile)
    //     ORIGINATE_ERROR("Failed to get RGBA IImageHeaderlessFile");
    // EGLStream::IImageHeaderlessFile *iBayerWithOutIspImageHeadelessFile =
    //     Argus::interface_cast<EGLStream::IImageHeaderlessFile>(bayerWithOutIspImage);
    // if (!iBayerWithOutIspImageHeadelessFile)
    //     ORIGINATE_ERROR("Failed to get RGBA IImageHeaderlessFile");
    //
    // status = iBayerWithIspImageHeadelessFile->writeHeaderlessFile(bayerWithIspOutputFileName);
    // if (status != Argus::STATUS_OK)
    //     ORIGINATE_ERROR("Failed to write output file");
    // printf("Wrote bayerWithIsp file : %s\n", bayerWithIspOutputFileName);
    // status = iBayerWithOutIspImageHeadelessFile->writeHeaderlessFile(bayerWithOutIspOutputFileName);
    // if (status != Argus::STATUS_OK)
    //     ORIGINATE_ERROR("Failed to write output file");
    // printf("Wrote bayerWithOutIsp file : %s\n", bayerWithOutIspOutputFileName);
    //
    // // Shut down Argus.
    // cameraProvider.reset();
    //
    // PRODUCER_PRINT("Done -- exiting.\n");
    return true;
}

}; // namespace ArgusSamples

int main(int argc, char** argv)
{
    ArgusSamples::CommonOptions options(
        basename(argv[0]),
        ArgusSamples::CommonOptions::Option_D_CameraDevice |
        ArgusSamples::CommonOptions::Option_M_SensorMode |
        ArgusSamples::CommonOptions::Option_T_CaptureTime);

    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    if (!ArgusSamples::execute(options))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
