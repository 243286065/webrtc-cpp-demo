#ifndef EXAMPLES_DESKTOP_CAPTURE_VIDEO_ENCODE_HANDLER_H_
#define EXAMPLES_DESKTOP_CAPTURE_VIDEO_ENCODE_HANDLER_H_

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video/encoded_image.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/include/module_common_types.h"

namespace webrtc_demo {

class VideoEncodeHandler : public rtc::VideoSinkInterface<webrtc::VideoFrame>,
                           public webrtc::EncodedImageCallback {
 public:
  enum class VideoEncodeType {
    VP8,
    VP9,
    H264,
    UNSUPPORT_TYPE,
  };

  ~VideoEncodeHandler();

  static std::unique_ptr<VideoEncodeHandler> Create(const VideoEncodeType type);

 private:
  VideoEncodeHandler(const VideoEncodeType type);

  // rtc::VideoSinkInterface override
  void OnFrame(const webrtc::VideoFrame& frame) override;

  // webrtc::EncodedImageCallback override
  webrtc::EncodedImageCallback::Result OnEncodedImage(
      const webrtc::EncodedImage& encoded_image,
      const webrtc::CodecSpecificInfo* codec_specific_info,
      const webrtc::RTPFragmentationHeader* fragmentation) override;
  void OnDroppedFrame(webrtc::EncodedImageCallback::DropReason reason) override;

  webrtc::VideoCodec DefaultCodecSettings(size_t width, size_t height, size_t keyFrameInterval);

  void ReInitEncoder();

  std::unique_ptr<webrtc::VideoEncoder> video_encoder_;
  std::string encode_type_name_;
  size_t frame_width_ = 0;
  size_t frame_height_ = 0;
};

}  // namespace webrtc_demo

#endif  // EXAMPLES_DESKTOP_CAPTURE_VIDEO_ENCODE_HANDLER_H_