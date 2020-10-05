#include "dynmsg_demo/cli.hpp"
#include "dynmsg_demo/message_reading.hpp"
#include "dynmsg_demo/msg_parser.hpp"
#include "dynmsg_demo/typesupport_utils.hpp"

#include <unistd.h>

#include <iostream>
#include <sstream>

#include "rcl/context.h"
#include "rcl/init_options.h"
#include "rcl/node_options.h"
#include "rcl/node.h"
#include "rcl/rcl.h"
#include "rcl/subscription.h"
#include "rcl/types.h"

#include "rcutils/logging_macros.h"


#include <stdexcept>
class NotImplemented : public std::logic_error
{
public:
    NotImplemented() : std::logic_error("Not implemented") { };
};


int
echo_topic(
  rcl_node_t * node,
  const std::string & topic,
  const InterfaceTypeName &interface_type) {
  std::cout << "Waiting for message on topic '" << topic << "' with type " <<
    interface_type.first << '/' << interface_type.second << '\n';

  RosMessage message;
  if (ros_message_init(interface_type, &message) != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "Message init failed");
    return 1;
  }

  RCUTILS_LOG_DEBUG_NAMED("cli-tool", "Creating subscription");
  rcl_subscription_t sub = rcl_get_zero_initialized_subscription();
  rcl_subscription_options_t sub_options = rcl_subscription_get_default_options();
  const auto* type_support = get_type_support(interface_type);
  if (type_support == nullptr) {
    return 1;
  }
  auto ret = rcl_subscription_init(&sub, node, type_support, topic.c_str(), &sub_options);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "subscription init failed");
    return 1;
  }

  while (true) {
    size_t count{0};
    ret = rmw_subscription_count_matched_publishers(rcl_subscription_get_rmw_handle(&sub), &count);
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED("cli-tool", "publisher count failed");
      return 1;
    }
    RCUTILS_LOG_DEBUG_NAMED("cli-tool", "There are %ld matched publishers", count);
    if (count > 0) {
      break;
    }
    sleep(0.25);
  }

  bool taken = false;
  while (!taken) {
    taken = false;
    ret = rmw_take(rcl_subscription_get_rmw_handle(&sub), message.data, &taken, nullptr);
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED("cli-tool", "take failed");
      return 1;
    }
    if (taken) {
      RCUTILS_LOG_DEBUG_NAMED("cli-tool", "Received data");
      break;
    }
  }

  std::cout << message_to_yaml(message) << '\n';

  ret = rcl_subscription_fini(&sub, node);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "subscription fini failed");
    return 1;
  }
  return 0;
}


int publish_to_topic(
  rcl_node_t * node,
  const std::string & topic,
  const InterfaceTypeName &interface_type,
  const std::string & message_yaml)
{
  std::cout << "Publishing message on topic '" << topic << "' with type " <<
    interface_type.first << '/' << interface_type.second << '\n';

  RosMessage message = yaml_to_rosmsg(interface_type, message_yaml);

  RCUTILS_LOG_DEBUG_NAMED("cli-tool", "Creating publisher");
  rcl_publisher_t pub = rcl_get_zero_initialized_publisher();
  rcl_publisher_options_t pub_options = rcl_publisher_get_default_options();
  const auto* type_support = get_type_support(interface_type);
  if (type_support == nullptr) {
    return 1;
  }
  auto ret = rcl_publisher_init(&pub, node, type_support, topic.c_str(), &pub_options);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "subscription init failed");
    return 1;
  }

  for (auto ii = 0; ii < 10; ++ii) {
    std::cout << "Publishing\n";
    ret = rcl_publish(&pub, message.data, nullptr);
    if (ret != RCL_RET_OK) {
      RCUTILS_LOG_ERROR_NAMED("cli-tool", "failed to publish message");
      return 1;
    }
    sleep(1);
  }

  ret = rcl_publisher_fini(&pub, node);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "publisher fini failed");
    return 1;
  }
  return 0;
}


int
main(int argc, char ** argv) {
  auto args = parse_arguments(argc, argv);

  rcl_init_options_t options = rcl_get_zero_initialized_init_options();
  rcl_ret_t ret = rcl_init_options_init(&options, rcl_get_default_allocator());
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "init options failed");
    return 1;
  }

  rcl_context_t context = rcl_get_zero_initialized_context();
  ret = rcl_init(argc, argv, &options, &context);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "init failed");
    return 1;
  }
  ret = rcl_init_options_fini(&options);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "init_options fini failed");
    return 1;
  }

  RCUTILS_LOG_DEBUG_NAMED("dynmsg_demo", "Creating node");
  rcl_node_options_t node_options = rcl_node_get_default_options();
  rcl_node_t node = rcl_get_zero_initialized_node();
  ret = rcl_node_init(&node, "dynmsg_test", "", &context, &node_options);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("dynmsg_demo", "node init failed");
    return 1;
  }

  InterfaceTypeName interface_type;
  switch (args.cmd) {
    case Command::TopicEcho:
      interface_type = get_topic_type(args.params["message_type"]);
      if (interface_type.first == "" || interface_type.second == "") {
        std::cout << "Unknown topic type '" << interface_type.first << '/' <<
          interface_type.second << "'\n";
        return 1;
      }
      return echo_topic(&node, args.params["topic"], interface_type);
    case Command::TopicPublish:
      interface_type = get_topic_type(args.params["message_type"]);
      if (interface_type.first == "" || interface_type.second == "") {
        std::cout << "Unknown topic type '" << interface_type.first << '/' <<
          interface_type.second << "'\n";
        return 1;
      }
      return publish_to_topic(&node, args.params["topic"], interface_type, args.params["msg"]);
    case Command::ServiceCall:
      throw NotImplemented();
    case Command::ServiceHost:
      throw NotImplemented();
    case Command::Unknown:
      std::cout << "Unknown command\n";
      return 1;
  };

  ret = rcl_node_fini(&node);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "node fini failed");
    return 1;
  }
  ret = rcl_shutdown(&context);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "shutdown failed");
    return 1;
  }
  ret = rcl_context_fini(&context);
  if (ret != RCL_RET_OK) {
    RCUTILS_LOG_ERROR_NAMED("cli-tool", "context fini failed");
    return 1;
  }
  return 0;
}