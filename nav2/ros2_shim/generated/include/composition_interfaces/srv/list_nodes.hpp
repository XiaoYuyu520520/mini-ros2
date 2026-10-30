#ifndef COMPOSITION_INTERFACES_SRV_LISTNODES_HPP_
#define COMPOSITION_INTERFACES_SRV_LISTNODES_HPP_

#include "vector"

#include <cstdint>
#include <memory>
#include <string>

namespace composition_interfaces
{
namespace srv
{

struct ListNodes
{
  static constexpr const char * ros2_service_type = "composition_interfaces/srv/ListNodes";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/ListNodes_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;

      template <class Archive>
      void visit(Archive & ar)
      {
        (void)ar;
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        (void)ar;
      }

      bool operator==(const Request & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "composition_interfaces/srv/ListNodes_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _full_node_names_type = std::vector<std::string>;
      std::vector<std::string> full_node_names;
      using _unique_ids_type = std::vector<uint64_t>;
      std::vector<uint64_t> unique_ids;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(full_node_names);
        ar(unique_ids);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(full_node_names);
        ar(unique_ids);
      }

      bool operator==(const Response & other) const
      {
        return full_node_names == other.full_node_names &&
          unique_ids == other.unique_ids;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace composition_interfaces

#endif  // COMPOSITION_INTERFACES_SRV_LISTNODES_HPP_
