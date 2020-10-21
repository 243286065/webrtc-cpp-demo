#ifndef EXAMPLES_VIDEO_CAPTURE_VCM_CAPTURER_TEST_H_
#define EXAMPLES_VIDEO_CAPTURE_VCM_CAPTURER_TEST_H_

#include <memory>

#include "modules/video_capture/video_capture.h"
#include "examples/video_capture/video_capturer_test.h"

namespace webrtc_demo {

class VcmCapturerTest : public VideoCapturerTest,
                        public rtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  static VcmCapturerTest* Create(size_t width,
                                 size_t height,
                                 size_t target_fps,
                                 size_t capture_device_index);

  virtual ~VcmCapturerTest();

  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  VcmCapturerTest();

  bool Init(size_t width,
            size_t height,
            size_t target_fps,
            size_t capture_device_index);

  void Destroy();

  rtc::scoped_refptr<webrtc::VideoCaptureModule> vcm_;
  webrtc::VideoCaptureCapability capability_;
};

}  // namespace webrtc_demo

#endif  // EXAMPLES_VIDEO_CAPTURE_VCM_CAPTURER_TEST_H_