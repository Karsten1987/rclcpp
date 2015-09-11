// Copyright 2014 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RCLCPP_RCLCPP_EXECUTORS_MULTI_THREADED_EXECUTOR_HPP_
#define RCLCPP_RCLCPP_EXECUTORS_MULTI_THREADED_EXECUTOR_HPP_

#include <cassert>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include <rmw/rmw.h>

#include <rclcpp/executor.hpp>
#include <rclcpp/macros.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/utilities.hpp>

namespace rclcpp
{
namespace executors
{
namespace multi_threaded_executor
{

// 
class ExecutionThread {
private:
  typedef std::function<void()> FunctionT;
  std::thread thread_;
  // TODO atomic bool?
  bool done_ = false;
  bool launched_ = false;
  std::queue<FunctionT> work_queue_;
  std::mutex queue_mutex_;
  std::shared_ptr<std::condition_variable> cv_;
  

  void execute() {
    while (!done_) {
      // Wait until work might be available (signal by condition variable)
      if (work_queue_.empty()) {
        cv_->wait();
        // TODO: waiting mechanism
      } else {
        // Execute the next piece of work 
        FunctionT current_work;
        {
          std::lock_guard<std::mutex> wait_lock(queue_mutex_);
          current_work = work_queue_.front();
        }
        current_work();
        {
          std::lock_guard<std::mutex> wait_lock(queue_mutex_);
          work_queue_.pop();
        }
      }
    }
  }

public:
  ExecutionThread(std::shared_ptr<std::condition_variable> cv) :
    cv_(cv), 
  {
  }

  void add_work(FunctionT function) {
    std::lock_guard<std::mutex> wait_lock(queue_mutex_);
    work_queue_.push(function);
  }

  void launch() {
    if (!launched_) {
      thread_ = std::thread(
        [this](){
          this->execute();
        }
      );
      launched_ = true;
    }
  }

  void set_done(bool done) {
    done_ = done;
  }

  void join() {
    thread_.join();
  }
};

class MultiThreadedExecutor : public executor::Executor
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(MultiThreadedExecutor);

  MultiThreadedExecutor(memory_strategy::MemoryStrategy::SharedPtr ms =
    memory_strategy::create_default_strategy())
  : executor::Executor(ms)
  {
    number_of_threads_ = std::thread::hardware_concurrency();
    if (number_of_threads_ == 0) {
      number_of_threads_ = 1;
    }
  }

  virtual ~MultiThreadedExecutor() {}

  void
  spin()
  {
    std::vector<std::thread> threads;
    {
      std::lock_guard<std::mutex> wait_lock(wait_mutex_);
      size_t thread_id_ = 1;  // Use a _ suffix to avoid shadowing `rclcpp::thread_id`
      for (size_t i = number_of_threads_; i > 0; --i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto func = std::bind(&MultiThreadedExecutor::run, this, thread_id_++);
        threads.emplace_back(func);
      }
    }
    for (auto & thread : threads) {
      thread.join();
    }
  }

  size_t
  get_number_of_threads()
  {
    return number_of_threads_;
  }

private:
  void run(size_t this_thread_id)
  {
    rclcpp::thread_id = this_thread_id;
    while (rclcpp::utilities::ok()) {
      executor::AnyExecutable::SharedPtr any_exec;
      {
        std::lock_guard<std::mutex> wait_lock(wait_mutex_);
        if (!rclcpp::utilities::ok()) {
          return;
        }
        any_exec = get_next_executable();
      }
      execute_any_executable(any_exec);
    }
  }

  RCLCPP_DISABLE_COPY(MultiThreadedExecutor);

  std::mutex wait_mutex_;
  size_t number_of_threads_;
  std::vector<ExecutionThread> exec_threads_;

};

} /* namespace multi_threaded_executor */
} /* namespace executors */
} /* namespace rclcpp */

#endif /* RCLCPP_RCLCPP_EXECUTORS_MULTI_THREADED_EXECUTOR_HPP_ */
