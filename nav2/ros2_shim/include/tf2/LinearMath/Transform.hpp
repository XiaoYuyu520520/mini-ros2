#ifndef TF2__LINEARMATH__TRANSFORM_HPP_
#define TF2__LINEARMATH__TRANSFORM_HPP_

#include "tf2/LinearMath/Matrix3x3.hpp"
#include "tf2/LinearMath/Quaternion.hpp"
#include "tf2/LinearMath/Vector3.hpp"

namespace tf2
{

class Transform
{
public:
  Transform() = default;

  static Transform getIdentity()
  {
    return Transform();
  }

  Transform(const Quaternion & q, const Vector3 & c = Vector3())
  : m_basis(q), m_origin(c)
  {
  }

  Transform(const Matrix3x3 & b, const Vector3 & c = Vector3())
  : m_basis(b), m_origin(c)
  {
  }

  void setIdentity()
  {
    m_basis.setIdentity();
    m_origin = Vector3(0, 0, 0);
  }

  void setOrigin(const Vector3 & origin) {m_origin = origin;}
  const Vector3 & getOrigin() const {return m_origin;}
  Vector3 & getOrigin() {return m_origin;}

  void setRotation(const Quaternion & q) {m_basis.setRotation(q);}
  void setRotation(const Matrix3x3 & b) {m_basis = b;}
  Quaternion getRotation() const
  {
    Quaternion q;
    m_basis.getRotation(q);
    return q;
  }

  const Matrix3x3 & getBasis() const {return m_basis;}
  Matrix3x3 & getBasis() {return m_basis;}

  Vector3 operator()(const Vector3 & v) const {return m_basis * v + m_origin;}

  Transform operator*(const Transform & t) const
  {
    return Transform(m_basis * t.m_basis, (*this)(t.m_origin));
  }

  Transform inverse() const
  {
    const Matrix3x3 inv = m_basis.transpose();
    return Transform(inv, inv * (-m_origin));
  }

  Transform inverseTimes(const Transform & t) const
  {
    return inverse() * t;
  }

  Transform operator*(const Quaternion & q) const
  {
    return Transform(m_basis * Matrix3x3(q));
  }

private:
  Matrix3x3 m_basis;
  Vector3 m_origin;
};

inline Vector3 operator*(const Transform & t, const Vector3 & v) {return t(v);}

}  // namespace tf2

#endif  // TF2__LINEARMATH__TRANSFORM_HPP_
