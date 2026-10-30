#ifndef SENSOR_MSGS__POINT_CLOUD2_ITERATOR_HPP_
#define SENSOR_MSGS__POINT_CLOUD2_ITERATOR_HPP_

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"

namespace sensor_msgs
{

inline uint32_t point_field_size(uint8_t datatype)
{
  switch (datatype) {
    case msg::PointField::INT8:
    case msg::PointField::UINT8: return 1;
    case msg::PointField::INT16:
    case msg::PointField::UINT16: return 2;
    case msg::PointField::INT32:
    case msg::PointField::UINT32:
    case msg::PointField::FLOAT32: return 4;
    case msg::PointField::FLOAT64: return 8;
    default: return 0;
  }
}

class PointCloud2Modifier
{
public:
  explicit PointCloud2Modifier(msg::PointCloud2 & cloud)
  : cloud_(cloud)
  {
  }

  void setPointCloud2Fields(int nfields, ...)
  {
    va_list args;
    va_start(args, nfields);
    cloud_.fields.clear();
    uint32_t offset = 0;
    for (int i = 0; i < nfields; ++i) {
      const char * name = va_arg(args, const char *);
      int count = va_arg(args, int);
      int datatype = va_arg(args, int);
      msg::PointField field;
      field.name = name ? name : "";
      field.offset = offset;
      field.datatype = static_cast<uint8_t>(datatype);
      field.count = static_cast<uint32_t>(count);
      cloud_.fields.push_back(field);
      offset += static_cast<uint32_t>(count) * point_field_size(field.datatype);
    }
    va_end(args);
    cloud_.point_step = offset;
    cloud_.row_step = cloud_.point_step * cloud_.width;
    cloud_.data.assign(cloud_.row_step * cloud_.height, 0);
  }

  void setPointCloud2FieldsByString(int nfields, ...)
  {
    va_list args;
    va_start(args, nfields);
    cloud_.fields.clear();
    uint32_t offset = 0;
    for (int i = 0; i < nfields; ++i) {
      const char * names = va_arg(args, const char *);
      for (const char * p = names; p && *p; ++p) {
        msg::PointField field;
        field.name = std::string(1, *p);
        field.offset = offset;
        field.datatype = msg::PointField::FLOAT32;
        field.count = 1;
        cloud_.fields.push_back(field);
        offset += 4;
      }
    }
    va_end(args);
    cloud_.point_step = offset;
    cloud_.row_step = cloud_.point_step * cloud_.width;
    cloud_.data.assign(cloud_.row_step * cloud_.height, 0);
  }

  void resize(size_t size)
  {
    cloud_.width = static_cast<uint32_t>(size);
    cloud_.height = 1;
    cloud_.row_step = cloud_.point_step * cloud_.width;
    cloud_.data.assign(cloud_.row_step * cloud_.height, 0);
  }

  void reserve(size_t) {}

private:
  msg::PointCloud2 & cloud_;
};

namespace detail
{
inline uint32_t field_offset(const msg::PointCloud2 & cloud, const std::string & name)
{
  for (const auto & field : cloud.fields) {
    if (field.name == name) {
      return field.offset;
    }
  }
  return 0;
}
}  // namespace detail

template<class T>
class PointCloud2Iterator
{
public:
  PointCloud2Iterator(msg::PointCloud2 & cloud, const std::string & field_name)
  : data_(cloud.data.data() + detail::field_offset(cloud, field_name)),
    step_(cloud.point_step),
    end_(cloud.data.data() + cloud.data.size())
  {
  }

  T & operator*() const {return *reinterpret_cast<T *>(data_);}
  T * operator->() const {return reinterpret_cast<T *>(data_);}

  PointCloud2Iterator & operator++() {data_ += step_; return *this;}
  PointCloud2Iterator & operator+=(int n) {data_ += step_ * n; return *this;}

  bool operator!=(const PointCloud2Iterator & other) const {return data_ != other.data_;}

  PointCloud2Iterator end() const
  {
    PointCloud2Iterator out(*this);
    out.data_ = end_;
    return out;
  }

private:
  uint8_t * data_;
  uint32_t step_;
  uint8_t * end_;
};

template<class T>
class PointCloud2ConstIterator
{
public:
  PointCloud2ConstIterator(const msg::PointCloud2 & cloud, const std::string & field_name)
  : data_(cloud.data.data() + detail::field_offset(cloud, field_name)),
    step_(cloud.point_step),
    end_(cloud.data.data() + cloud.data.size())
  {
  }

  const T & operator*() const {return *reinterpret_cast<const T *>(data_);}
  const T * operator->() const {return reinterpret_cast<const T *>(data_);}

  PointCloud2ConstIterator & operator++() {data_ += step_; return *this;}
  PointCloud2ConstIterator & operator+=(int n) {data_ += step_ * n; return *this;}

  bool operator!=(const PointCloud2ConstIterator & other) const {return data_ != other.data_;}

  PointCloud2ConstIterator end() const
  {
    PointCloud2ConstIterator out(*this);
    out.data_ = end_;
    return out;
  }

  bool operator==(const PointCloud2ConstIterator & other) const {return data_ == other.data_;}

private:
  const uint8_t * data_;
  uint32_t step_;
  const uint8_t * end_;
};

}  // namespace sensor_msgs

#endif  // SENSOR_MSGS__POINT_CLOUD2_ITERATOR_HPP_
