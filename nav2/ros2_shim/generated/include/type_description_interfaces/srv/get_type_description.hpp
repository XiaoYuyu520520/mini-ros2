#ifndef TYPE_DESCRIPTION_INTERFACES_SRV_GETTYPEDESCRIPTION_HPP_
#define TYPE_DESCRIPTION_INTERFACES_SRV_GETTYPEDESCRIPTION_HPP_

#include "type_description_interfaces/msg/key_value.hpp"
#include "type_description_interfaces/msg/type_description.hpp"
#include "type_description_interfaces/msg/type_source.hpp"
#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace type_description_interfaces
{
namespace srv
{

struct GetTypeDescription
{
  static constexpr const char * ros2_service_type = "type_description_interfaces/srv/GetTypeDescription";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "type_description_interfaces/srv/GetTypeDescription_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _type_name_type = std::string;
      std::string type_name;
      using _type_hash_type = std::string;
      std::string type_hash;
      using _include_type_sources_type = bool;
      bool include_type_sources = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(type_name);
        ar(type_hash);
        ar(include_type_sources);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(type_name);
        ar(type_hash);
        ar(include_type_sources);
      }

      bool operator==(const Request & other) const
      {
        return type_name == other.type_name &&
          type_hash == other.type_hash &&
          include_type_sources == other.include_type_sources;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "type_description_interfaces/srv/GetTypeDescription_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _successful_type = bool;
      bool successful = false;
      using _failure_reason_type = std::string;
      std::string failure_reason;
      using _type_description_type = ::type_description_interfaces::msg::TypeDescription;
      ::type_description_interfaces::msg::TypeDescription type_description;
      using _type_sources_type = std::vector<::type_description_interfaces::msg::TypeSource>;
      std::vector<::type_description_interfaces::msg::TypeSource> type_sources;
      using _extra_information_type = std::vector<::type_description_interfaces::msg::KeyValue>;
      std::vector<::type_description_interfaces::msg::KeyValue> extra_information;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(successful);
        ar(failure_reason);
        ar(type_description);
        ar(type_sources);
        ar(extra_information);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(successful);
        ar(failure_reason);
        ar(type_description);
        ar(type_sources);
        ar(extra_information);
      }

      bool operator==(const Response & other) const
      {
        return successful == other.successful &&
          failure_reason == other.failure_reason &&
          type_description == other.type_description &&
          type_sources == other.type_sources &&
          extra_information == other.extra_information;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace type_description_interfaces

#endif  // TYPE_DESCRIPTION_INTERFACES_SRV_GETTYPEDESCRIPTION_HPP_
