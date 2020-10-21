#ifndef EXAMPLES_VIDEO_CAPTURE_VIDEO_CPTURE_TEST_H_
#define EXAMPLES_VIDEO_CAPTURE_VIDEO_CPTURE_TEST_H_

#include <memory>
#include <mutex>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_source_interface.h"
#include "media/base/video_adapter.h"
#include "media/base/video_broadcaster.h"
#include "modules/video_capture/video_capture.h"

namespace webrtc_demo {

class VideoCapturerTest : public rtc::VideoSourceInterface<webrtc::VideoFrame> {
 public:
  class FramePreprocessor {
   public:
    virtual ~FramePreprocessor() = default;
    virtual webrtc::VideoFrame Preprocess(const webrtc::VideoFrame& frame) = 0;
  };

 public:
  ~VideoCapturerTest() override;

  void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                       const rtc::VideoSinkWants& wants) override;

  void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

  void SetFramePreprocessor(std::unique_ptr<FramePreprocessor> preprocessor) {
    std::lock_guard<std::mutex> lock(mutex_);
    preprocessor_ = std::move(preprocessor);
  }

 protected:
  void OnFrame(const webrtc::VideoFrame& frame);
  rtc::VideoSinkWants GetSinkWants();

 private:
  void UpdateVideoAdapter();
  webrtc::VideoFrame MaybePreprocess(const webrtc::VideoFrame& frame);

 private:
  std::unique_ptr<FramePreprocessor> preprocessor_;
  std::mutex mutex_;
  rtc::VideoBroadcaster broadcaster_;
  cricket::VideoAdapter video_adapter_;
};

}  // namespace webrtc_demo

#endif  // EXAMPLES_VIDEO_CAPTURE_VIDEO_CPTURE_TEST_H_