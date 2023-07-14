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

#include "ArgusHelpers.h"
#include "CommonOptions.h"
#include "Error.h"
#include "Thread.h"
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <stdio.h>
#include <stdlib.h>

#include <Argus/Argus.h>

#include <iomanip>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>

#ifdef ANDROID
#define FILE_PREFIX "/sdcard/DCIM/"
#else
#define FILE_PREFIX ""
#endif

using namespace Argus;
using namespace EGLStream;

/*
 * This program opens an output stream for a specified capture time and
 * outputs the raw bayer data. The consumer thread captures a frame and writes
 * the raw bayer data to a file every second.
 */

namespace ArgusSamples {

// Debug print macros.
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)

static const uint32_t CAMERA_INDEX = 0;

/*******************************************************************************
 * FrameConsumer thread:
 *   Creates a FrameConsumer object to read frames from the OutputStream, then
 *   acquires frame info from the IImage while frames are presented.
 *   It will also write the raw bayer data to a .raw file.
 ******************************************************************************/
class ConsumerThread : public Thread {
public:
  explicit ConsumerThread(OutputStream *stream) : m_stream(stream) {}
  ~ConsumerThread() {}

private:
  /** @name Thread methods */
  /**@{*/
  virtual bool threadInitialize();
  virtual bool threadExecute();
  virtual bool threadShutdown();
  /**@}*/

  OutputStream *m_stream;
  UniqueObj<FrameConsumer> m_consumer;
};

bool ConsumerThread::threadInitialize() {
  // Create the FrameConsumer.
  m_consumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_stream));
  if (!m_consumer)
    ORIGINATE_ERROR("Failed to create FrameConsumer");

  return true;
}

bool ConsumerThread::threadExecute() {
  char bayerWithIspOutputFileName[] = "argus_bayerWithIsp.raw";
  IEGLOutputStream *iStream = interface_cast<IEGLOutputStream>(m_stream);
  IFrameConsumer *iFrameConsumer = interface_cast<IFrameConsumer>(m_consumer);

  // Wait until the producer has connected to the stream.
  CONSUMER_PRINT("Waiting until producer is connected...\n");
  if (iStream->waitUntilConnected() != STATUS_OK)
    ORIGINATE_ERROR("Stream failed to connect.");
  CONSUMER_PRINT("Producer has connected; continuing.\n");

  while (true) {
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
    IArgusCaptureMetadata *iArgusCaptureMetadata =
        interface_cast<IArgusCaptureMetadata>(frame);
    if (!iArgusCaptureMetadata)
      ORIGINATE_ERROR("Failed to get IArgusCaptureMetadata interface.");
    CaptureMetadata *metadata = iArgusCaptureMetadata->getMetadata();
    ICaptureMetadata *iMetadata = interface_cast<ICaptureMetadata>(metadata);
    if (!iMetadata)
      ORIGINATE_ERROR("Failed to get ICaptureMetadata interface.");
    CONSUMER_PRINT(
        "\tSensor Timestamp: %llu, LUX: %f\n",
        static_cast<unsigned long long>(iMetadata->getSensorTimestamp()),
        iMetadata->getSceneLux());

    // Print out image details, and map the buffers to read out some data.
    Image *image = iFrame->getImage();
    IImage *iImage = interface_cast<IImage>(image);
    IImage2D *iImage2D = interface_cast<IImage2D>(image);
    for (uint32_t i = 0; i < iImage->getBufferCount(); i++) {
      const uint8_t *d = static_cast<const uint8_t *>(iImage->mapBuffer(i));
      if (!d)
        ORIGINATE_ERROR("\tFailed to map buffer\n");

      Size2D<uint32_t> size = iImage2D->getSize(i);
      CONSUMER_PRINT(
          "\tIImage(2D): "
          "buffer %u (%ux%u, %u stride), "
          "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
          i, size.width(), size.height(), iImage2D->getStride(i), d[0], d[1],
          d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11]);
    }

    // Write raw bayer data
    EGLStream::IImageHeaderlessFile *iBayerWithIspImageHeaderlessFile =
        Argus::interface_cast<EGLStream::IImageHeaderlessFile>(image);
    if (!iBayerWithIspImageHeaderlessFile)
      ORIGINATE_ERROR("Failed to get RGBA IImageHeaderlessFile");

    /* Method to write image data to disk with no encoding, and no header.
     * All pixels are written to file in buffer, row, column order, with
     * multi-byte pixels stored little-endian.
     */
    Argus::Status status =
        iBayerWithIspImageHeaderlessFile->writeHeaderlessFile(
            bayerWithIspOutputFileName);
    if (status != Argus::STATUS_OK)
      ORIGINATE_ERROR("Failed to write output file");
    printf("Wrote bayerWithIsp file : %s\n", bayerWithIspOutputFileName);
  }

  CONSUMER_PRINT("Done.\n");

  PROPAGATE_ERROR(requestShutdown());

  return true;
}

