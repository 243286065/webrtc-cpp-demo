#include "examples/peerconnection_demo/peerconnection_impl.h"

#include "rtc_base/logging.h"
#include <thread>
#include <iostream>

int main() {
  std::unique_ptr<webrtc_demo::PeerConnectionImpl> peer_connetion = std::make_unique<webrtc_demo::PeerConnectionImpl>();
  peer_connetion->Init();

  // std::this_thread::sleep_for(std::chrono::seconds(30));
  char input;
  std::cin >> input;
  RTC_LOG(WARNING) << "Demo exit";
  return 0;
}