#ifndef TF2__LINEARMATH__VECTOR3_HPP_
#define TF2__LINEARMATH__VECTOR3_HPP_

#include <cmath>

#include "tf2/LinearMath/Scalar.hpp"

namespace tf2
{

class Vector3
{
public:
  Vector3() : m_x(0), m_y(0), m_z(0) {}
  Vector3(double x, double y, double z) : m_x(x), m_y(y), m_z(z) {}

  double x() const {return m_x;}
  double y() const {return m_y;}
  double z() const {return m_z;}
  double getX() const {return m_x;}
  double getY() const {return m_y;}
  double getZ() const {return m_z;}
  void setX(double x) {m_x = x;}
  void setY(double y) {m_y = y;}
  void setZ(double z) {m_z = z;}

  void setValue(double x, double y, double z)
  {
    m_x = x;
    m_y = y;
    m_z = z;
  }

  double length2() const {return m_x * m_x + m_y * m_y + m_z * m_z;}
  double length() const {return std::sqrt(length2());}

  Vector3 & normalize()
  {
    const double l = length();
    if (l > 0.0) {
      m_x /= l;
      m_y /= l;
      m_z /= l;
    }
    return *this;
  }

  double dot(const Vector3 & v) const {return m_x * v.m_x + m_y * v.m_y + m_z * v.m_z;}

  Vector3 cross(const Vector3 & v) const
  {
    return Vector3(
      m_y * v.m_z - m_z * v.m_y,
      m_z * v.m_x - m_x * v.m_z,
      m_x * v.m_y - m_y * v.m_x);
  }

  Vector3 operator+(const Vector3 & v) const {return Vector3(m_x + v.m_x, m_y + v.m_y, m_z + v.m_z);}
  Vector3 operator-(const Vector3 & v) const {return Vector3(m_x - v.m_x, m_y - v.m_y, m_z - v.m_z);}
  Vector3 operator*(double s) const {return Vector3(m_x * s, m_y * s, m_z * s);}
  Vector3 operator-() const {return Vector3(-m_x, -m_y, -m_z);}

  Vector3 & operator+=(const Vector3 & v) {m_x += v.m_x; m_y += v.m_y; m_z += v.m_z; return *this;}
  Vector3 & operator-=(const Vector3 & v) {m_x -= v.m_x; m_y -= v.m_y; m_z -= v.m_z; return *this;}
  Vector3 & operator*=(double s) {m_x *= s; m_y *= s; m_z *= s; return *this;}

  double operator[](int i) const {return i == 0 ? m_x : (i == 1 ? m_y : m_z);}
  double & operator[](int i) {return i == 0 ? m_x : (i == 1 ? m_y : m_z);}

  double distance(const Vector3 & v) const {return (*this - v).length();}

private:
  double m_x, m_y, m_z;
};

inline Vector3 operator*(double s, const Vector3 & v) {return v * s;}

}  // namespace tf2

#endif  // TF2__LINEARMATH__VECTOR3_HPP_
