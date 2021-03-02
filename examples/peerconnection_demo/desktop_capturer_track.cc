#include "examples/peerconnection_demo/desktop_capturer_track.h"

#include "modules/desktop_capture/desktop_capture_options.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv.h"

namespace webrtc_demo {

DesktopCapturerTack::DesktopCapturerTack()
    : webrtc::VideoTrackSource(false), dc_(nullptr), start_flag_(false) {}

DesktopCapturerTack::~DesktopCapturerTack() {
  Destory();
}

void DesktopCapturerTack::Destory() {
  StopCapture();

  if (!dc_)
    return;

  dc_.reset(nullptr);
}

rtc::scoped_refptr<DesktopCapturerTack> DesktopCapturerTack::Create(
    size_t target_fps,
    size_t capture_screen_index) {
  auto dc = new rtc::RefCountedObject<DesktopCapturerTack>();
  if (!dc->Init(target_fps, capture_screen_index)) {
    RTC_LOG(LS_WARNING) << "Failed to create DesktopCapture(fps = "
                        << target_fps << ")";
    return nullptr;
  }

  return dc;
}

bool DesktopCapturerTack::Init(size_t target_fps, size_t capture_screen_index) {
  dc_ = webrtc::DesktopCapturer::CreateScreenCapturer(
      webrtc::DesktopCaptureOptions::CreateDefault());

  if (!dc_)
    return false;

  webrtc::DesktopCapturer::SourceList sources;
  dc_->GetSourceList(&sources);
  if (capture_screen_index > sources.size()) {
    RTC_LOG(LS_WARNING) << "The total sources of screen is " << sources.size()
                        << ", but require source of index at "
                        << capture_screen_index;
    return false;
  }

  RTC_CHECK(dc_->SelectSource(sources[capture_screen_index].id));
  window_title_ = sources[capture_screen_index].title;
  fps_ = target_fps;

  RTC_LOG(LS_INFO) << "Init DesktopCapture finish";
  // Start new thread to capture
  return true;
}

void DesktopCapturerTack::OnCaptureResult(
    webrtc::DesktopCapturer::Result result,
    std::unique_ptr<webrtc::DesktopFrame> frame) {
  static auto timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  static size_t cnt = 0;

  cnt++;
  auto timestamp_curr = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
  if (timestamp_curr - timestamp > 1000) {
    RTC_LOG(LS_INFO) << "FPS: " << cnt;
    cnt = 0;
    timestamp = timestamp_curr;
  }

  // Convert DesktopFrame to VideoFrame
  if (result != webrtc::DesktopCapturer::Result::SUCCESS) {
    RTC_LOG(LS_ERROR) << "Capture frame faiiled, result: " << result;
  }
  int width = frame->size().width();
  int height = frame->size().height();
  // int half_width = (width + 1) / 2;

  if (!i420_buffer_.get() ||
      i420_buffer_->width() * i420_buffer_->height() < width * height) {
    i420_buffer_ = webrtc::I420Buffer::Create(width, height);
  }

  libyuv::ConvertToI420(frame->data(), 0, i420_buffer_->MutableDataY(),
                        i420_buffer_->StrideY(), i420_buffer_->MutableDataU(),
                        i420_buffer_->StrideU(), i420_buffer_->MutableDataV(),
                        i420_buffer_->StrideV(), 0, 0, width, height, width,
                        height, libyuv::kRotate0, libyuv::FOURCC_ARGB);

  webrtc::VideoFrame captureFrame =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(i420_buffer_)
          .set_timestamp_rtp(0)
          .set_timestamp_ms(rtc::TimeMillis())
          .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
          .build();

  // Act as source to notify all sinkes.
  this->OnFrame(captureFrame);
}

void DesktopCapturerTack::StartCapture() {
  if (start_flag_) {
    RTC_LOG(WARNING) << "Capture already been running...";
    return;
  }

  start_flag_ = true;

  // Start new thread to capture
  capture_thread_.reset(new std::thread([this]() {
    dc_->Start(this);

    while (start_flag_) {
      dc_->CaptureFrame();
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps_));
    }
  }));
}

void DesktopCapturerTack::StopCapture() {
  start_flag_ = false;

  if (capture_thread_ && capture_thread_->joinable()) {
    capture_thread_->join();
  }
}

void DesktopCapturerTack::UpdateVideoAdapter() {
  video_adapter_.OnSinkWants(broadcaster_.wants());
}

void DesktopCapturerTack::AddOrUpdateSink(
    rtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
    const rtc::VideoSinkWants& wants) {
  broadcaster_.AddOrUpdateSink(sink, wants);
  UpdateVideoAdapter();
}

void DesktopCapturerTack::RemoveSink(
    rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
  broadcaster_.RemoveSink(sink);
  UpdateVideoAdapter();
}

void DesktopCapturerTack::OnFrame(const webrtc::VideoFrame& frame) {
  int cropped_width = 0;
  int cropped_height = 0;
  int out_width = 0;
  int out_height = 0;

  if (!video_adapter_.AdaptFrameResolution(
          frame.width(), frame.height(), frame.timestamp_us() * 1000,
          &cropped_width, &cropped_height, &out_width, &out_height)) {
    // Drop frame in order to respect frame rate constraint.
    return;
  }

  if (out_height != frame.height() || out_width != frame.width()) {
    // Video adapter has requested a down-scale. Allocate a new buffer and
    // return scaled version.
    // For simplicity, only scale here without cropping.
    rtc::scoped_refptr<webrtc::I420Buffer> scaled_buffer =
        webrtc::I420Buffer::Create(out_width, out_height);
    scaled_buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
    webrtc::VideoFrame::Builder new_frame_builder =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(scaled_buffer)
            .set_rotation(webrtc::kVideoRotation_0)
            .set_timestamp_us(frame.timestamp_us())
            .set_id(frame.id());
    ;
    if (frame.has_update_rect()) {
      webrtc::VideoFrame::UpdateRect new_rect =
          frame.update_rect().ScaleWithFrame(frame.width(), frame.height(), 0,
                                             0, frame.width(), frame.height(),
                                             out_width, out_height);
      new_frame_builder.set_update_rect(new_rect);
    }
    broadcaster_.OnFrame(new_frame_builder.build());
  } else {
    // No adaptations needed, just return the frame as is.
    broadcaster_.OnFrame(frame);
  }
}

}  // namespace webrtc_demo
