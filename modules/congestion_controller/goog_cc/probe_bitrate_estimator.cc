/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/goog_cc/probe_bitrate_estimator.h"

#include <algorithm>
#include <memory>

#include "api/rtc_event_log/rtc_event_log.h"
#include "logging/rtc_event_log/events/rtc_event_probe_result_failure.h"
#include "logging/rtc_event_log/events/rtc_event_probe_result_success.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"

namespace webrtc {
namespace {
// The minumum number of probes we need to receive feedback about in percent
// in order to have a valid estimate.
constexpr double kMinReceivedProbesRatio = .80;

// The minumum number of bytes we need to receive feedback about in percent
// in order to have a valid estimate.
constexpr double kMinReceivedBytesRatio = .80;

// The maximum |receive rate| / |send rate| ratio for a valid estimate.
constexpr float kMaxValidRatio = 2.0f;

// The minimum |receive rate| / |send rate| ratio assuming that the link is
// not saturated, i.e. we assume that we will receive at least
// kMinRatioForUnsaturatedLink * |send rate| if |send rate| is less than the
// link capacity.
constexpr float kMinRatioForUnsaturatedLink = 0.9f;

// The target utilization of the link. If we know true link capacity
// we'd like to send at 95% of that rate.
constexpr float kTargetUtilizationFraction = 0.95f;

// The maximum time period over which the cluster history is retained.
// This is also the maximum time period beyond which a probing burst is not
// expected to last.
constexpr TimeDelta kMaxClusterHistory = TimeDelta::Seconds(1);

// The maximum time interval between first and the last probe on a cluster
// on the sender side as well as the receive side.
constexpr TimeDelta kMaxProbeInterval = TimeDelta::Seconds(1);

}  // namespace

ProbeBitrateEstimator::ProbeBitrateEstimator(RtcEventLog* event_log)
    : event_log_(event_log) {}

ProbeBitrateEstimator::~ProbeBitrateEstimator() = default;

absl::optional<DataRate> ProbeBitrateEstimator::HandleProbeAndEstimateBitrate(
    const PacketResult& packet_feedback) {
  int cluster_id = packet_feedback.sent_packet.pacing_info.probe_cluster_id;
  RTC_DCHECK_NE(cluster_id, PacedPacketInfo::kNotAProbe);

  //移除旧数据(保留过去1s(kMaxClusterHistory)内的数据)
  EraseOldClusters(packet_feedback.receive_time);

  AggregatedCluster* cluster = &clusters_[cluster_id];

  //更新发送的第一个包的时间戳
  if (packet_feedback.sent_packet.send_time < cluster->first_send) {
    cluster->first_send = packet_feedback.sent_packet.send_time;
  }
  //更新最后发送的包的时间戳及大小
  if (packet_feedback.sent_packet.send_time > cluster->last_send) {
    cluster->last_send = packet_feedback.sent_packet.send_time;
    cluster->size_last_send = packet_feedback.sent_packet.size;
  }
  //更新第一个包的接收时间及大小
  if (packet_feedback.receive_time < cluster->first_receive) {
    cluster->first_receive = packet_feedback.receive_time;
    cluster->size_first_receive = packet_feedback.sent_packet.size;
  }
  //更新最后接收到包的时间
  if (packet_feedback.receive_time > cluster->last_receive) {
    cluster->last_receive = packet_feedback.receive_time;
  }
  //更新一个cluster的包数量和大小
  cluster->size_total += packet_feedback.sent_packet.size;
  cluster->num_probes += 1;

  RTC_DCHECK_GT(
      packet_feedback.sent_packet.pacing_info.probe_cluster_min_probes, 0);
  RTC_DCHECK_GT(packet_feedback.sent_packet.pacing_info.probe_cluster_min_bytes,
                0);

  //探测包收到的最小数量 = probe_cluster_min_probes * 0.8
  int min_probes =
      packet_feedback.sent_packet.pacing_info.probe_cluster_min_probes *
      kMinReceivedProbesRatio;
  
  //收到的探测报最小的大小 = probe_cluster_min_bytes * 0.8
  DataSize min_size =
      DataSize::Bytes(
          packet_feedback.sent_packet.pacing_info.probe_cluster_min_bytes) *
      kMinReceivedBytesRatio;
  //尚未收到足够数量或者大小的包，直接返回
  if (cluster->num_probes < min_probes || cluster->size_total < min_size)
    return absl::nullopt;

  //发送持续时间
  TimeDelta send_interval = cluster->last_send - cluster->first_send;
  //接受持续时间
  TimeDelta receive_interval = cluster->last_receive - cluster->first_receive;

  //数据异常，或者发送间隔超过1s，或者接收时间间隔超过1s，表示探测失败
  if (send_interval <= TimeDelta::Zero() || send_interval > kMaxProbeInterval ||
      receive_interval <= TimeDelta::Zero() ||
      receive_interval > kMaxProbeInterval) {
    RTC_LOG(LS_INFO) << "Probing unsuccessful, invalid send/receive interval"
                        " [cluster id: "
                     << cluster_id
                     << "] [send interval: " << ToString(send_interval)
                     << "]"
                        " [receive interval: "
                     << ToString(receive_interval) << "]";
    if (event_log_) {
      event_log_->Log(std::make_unique<RtcEventProbeResultFailure>(
          cluster_id, ProbeFailureReason::kInvalidSendReceiveInterval));
    }
    return absl::nullopt;
  }

  //估算发送速率
  // Since the |send_interval| does not include the time it takes to actually
  // send the last packet the size of the last sent packet should not be
  // included when calculating the send bitrate.
  RTC_DCHECK_GT(cluster->size_total, cluster->size_last_send);
  //发送的数据大小，不包含最后一个包
  DataSize send_size = cluster->size_total - cluster->size_last_send;
  //发送的速率
  DataRate send_rate = send_size / send_interval;

  //估算接速率
  // Since the |receive_interval| does not include the time it takes to
  // actually receive the first packet the size of the first received packet
  // should not be included when calculating the receive bitrate.
  RTC_DCHECK_GT(cluster->size_total, cluster->size_first_receive);
  //接收的数据大小(不包含第一个包的大小)
  DataSize receive_size = cluster->size_total - cluster->size_first_receive;
  //接收速率
  DataRate receive_rate = receive_size / receive_interval;

  //接收速率和发送速率之比
  double ratio = receive_rate / send_rate;
  
  //当接收速率/发送速率 > 2,比例过高，也是异常数据，返回空
  if (ratio > kMaxValidRatio) {
    RTC_LOG(LS_INFO) << "Probing unsuccessful, receive/send ratio too high"
                        " [cluster id: "
                     << cluster_id << "] [send: " << ToString(send_size)
                     << " / " << ToString(send_interval) << " = "
                     << ToString(send_rate)
                     << "]"
                        " [receive: "
                     << ToString(receive_size) << " / "
                     << ToString(receive_interval) << " = "
                     << ToString(receive_rate)
                     << " ]"
                        " [ratio: "
                     << ToString(receive_rate) << " / " << ToString(send_rate)
                     << " = " << ratio << " > kMaxValidRatio ("
                     << kMaxValidRatio << ")]";
    if (event_log_) {
      event_log_->Log(std::make_unique<RtcEventProbeResultFailure>(
          cluster_id, ProbeFailureReason::kInvalidSendReceiveRatio));
    }
    return absl::nullopt;
  }

  // 探测成功的情况
  RTC_LOG(LS_INFO) << "Probing successful"
                      " [cluster id: "
                   << cluster_id << "] [send: " << ToString(send_size) << " / "
                   << ToString(send_interval) << " = " << ToString(send_rate)
                   << " ]"
                      " [receive: "
                   << ToString(receive_size) << " / "
                   << ToString(receive_interval) << " = "
                   << ToString(receive_rate) << "]";

  // 预测结果就是发送速率和接收速率的最小值
  DataRate res = std::min(send_rate, receive_rate);
  // If we're receiving at significantly lower bitrate than we were sending at,
  // it suggests that we've found the true capacity of the link. In this case,
  // set the target bitrate slightly lower to not immediately overuse.
  //如果 接收速率<0.9*发送速率， 代表我们已经找到了带宽的大小，取这个值的稍微小点(0.95)修正
  if (receive_rate < kMinRatioForUnsaturatedLink * send_rate) {
    RTC_DCHECK_GT(send_rate, receive_rate);
    res = kTargetUtilizationFraction * receive_rate;
  }
  if (event_log_) {
    event_log_->Log(
        std::make_unique<RtcEventProbeResultSuccess>(cluster_id, res.bps()));
  }
  estimated_data_rate_ = res;
  return estimated_data_rate_;
}

absl::optional<DataRate>
ProbeBitrateEstimator::FetchAndResetLastEstimatedBitrate() {
  absl::optional<DataRate> estimated_data_rate = estimated_data_rate_;
  estimated_data_rate_.reset();
  return estimated_data_rate;
}

void ProbeBitrateEstimator::EraseOldClusters(Timestamp timestamp) {
  for (auto it = clusters_.begin(); it != clusters_.end();) {
    if (it->second.last_receive + kMaxClusterHistory < timestamp) {
      it = clusters_.erase(it);
    } else {
      ++it;
    }
  }
}
}  // namespace webrtc
