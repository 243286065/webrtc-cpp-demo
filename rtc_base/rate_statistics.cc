/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/rate_statistics.h"

#include <algorithm>
#include <limits>
#include <memory>

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"

namespace webrtc {

RateStatistics::Bucket::Bucket(int64_t timestamp)
    : sum(0), num_samples(0), timestamp(timestamp) {}

RateStatistics::RateStatistics(int64_t window_size_ms, float scale)
    : accumulated_count_(0),
      first_timestamp_(-1),
      num_samples_(0),
      scale_(scale),
      max_window_size_ms_(window_size_ms),
      current_window_size_ms_(max_window_size_ms_) {}

RateStatistics::RateStatistics(const RateStatistics& other)
    : buckets_(other.buckets_),
      accumulated_count_(other.accumulated_count_),
      first_timestamp_(other.first_timestamp_),
      overflow_(other.overflow_),
      num_samples_(other.num_samples_),
      scale_(other.scale_),
      max_window_size_ms_(other.max_window_size_ms_),
      current_window_size_ms_(other.current_window_size_ms_) {}

RateStatistics::RateStatistics(RateStatistics&& other) = default;

RateStatistics::~RateStatistics() {}

void RateStatistics::Reset() {
  accumulated_count_ = 0;
  overflow_ = false;
  num_samples_ = 0;
  first_timestamp_ = -1;
  current_window_size_ms_ = max_window_size_ms_;
  buckets_.clear();
}

void RateStatistics::Update(int64_t count, int64_t now_ms) {
  RTC_DCHECK_GE(count, 0);

  //移除超过时间窗口的桶
  EraseOld(now_ms);
  if (first_timestamp_ == -1) {
    first_timestamp_ = now_ms;
  }

  if (buckets_.empty() || now_ms != buckets_.back().timestamp) {
    //当前更新时间比上次更新时间还早，属于异常情况，更换now_ms
    if (!buckets_.empty() && now_ms < buckets_.back().timestamp) {
      RTC_LOG(LS_WARNING) << "Timestamp " << now_ms
                          << " is before the last added "
                             "timestamp in the rate window: "
                          << buckets_.back().timestamp << ", aligning to that.";
      now_ms = buckets_.back().timestamp;
    }
    //没有现有的桶，或者当前毫秒没有记录时，创建对应当前时间的桶
    buckets_.emplace_back(now_ms);
  }
  Bucket& last_bucket = buckets_.back();
  //更新对应时间的桶中的统计数据
  last_bucket.sum += count;
  ++last_bucket.num_samples;

  //防止accumulated_count_溢出
  if (std::numeric_limits<int64_t>::max() - accumulated_count_ > count) {
    accumulated_count_ += count;
  } else {
    overflow_ = true;
  }
  ++num_samples_;
}

absl::optional<int64_t> RateStatistics::Rate(int64_t now_ms) const {
  // Yeah, this const_cast ain't pretty, but the alternative is to declare most
  // of the members as mutable...
  // 移除旧的桶
  const_cast<RateStatistics*>(this)->EraseOld(now_ms);

  int active_window_size = 0;
  if (first_timestamp_ != -1) {
    //距离第一次的记录的时间间隔超过了当前的时间窗口，那么计算的周期就选当前的窗口
    if (first_timestamp_ <= now_ms - current_window_size_ms_) {
      // Count window as full even if no data points currently in view, if the
      // data stream started before the window.
      active_window_size = current_window_size_ms_;
    } else {
      // 当前的记录不足当前时间窗口，那么按照实际的时间计算时间周期
      // Size of a single bucket is 1ms, so even if now_ms == first_timestmap_
      // the window size should be 1.
      active_window_size = now_ms - first_timestamp_ + 1;
    }
  }

  // If window is a single bucket or there is only one sample in a data set that
  // has not grown to the full window size, or if the accumulator has
  // overflowed, treat this as rate unavailable.
  // 当还没有sample，或者有效时间周期小于等于1，或者只有一条sample并且尚未经过一个完整的时间窗口，或者已经统计溢出，都任务非法，返回null
  if (num_samples_ == 0 || active_window_size <= 1 ||
      (num_samples_ <= 1 &&
       rtc::SafeLt(active_window_size, current_window_size_ms_)) ||
      overflow_) {
    return absl::nullopt;
  }

  // 使用 ((当前窗口内的所有计数/有效时间窗口) *  scale_ + 0.5) 作为速率返回
  float scale = static_cast<float>(scale_) / active_window_size;
  float result = accumulated_count_ * scale + 0.5f;

  // Better return unavailable rate than garbage value (undefined behavior).
  if (result > static_cast<float>(std::numeric_limits<int64_t>::max())) {
    return absl::nullopt;
  }
  return rtc::dchecked_cast<int64_t>(result);
}

// 移除不在时间窗口内的桶
void RateStatistics::EraseOld(int64_t now_ms) {
  // New oldest time that is included in data set.
  const int64_t new_oldest_time = now_ms - current_window_size_ms_ + 1;

  // Loop over buckets and remove too old data points.
  while (!buckets_.empty() && buckets_.front().timestamp < new_oldest_time) {
    const Bucket& oldest_bucket = buckets_.front();
    RTC_DCHECK_GE(accumulated_count_, oldest_bucket.sum);
    RTC_DCHECK_GE(num_samples_, oldest_bucket.num_samples);
    accumulated_count_ -= oldest_bucket.sum;
    num_samples_ -= oldest_bucket.num_samples;
    buckets_.pop_front();
    // This does not clear overflow_ even when counter is empty.
    // TODO(https://bugs.webrtc.org/11247): Consider if overflow_ can be reset.
  }
}

bool RateStatistics::SetWindowSize(int64_t window_size_ms, int64_t now_ms) {
  if (window_size_ms <= 0 || window_size_ms > max_window_size_ms_)
    return false;
  if (first_timestamp_ != -1) {
    // If the window changes (e.g. decreases - removing data point, then
    // increases again) we need to update the first timestamp mark as
    // otherwise it indicates the window coveres a region of zeros, suddenly
    // under-estimating the rate.
    first_timestamp_ = std::max(first_timestamp_, now_ms - window_size_ms + 1);
  }
  current_window_size_ms_ = window_size_ms;
  EraseOld(now_ms);
  return true;
}

}  // namespace webrtc
