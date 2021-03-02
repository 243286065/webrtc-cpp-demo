#ifndef EXAMPLES_PEERCONNECTION_DEMO_PEERCONNECTION_IMPL_H_
#define EXAMPLES_PEERCONNECTION_DEMO_PEERCONNECTION_IMPL_H_

#include "api/peer_connection_interface.h"

#include "examples/peerconnection_demo/desktop_capturer_track.h"


namespace webrtc_demo
{
class PeerConnectionImpl : public webrtc::PeerConnectionObserver {
public:
  bool Init(const size_t fps = 15, const size_t screen_index = 0);

protected:
  //
  // PeerConnectionObserver implementation.
  //
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

private:
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  rtc::scoped_refptr<webrtc_demo::DesktopCapturerTack> desktop_capturer_;
};

  
} // namespace webrtc_demo


#endif  // EXAMPLES_PEERCONNECTION_DEMO_PEERCONNECTION_IMPL_H_
