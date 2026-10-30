#ifndef TF2__LINEARMATH__QUATERNION_HPP_
#define TF2__LINEARMATH__QUATERNION_HPP_

#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "tf2/LinearMath/Vector3.hpp"

namespace tf2
{

class Matrix3x3;

class Quaternion
{
public:
  Quaternion() : m_x(0), m_y(0), m_z(0), m_w(1) {}
  Quaternion(double x, double y, double z, double w) : m_x(x), m_y(y), m_z(z), m_w(w) {}

  explicit Quaternion(const Vector3 & axis, double angle)
  {
    setRotation(axis, angle);
  }

  double x() const {return m_x;}
  double y() const {return m_y;}
  double z() const {return m_z;}
  double w() const {return m_w;}
  double getX() const {return m_x;}
  double getY() const {return m_y;}
  double getZ() const {return m_z;}
  double getW() const {return m_w;}

  void setValue(double x, double y, double z, double w)
  {
    m_x = x;
    m_y = y;
    m_z = z;
    m_w = w;
  }

  void setX(double x) {m_x = x;}
  void setY(double y) {m_y = y;}
  void setZ(double z) {m_z = z;}
  void setW(double w) {m_w = w;}

  void setRPY(double roll, double pitch, double yaw)
  {
    const double cr = std::cos(roll * 0.5);
    const double sr = std::sin(roll * 0.5);
    const double cp = std::cos(pitch * 0.5);
    const double sp = std::sin(pitch * 0.5);
    const double cy = std::cos(yaw * 0.5);
    const double sy = std::sin(yaw * 0.5);
    m_x = sr * cp * cy - cr * sp * sy;
    m_y = cr * sp * cy + sr * cp * sy;
    m_z = cr * cp * sy - sr * sp * cy;
    m_w = cr * cp * cy + sr * sp * sy;
  }

  void setEulerZYX(double yaw, double pitch, double roll) {setRPY(roll, pitch, yaw);}

  // Deprecated upstream spelling; kept because it is what slam_toolbox calls.
  void setEuler(double yaw, double pitch, double roll) {setEulerZYX(yaw, pitch, roll);}

  void setRotation(const Vector3 & axis, double angle)
  {
    const double d = axis.length();
    if (d < 1e-12) {
      setValue(0, 0, 0, 1);
      return;
    }
    const double s = std::sin(angle * 0.5) / d;
    setValue(axis.x() * s, axis.y() * s, axis.z() * s, std::cos(angle * 0.5));
  }

  void getRPY(double & roll, double & pitch, double & yaw) const
  {
    // roll (x-axis rotation)
    const double sinr_cosp = 2.0 * (m_w * m_x + m_y * m_z);
    const double cosr_cosp = 1.0 - 2.0 * (m_x * m_x + m_y * m_y);
    roll = std::atan2(sinr_cosp, cosr_cosp);
    // pitch (y-axis rotation)
    const double sinp = 2.0 * (m_w * m_y - m_z * m_x);
    if (std::fabs(sinp) >= 1.0) {
      pitch = std::copysign(M_PI / 2.0, sinp);
    } else {
      pitch = std::asin(sinp);
    }
    // yaw (z-axis rotation)
    const double siny_cosp = 2.0 * (m_w * m_z + m_x * m_y);
    const double cosy_cosp = 1.0 - 2.0 * (m_y * m_y + m_z * m_z);
    yaw = std::atan2(siny_cosp, cosy_cosp);
  }

  double length2() const {return m_x * m_x + m_y * m_y + m_z * m_z + m_w * m_w;}
  double length() const {return std::sqrt(length2());}

  Quaternion & normalize()
  {
    const double l = length();
    if (l > 0.0) {
      m_x /= l;
      m_y /= l;
      m_z /= l;
      m_w /= l;
    }
    return *this;
  }

  Quaternion inverse() const {return Quaternion(-m_x, -m_y, -m_z, m_w);}

  Quaternion normalized() const
  {
    Quaternion out = *this;
    out.normalize();
    return out;
  }

  Quaternion operator/(double s) const {return Quaternion(m_x / s, m_y / s, m_z / s, m_w / s);}

