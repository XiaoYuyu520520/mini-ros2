#ifndef STD_SRVS_SRV_SETBOOL_HPP_
#define STD_SRVS_SRV_SETBOOL_HPP_


#include <cstdint>
#include <memory>
#include <string>

namespace std_srvs
{
namespace srv
{

struct SetBool
{
  static constexpr const char * ros2_service_type = "std_srvs/srv/SetBool";
    struct Request
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "std_srvs/srv/SetBool_Request";
      using SharedPtr = std::shared_ptr<Request>;
      using ConstSharedPtr = std::shared_ptr<const Request>;
      using UniquePtr = std::unique_ptr<Request>;
      using _data_type = bool;
      bool data = false;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(data);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(data);
      }

      bool operator==(const Request & other) const
      {
        return data == other.data;
      }
      bool operator!=(const Request & other) const {return !(*this == other);}
    };

    struct Response
    {
      using ros2_message_tag = void;
      static constexpr const char * ros2_message_type = "std_srvs/srv/SetBool_Response";
      using SharedPtr = std::shared_ptr<Response>;
      using ConstSharedPtr = std::shared_ptr<const Response>;
      using UniquePtr = std::unique_ptr<Response>;
      using _success_type = bool;
      bool success = false;
      using _message_type = std::string;
      std::string message;

      template <class Archive>
      void visit(Archive & ar)
      {
        ar(success);
        ar(message);
      }

      template <class Archive>
      void visit(Archive & ar) const
      {
        ar(success);
        ar(message);
      }

      bool operator==(const Response & other) const
      {
        return success == other.success &&
          message == other.message;
      }
      bool operator!=(const Response & other) const {return !(*this == other);}
    };
};

}  // namespace srv
}  // namespace std_srvs

#endif  // STD_SRVS_SRV_SETBOOL_HPP_
