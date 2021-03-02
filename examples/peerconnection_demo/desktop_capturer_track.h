#ifndef EXAMPLES_PEERCONNECTION_DEMO_DESKTOP_CAPTURER_TRACK_H_
#define EXAMPLES_PEERCONNECTION_DEMO_DESKTOP_CAPTURER_TRACK_H_

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "pc/video_track_source.h"
#include "media/base/video_adapter.h"
#include "media/base/video_broadcaster.h"

#include <atomic>
#include <thread>

namespace webrtc_demo {

class DesktopCapturerTack : public webrtc::DesktopCapturer::Callback,
                            public webrtc::VideoTrackSource {
 public:
  static rtc::scoped_refptr<DesktopCapturerTack> Create(
      size_t target_fps,
      size_t capture_screen_index);

  ~DesktopCapturerTack() override;

  std::string GetWindowTitle() const { return window_title_; }

  void StartCapture();
  void StopCapture();

  void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                       const rtc::VideoSinkWants& wants) override;

  void RemoveSink(rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

 protected:
  rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return this;
  }

  explicit DesktopCapturerTack();

 private:
  void Destory();

  bool Init(size_t target_fps, size_t capture_screen_index);

  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override;

  void UpdateVideoAdapter();

  // Notify sinkes
  void OnFrame(const webrtc::VideoFrame& frame);

  std::unique_ptr<webrtc::DesktopCapturer> dc_;

  size_t fps_;
  std::string window_title_;

  std::unique_ptr<std::thread> capture_thread_;
  std::atomic_bool start_flag_;

  rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer_;

  rtc::VideoBroadcaster broadcaster_;
  cricket::VideoAdapter video_adapter_;
};
}  // namespace webrtc_demo

#endif  // EXAMPLES_PEERCONNECTION_DEMO_DESKTOP_CAPTURER_TRACK_H_