  bool isnan() const
  {
    return std::isnan(m_x) || std::isnan(m_y) || std::isnan(m_z) || std::isnan(m_w);
  }

  /// Half the angle between this quaternion and `q`, upstream's spelling.
  double angle(const Quaternion & q) const
  {
    const double s = std::sqrt(length2() * q.length2());
    if (s == 0.0) {
      return 0.0;
    }
    return std::acos(clamped(dot(q) / s));
  }

  /// The full angle between the two rotations, taking the short way round;
  /// `truncate_path_local_action` measures how hard a path turns with it.
  double angleShortestPath(const Quaternion & q) const
  {
    const double s = std::sqrt(length2() * q.length2());
    if (s == 0.0) {
      return 0.0;
    }
    return std::acos(clamped(std::fabs(dot(q)) / s)) * 2.0;
  }

  /// The angle of the rotation this quaternion represents, in [0, 2*pi].
  double getAngle() const {return 2.0 * std::acos(clamped(m_w));}

  /// The same, folded into [0, pi].
  double getAngleShortestPath() const {return 2.0 * std::acos(clamped(std::fabs(m_w)));}

  Vector3 getAxis() const
  {
    const double s_squared = 1.0 - m_w * m_w;
    if (s_squared < 1e-12) {
      return Vector3(1.0, 0.0, 0.0);
    }
    const double s = std::sqrt(s_squared);
    return Vector3(m_x / s, m_y / s, m_z / s);
  }

  Quaternion operator*(const Quaternion & q) const
  {
    return Quaternion(
      m_w * q.m_x + m_x * q.m_w + m_y * q.m_z - m_z * q.m_y,
      m_w * q.m_y - m_x * q.m_z + m_y * q.m_w + m_z * q.m_x,
      m_w * q.m_z + m_x * q.m_y - m_y * q.m_x + m_z * q.m_w,
      m_w * q.m_w - m_x * q.m_x - m_y * q.m_y - m_z * q.m_z);
  }

  Quaternion & operator*=(const Quaternion & q)
  {
    *this = (*this) * q;
    return *this;
  }

  Vector3 operator*(const Vector3 & v) const
  {
    const Vector3 qv(m_x, m_y, m_z);
    const Vector3 t = qv.cross(v) * 2.0;
    return v + t * m_w + qv.cross(t);
  }

  double dot(const Quaternion & q) const
  {
    return m_x * q.m_x + m_y * q.m_y + m_z * q.m_z + m_w * q.m_w;
  }

  Quaternion & operator+=(const Quaternion & q)
  {
    m_x += q.m_x;
    m_y += q.m_y;
    m_z += q.m_z;
    m_w += q.m_w;
    return *this;
  }

  Quaternion operator*(double s) const {return Quaternion(m_x * s, m_y * s, m_z * s, m_w * s);}

  Quaternion slerp(const Quaternion & q, double t) const
  {
    Quaternion target = q;
    double cosom = dot(q);
    if (cosom < 0.0) {
      cosom = -cosom;
      target = Quaternion(-q.m_x, -q.m_y, -q.m_z, -q.m_w);
    }
    double scale0 = 1.0 - t;
    double scale1 = t;
    if (1.0 - cosom > 1e-6) {
      const double omega = std::acos(cosom);
      const double sinom = std::sin(omega);
      scale0 = std::sin((1.0 - t) * omega) / sinom;
      scale1 = std::sin(t * omega) / sinom;
    }
    return Quaternion(
      m_x * scale0 + target.m_x * scale1,
      m_y * scale0 + target.m_y * scale1,
      m_z * scale0 + target.m_z * scale1,
      m_w * scale0 + target.m_w * scale1);
  }

private:
  static double clamped(double value)
  {
    return value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
  }

  double m_x, m_y, m_z, m_w;
};

inline double getYaw(const Quaternion & q)
{
  double roll, pitch, yaw;
  q.getRPY(roll, pitch, yaw);
  return yaw;
}

}  // namespace tf2

#endif  // TF2__LINEARMATH__QUATERNION_HPP_
