/*
 *  Copyright 2015 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/rate_tracker.h"

#include <algorithm>

#include "rtc_base/checks.h"
#include "rtc_base/time_utils.h"

namespace rtc {

static const int64_t kTimeUnset = -1;

RateTracker::RateTracker(int64_t bucket_milliseconds, size_t bucket_count)
    : bucket_milliseconds_(bucket_milliseconds),
      bucket_count_(bucket_count),
      sample_buckets_(new int64_t[bucket_count + 1]),
      total_sample_count_(0u),
      bucket_start_time_milliseconds_(kTimeUnset) {
  RTC_CHECK(bucket_milliseconds > 0);
  RTC_CHECK(bucket_count > 0);
}

RateTracker::~RateTracker() {
  delete[] sample_buckets_;
}

double RateTracker::ComputeRateForInterval(
    int64_t interval_milliseconds) const {
  if (bucket_start_time_milliseconds_ == kTimeUnset) {
    return 0.0;
  }
  int64_t current_time = Time();
  // Calculate which buckets to sum up given the current time.  If the time
  // has passed to a new bucket then we have to skip some of the oldest buckets.
  // 时间间隔限制在当前所有桶的范围，已经被覆盖的不能包含在内
  int64_t available_interval_milliseconds =
      std::min(interval_milliseconds,
               bucket_milliseconds_ * static_cast<int64_t>(bucket_count_));
  // number of old buckets (i.e. after the current bucket in the ring buffer)
  // that are expired given our current time interval.
  size_t buckets_to_skip;
  // Number of milliseconds of the first bucket that are not a portion of the
  // current interval.
  int64_t milliseconds_to_skip;
  // 判断发生了桶覆盖的情况
  if (current_time >
      initialization_time_milliseconds_ + available_interval_milliseconds) {
    //需要跳过的时间
    int64_t time_to_skip =
        current_time - bucket_start_time_milliseconds_ +
        static_cast<int64_t>(bucket_count_) * bucket_milliseconds_ -
        available_interval_milliseconds;
    //需要跳过的桶数量
    buckets_to_skip = time_to_skip / bucket_milliseconds_;
    //桶内时间偏移
    milliseconds_to_skip = time_to_skip % bucket_milliseconds_;
  } else {
    buckets_to_skip = bucket_count_ - current_bucket_;
    milliseconds_to_skip = 0;
    available_interval_milliseconds =
        TimeDiff(current_time, initialization_time_milliseconds_);
    // 连一个桶的时间都不到
    // Let one bucket interval pass after initialization before reporting.
    if (available_interval_milliseconds < bucket_milliseconds_) {
      return 0.0;
    }
  }
  // If we're skipping all buckets that means that there have been no samples
  // within the sampling interval so report 0.
  if (buckets_to_skip > bucket_count_ || available_interval_milliseconds == 0) {
    return 0.0;
  }
  size_t start_bucket = NextBucketIndex(current_bucket_ + buckets_to_skip);
  // Only count a portion of the first bucket according to how much of the
  // first bucket is within the current interval.
  int64_t total_samples = ((sample_buckets_[start_bucket] *
                            (bucket_milliseconds_ - milliseconds_to_skip)) +
                           (bucket_milliseconds_ >> 1)) /
                          bucket_milliseconds_;
  // All other buckets in the interval are counted in their entirety.
  for (size_t i = NextBucketIndex(start_bucket);
       i != NextBucketIndex(current_bucket_); i = NextBucketIndex(i)) {
    total_samples += sample_buckets_[i];
  }
  // Convert to samples per second.
  return static_cast<double>(total_samples * 1000) /
         static_cast<double>(available_interval_milliseconds);
}

double RateTracker::ComputeTotalRate() const {
  if (bucket_start_time_milliseconds_ == kTimeUnset) {
    return 0.0;
  }
  int64_t current_time = Time();
  if (current_time <= initialization_time_milliseconds_) {
    return 0.0;
  }
  return static_cast<double>(total_sample_count_ * 1000) /
         static_cast<double>(
             TimeDiff(current_time, initialization_time_milliseconds_));
}

int64_t RateTracker::TotalSampleCount() const {
  return total_sample_count_;
}

void RateTracker::AddSamples(int64_t sample_count) {
  RTC_DCHECK_LE(0, sample_count);
  // 确保进行初始化
  EnsureInitialized();
  int64_t current_time = Time();
  // Advance the current bucket as needed for the current time, and reset
  // bucket counts as we advance.
  // 根据当前时间，对比当前桶的起始时间bucket_start_time_milliseconds_，每一个bucket_milliseconds_间隔，就初始化一个桶
  // 在当前桶的时间范围内，就停止
  for (size_t i = 0;
       i <= bucket_count_ &&
       current_time >= bucket_start_time_milliseconds_ + bucket_milliseconds_;
       ++i) {
    bucket_start_time_milliseconds_ += bucket_milliseconds_;
    current_bucket_ = NextBucketIndex(current_bucket_);
    sample_buckets_[current_bucket_] = 0;
  }
  // Ensure that bucket_start_time_milliseconds_ is updated appropriately if
  // the entire buffer of samples has been expired.
  // 确保bucket_start_time_milliseconds_的时间正确（有种情况就是桶的数量已达上限）
  bucket_start_time_milliseconds_ +=
      bucket_milliseconds_ *
      ((current_time - bucket_start_time_milliseconds_) / bucket_milliseconds_);
  // Add all samples in the bucket that includes the current time.
  // 更新当前桶的样本数量和总的样本数量
  sample_buckets_[current_bucket_] += sample_count;
  total_sample_count_ += sample_count;
}

int64_t RateTracker::Time() const {
  return rtc::TimeMillis();
}

// 进行初始化
void RateTracker::EnsureInitialized() {
  if (bucket_start_time_milliseconds_ == kTimeUnset) {
    initialization_time_milliseconds_ = Time();
    bucket_start_time_milliseconds_ = initialization_time_milliseconds_;
    // 当前桶的序号
    current_bucket_ = 0;
    // We only need to initialize the first bucket because we reset buckets when
    // current_bucket_ increments.
    // 初始化第一个桶的数量
    sample_buckets_[current_bucket_] = 0;
  }
}

size_t RateTracker::NextBucketIndex(size_t bucket_index) const {
  return (bucket_index + 1u) % (bucket_count_ + 1u);
}

}  // namespace rtc
