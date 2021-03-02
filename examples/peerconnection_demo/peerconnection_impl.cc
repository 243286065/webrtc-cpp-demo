#include "examples/peerconnection_demo/peerconnection_impl.h"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "rtc_base/logging.h"

namespace webrtc_demo {

const char kVideoLabel[] = "video_label";
const char kStreamId[] = "stream_id";

std::string GetEnvVarOrDefault(const char* env_var_name,
                               const char* default_value) {
  std::string value;
  const char* env_var = getenv(env_var_name);
  if (env_var)
    value = env_var;

  if (value.empty())
    value = default_value;

  return value;
}

std::string GetPeerConnectionString() {
  return GetEnvVarOrDefault("WEBRTC_CONNECT", "stun:stun.l.google.com:19302");
}

void PeerConnectionImpl::OnAddTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
        streams) {
  RTC_LOG(LS_INFO) << "--------PeerConnectionImpl::OnAddTrack------------";
}

void PeerConnectionImpl::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  RTC_LOG(LS_INFO) << "--------PeerConnectionImpl::OnRemoveTrack------------";
}

void PeerConnectionImpl::OnIceCandidate(
    const webrtc::IceCandidateInterface* candidate) {
  RTC_LOG(LS_INFO) << "--------PeerConnectionImpl::OnIceCandidate------------";
}

bool PeerConnectionImpl::Init(const size_t fps, const size_t screen_index) {
  peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
      nullptr /* network_thread */, nullptr /* worker_thread */,
      nullptr /* signaling_thread */, nullptr /* default_adm */,
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      webrtc::CreateBuiltinVideoEncoderFactory(),
      webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
      nullptr /* audio_processing */);

  if (!peer_connection_factory_) {
    RTC_LOG(LS_ERROR) << "CreatePeerConnectionFactory failed";
    return false;
  }

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  config.enable_dtls_srtp = false;
  webrtc::PeerConnectionInterface::IceServer server;
  server.uri = GetPeerConnectionString();
  config.servers.push_back(server);
  peer_connection_ = peer_connection_factory_->CreatePeerConnection(
      config, nullptr, nullptr, this);
  if (!peer_connection_) {
    RTC_LOG(LS_ERROR) << "CreatePeerConnection failed";
    return false;
  }

  desktop_capturer_ = webrtc_demo::DesktopCapturerTack::Create(fps, screen_index);
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
      peer_connection_factory_->CreateVideoTrack(kVideoLabel,
                                                 desktop_capturer_));

  auto result_or_error = peer_connection_->AddTrack(video_track, {kStreamId});
  if (!result_or_error.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add video track to PeerConnection: "
                      << result_or_error.error().message();
    return false;
  }

  desktop_capturer_->StartCapture();
  return true;
}

}  // namespace webrtc_demo