bool ConsumerThread::threadShutdown() { return true; }

static bool execute(const CommonOptions &options) {
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
  std::vector<CameraDevice *> cameraDevices;
  iCameraProvider->getCameraDevices(&cameraDevices);
  if (cameraDevices.size() < 1) {
    ORIGINATE_ERROR("Insufficient camera devices.");
  }

  /****** Bayer capture with ISP enabled ******/
  Argus::CameraDevice *bayerWithIspDevice = cameraDevices[CAMERA_INDEX];

  ICameraProperties *iBayerWithIspProperties =
      interface_cast<ICameraProperties>(bayerWithIspDevice);
  if (!iBayerWithIspProperties) {
    ORIGINATE_ERROR(
        "Failed to get the JPEG camera device properties interface.");
  }

  std::vector<Argus::SensorMode *> bayerWithIspSensorModes;
  // Get camera sensor and corresponding information. Sensor modes include
  // Depth, RGB, YUV and Bayer types.
  iBayerWithIspProperties->getAllSensorModes(&bayerWithIspSensorModes);
  if (!bayerWithIspSensorModes.size()) {
    ORIGINATE_ERROR("Failed to get valid JPEG sensor mode list.");
  }
  ISensorMode *iBayerWithIspSensorMode =
      interface_cast<ISensorMode>(bayerWithIspSensorModes[0]);
  if (!iBayerWithIspSensorMode)
    ORIGINATE_ERROR("Failed to get the sensor mode.");

  // Create the JPEG capture session.
  UniqueObj<CaptureSession> bayerWithIspSession = UniqueObj<CaptureSession>(
      iCameraProvider->createCaptureSession(bayerWithIspDevice));
  if (!bayerWithIspSession)
    ORIGINATE_ERROR("Failed to create JPEG session with camera index %d.",
                    CAMERA_INDEX);
  ICaptureSession *iBayerWithIspCaptureSession =
      interface_cast<ICaptureSession>(bayerWithIspSession);
  if (!iBayerWithIspCaptureSession)
    ORIGINATE_ERROR("Failed to get JPEG capture session interface");

  // Create JPEG stream.
  PRODUCER_PRINT("Creating the JPEG stream.\n");
  UniqueObj<OutputStreamSettings> bayerWithIspSettings(
      iBayerWithIspCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
  IEGLOutputStreamSettings *iBayerWithIspSettings =
      interface_cast<IEGLOutputStreamSettings>(bayerWithIspSettings);
  if (iBayerWithIspSettings) {
    iBayerWithIspSettings->setPixelFormat(PIXEL_FMT_RAW16);
    iBayerWithIspSettings->setResolution(
        iBayerWithIspSensorMode->getResolution());
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

  // Enables the specified output stream. Captures made with this Request will
  // produce output on that stream.
  iRequest->enableOutputStream(bayerWithIspStream.get());
  iRequest->setEnableIspStage(true);

  /*
   * Sets whether or not post-processing is enabled for this stream.
   * Post-processing features are controlled on a per-Request basis and all
   * streams share the same post-processing control values, but this enable
   * allows certain streams to be excluded from all post-processing. The current
   * controls defined to be a part of "post-processing" includes (but may not be
   * limited to):
   *   - Denoise
   * Default value is true.
   */
  IStreamSettings *iStreamSettings = interface_cast<IStreamSettings>(
      iRequest->getStreamSettings(bayerWithIspStream.get()));
  iStreamSettings->setPostProcessingEnable(true);

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

  return true;
}

}; // namespace ArgusSamples

int main(int argc, char **argv) {
  // Sets command line options when running this program
  ArgusSamples::CommonOptions options(
      basename(argv[0]), ArgusSamples::CommonOptions::Option_D_CameraDevice |
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
