#ifndef STD_SRVS_SRV_EMPTY_HPP_
#define STD_SRVS_SRV_EMPTY_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_srvs
{
namespace srv
{

struct Empty
{
  static constexpr const char * ros2_service_type = "std_srvs/srv/Empty";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "std_srvs/srv/Empty_Request";
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
      static constexpr const char * ros2_message_type = "std_srvs/srv/Empty_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;

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

      bool operator==(const Response & other) const
      {
        (void)other;
        return true;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace std_srvs

#endif  // STD_SRVS_SRV_EMPTY_HPP_
