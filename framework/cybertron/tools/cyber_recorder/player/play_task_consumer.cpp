/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "cybertron/tools/cyber_recorder/player/play_task_consumer.h"

#include "cybertron/common/log.h"
#include "cybertron/time/time.h"

namespace apollo {
namespace cybertron {
namespace record {

PlayTaskConsumer::PlayTaskConsumer(const TaskBufferPtr& task_buffer,
                                   double play_rate)
    : play_rate_(play_rate),
      consume_th_(nullptr),
      task_buffer_(task_buffer),
      is_stopped_(true),
      is_paused_(false),
      base_msg_play_time_ns_(0),
      last_played_msg_real_time_ns_(0) {
  if (play_rate_ <= 0) {
    AERROR << "invalid play rate: " << play_rate_
           << " , we will use default value(1.0).";
    play_rate_ = 1.0;
  }
}

PlayTaskConsumer::~PlayTaskConsumer() { Stop(); }

void PlayTaskConsumer::Start() {
  if (!is_stopped_.exchange(false)) {
    return;
  }
  consume_th_.reset(new std::thread(&PlayTaskConsumer::ThreadFunc, this));
}

void PlayTaskConsumer::Stop() {
  if (is_stopped_.exchange(true)) {
    return;
  }
  if (consume_th_ != nullptr && consume_th_->joinable()) {
    consume_th_->join();
    consume_th_ = nullptr;
  }
}

void PlayTaskConsumer::ThreadFunc() {
  uint64_t base_real_time_ns = 0;
  uint64_t accumulated_pause_time_ns = 0;

  while (!is_stopped_.load()) {
    auto task = task_buffer_->Pop();
    if (task == nullptr) {
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(kWaitProduceSleepNanoSec));
      continue;
    }

    if (base_msg_play_time_ns_ == 0) {
      base_msg_play_time_ns_ = task->msg_play_time_ns();
      base_real_time_ns = Time::Now().ToNanosecond();
      ADEBUG << "base_msg_play_time_ns: " << base_msg_play_time_ns_
             << "base_real_time_ns: " << base_real_time_ns;
    }

    uint64_t task_interval_ns =
        (task->msg_play_time_ns() - base_msg_play_time_ns_) / play_rate_;
    uint64_t real_time_interval_ns = Time::Now().ToNanosecond() -
                                     base_real_time_ns -
                                     accumulated_pause_time_ns;
    if (task_interval_ns > real_time_interval_ns) {
      uint64_t sleep_ns = task_interval_ns - real_time_interval_ns;
      std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
    }

    task->Play();
    last_played_msg_real_time_ns_ = task->msg_real_time_ns();

    while (is_paused_.load() && !is_stopped_.load()) {
      std::this_thread::sleep_for(std::chrono::nanoseconds(kPauseSleepNanoSec));
      accumulated_pause_time_ns += kPauseSleepNanoSec;
    }
  }
}

}  // namespace record
}  // namespace cybertron
}  // namespace apollo