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

#ifndef RCLCPP__SERVICE_HPP_
#define RCLCPP__SERVICE_HPP_

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "rclcpp/any_service_callback.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/allocator/allocator_common.hpp"
#include "rclcpp/any_service_callback.hpp"
#include "rclcpp/visibility_control.hpp"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"

namespace rclcpp
{
namespace service
{

class ServiceBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(ServiceBase);

  RCLCPP_PUBLIC
  ServiceBase(
    std::shared_ptr<rmw_node_t> node_handle,
    rmw_service_t * service_handle,
    const std::string service_name);

  RCLCPP_PUBLIC
  virtual ~ServiceBase();

  RCLCPP_PUBLIC
  std::string
  get_service_name();

  RCLCPP_PUBLIC
  const rmw_service_t *
  get_service_handle();

  virtual std::shared_ptr<void> create_request() = 0;
  virtual std::shared_ptr<void> create_request_header() = 0;
  virtual void handle_request(
    std::shared_ptr<void> request_header,
    std::shared_ptr<void> request) = 0;

private:
  RCLCPP_DISABLE_COPY(ServiceBase);

  std::shared_ptr<rmw_node_t> node_handle_;

  rmw_service_t * service_handle_;
  std::string service_name_;
};

using any_service_callback::AnyServiceCallback;

template<typename ServiceT, typename Alloc = std::allocator<void>>
class Service : public ServiceBase
{
public:
  using RequestAllocTraits = allocator::AllocRebind<typename ServiceT::Request, Alloc>;
  using RequestAlloc = typename RequestAllocTraits::allocator_type;

  using ResponseAllocTraits = allocator::AllocRebind<typename ServiceT::Response, Alloc>;
  using ResponseAlloc = typename ResponseAllocTraits::allocator_type;

  using HeaderAllocTraits = allocator::AllocRebind<rmw_request_id_t, Alloc>;
  using HeaderAlloc = typename HeaderAllocTraits::allocator_type;

  using CallbackType = std::function<
      void(
        const std::shared_ptr<typename ServiceT::Request>,
        std::shared_ptr<typename ServiceT::Response>)>;

  using CallbackWithHeaderType = std::function<
      void(
        const std::shared_ptr<rmw_request_id_t>,
        const std::shared_ptr<typename ServiceT::Request>,
        std::shared_ptr<typename ServiceT::Response>)>;
  RCLCPP_SMART_PTR_DEFINITIONS(Service);

  Service(
    std::shared_ptr<rmw_node_t> node_handle,
    rmw_service_t * service_handle,
    const std::string & service_name,
    AnyServiceCallback<ServiceT> any_callback,
    std::shared_ptr<Alloc> allocator = nullptr)
  : ServiceBase(node_handle, service_handle, service_name), any_callback_(any_callback)
  {
    if (!allocator) {
      allocator = std::make_shared<Alloc>();
    }
    request_allocator_ = std::make_shared<RequestAlloc>(*allocator.get());
    response_allocator_ = std::make_shared<ResponseAlloc>(*allocator.get());
    header_allocator_ = std::make_shared<HeaderAlloc>(*allocator.get());
  }

  Service() = delete;

  std::shared_ptr<void> create_request()
  {
    return std::allocate_shared<typename ServiceT::Request>(*request_allocator_.get());
  }

  std::shared_ptr<void> create_request_header()
  {
    // TODO(wjwwood): This should probably use rmw_request_id's allocator.
    //                (since it is a C type)
    return std::allocate_shared<rmw_request_id_t>(*header_allocator_.get());
  }

  void handle_request(std::shared_ptr<void> request_header, std::shared_ptr<void> request)
  {
    //auto typed_request = std::static_pointer_cast<typename ServiceT::Request>(request);
    auto typed_request =
      rclcpp::allocator::allocator_static_pointer_cast<typename ServiceT::Request>(
        request, *request_allocator_.get());
    //auto typed_request_header = std::static_pointer_cast<rmw_request_id_t>(request_header);
    auto typed_request_header =
      rclcpp::allocator::allocator_static_pointer_cast<rmw_request_id_t>(
        request_header, *request_allocator_.get());
    auto response = std::allocate_shared<typename ServiceT::Response>(*response_allocator_.get());
    any_callback_.dispatch(typed_request_header, typed_request, response);
    send_response(typed_request_header, response);
  }

  void send_response(
    std::shared_ptr<rmw_request_id_t> req_id,
    std::shared_ptr<typename ServiceT::Response> response)
  {
    rmw_ret_t status = rmw_send_response(get_service_handle(), req_id.get(), response.get());
    if (status != RMW_RET_OK) {
      // *INDENT-OFF* (prevent uncrustify from making unecessary indents here)
      throw std::runtime_error(
        std::string("failed to send response: ") + rmw_get_error_string_safe());
      // *INDENT-ON*
    }
  }

private:
  RCLCPP_DISABLE_COPY(Service);

  AnyServiceCallback<ServiceT> any_callback_;

  std::shared_ptr<RequestAlloc> request_allocator_;
  std::shared_ptr<ResponseAlloc> response_allocator_;
  std::shared_ptr<HeaderAlloc> header_allocator_;
};

}  // namespace service
}  // namespace rclcpp

#endif  // RCLCPP__SERVICE_HPP_
