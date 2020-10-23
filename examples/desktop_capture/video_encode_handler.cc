#include "examples/desktop_capture/video_encode_handler.h"

#include "api/video/video_codec_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "media/engine/internal_encoder_factory.h"
#include "rtc_base/logging.h"
#include "test/video_codec_settings.h"

namespace webrtc_demo {

constexpr size_t kWidth = 1920;
constexpr size_t kHeight = 1080;
constexpr size_t kBaseKeyFrameInterval = 30;

const webrtc::VideoEncoder::Capabilities kCapabilities(false);
const webrtc::VideoEncoder::Settings kSettings(kCapabilities,
                                               /*number_of_cores=*/1,
                                               /*max_payload_size=*/0);

VideoEncodeHandler::VideoEncodeHandler(const VideoEncodeType type)
    : video_encoder_(nullptr), frame_width_(kWidth), frame_height_(kHeight) {
  switch (type) {
    case VideoEncodeType::VP8:
      encode_type_name_ = "VP8";
      break;
    case VideoEncodeType::VP9:
      encode_type_name_ = "VP9";
      break;
    case VideoEncodeType::H264:
      encode_type_name_ = "H264";
      break;
    default:
      break;
  }

  auto support_formats = webrtc::InternalEncoderFactory::SupportedFormats();
  for (auto& format : support_formats) {
    RTC_LOG(LS_INFO) << "Support encode: " << format.ToString();
    if (!video_encoder_ && format.name == encode_type_name_) {
      RTC_LOG(LS_INFO) << "Find encode: " << format.name;
      std::unique_ptr<webrtc::InternalEncoderFactory> encode_factory =
          std::make_unique<webrtc::InternalEncoderFactory>();
      video_encoder_ = encode_factory->CreateVideoEncoder(format);
      video_encoder_->RegisterEncodeCompleteCallback(this);

      ReInitEncoder();
    }
  }
}

void VideoEncodeHandler::ReInitEncoder() {
  webrtc::VideoCodec codec_settings =
      DefaultCodecSettings(frame_width_, frame_height_, kBaseKeyFrameInterval);
  video_encoder_->InitEncode(&codec_settings, kSettings);
}

VideoEncodeHandler::~VideoEncodeHandler() {}

std::unique_ptr<VideoEncodeHandler> VideoEncodeHandler::Create(
    const VideoEncodeType type) {
  if (type >= VideoEncodeType::UNSUPPORT_TYPE) {
    RTC_LOG(LS_WARNING) << "Not support encode type: " << type;
    return nullptr;
  }

  return std::unique_ptr<VideoEncodeHandler>(new VideoEncodeHandler(type));
}

void VideoEncodeHandler::OnFrame(const webrtc::VideoFrame& frame) {
  // RTC_LOG(LS_INFO) << "-----VideoEncodeHandler::OnFrame-----";
  if (!video_encoder_) {
    RTC_LOG(LS_ERROR) << "Encoder not valid";
    return;
  }

  if (frame.width() != (int)frame_width_ || frame.height() != (int)frame_height_) {
    frame_width_ = frame.width();
    frame_height_ = frame.height();
    ReInitEncoder();
  }

  if (video_encoder_) {
    video_encoder_->Encode(frame, nullptr);
  }
}

webrtc::EncodedImageCallback::Result VideoEncodeHandler::OnEncodedImage(
    const webrtc::EncodedImage& encoded_image,
    const webrtc::CodecSpecificInfo* codec_specific_info,
    const webrtc::RTPFragmentationHeader* fragmentation) {
  RTC_LOG(LS_INFO) << "-----VideoEncodeHandler::OnEncodedImage-----" << encoded_image.size() << ", " << encoded_image.Timestamp() << "--" << encoded_image._frameType;

  return webrtc::EncodedImageCallback::Result(
      webrtc::EncodedImageCallback::Result::OK);
}

void VideoEncodeHandler::OnDroppedFrame(
    webrtc::EncodedImageCallback::DropReason reason) {
  RTC_LOG(LS_INFO) << "-----VideoEncodeHandler::OnDroppedFrame-----";
}

webrtc::VideoCodec VideoEncodeHandler::DefaultCodecSettings(
    size_t width,
    size_t height,
    size_t key_frame_interval) {
  webrtc::VideoCodec codec_settings;
  webrtc::VideoCodecType codec_type =
      webrtc::PayloadStringToCodecType(encode_type_name_);
  webrtc::test::CodecSettings(codec_type, &codec_settings);

  codec_settings.width = static_cast<uint16_t>(width);
  codec_settings.height = static_cast<uint16_t>(height);

  switch (codec_settings.codecType) {
    case webrtc::kVideoCodecVP8:
      codec_settings.VP8()->keyFrameInterval = key_frame_interval;
      codec_settings.VP8()->frameDroppingOn = true;
      codec_settings.VP8()->numberOfTemporalLayers = 1;
      break;
    case webrtc::kVideoCodecVP9:
      codec_settings.VP9()->keyFrameInterval = key_frame_interval;
      codec_settings.VP9()->frameDroppingOn = true;
      codec_settings.VP9()->numberOfTemporalLayers = 1;
      break;
    case webrtc::kVideoCodecAV1:
      codec_settings.qpMax = 63;
      break;
    case webrtc::kVideoCodecH264:
      codec_settings.H264()->keyFrameInterval = key_frame_interval;
      break;
    default:
      break;
  }

  return codec_settings;
}

}  // namespace webrtc_demo
