#ifndef TF2__LINEARMATH__MATRIX3X3_HPP_
#define TF2__LINEARMATH__MATRIX3X3_HPP_

#include <cmath>

#include "tf2/LinearMath/Quaternion.hpp"
#include "tf2/LinearMath/Vector3.hpp"

namespace tf2
{

class Matrix3x3
{
public:
  Matrix3x3() {setIdentity();}

  Matrix3x3(
    double xx, double xy, double xz,
    double yx, double yy, double yz,
    double zx, double zy, double zz)
  {
    m[0][0] = xx; m[0][1] = xy; m[0][2] = xz;
    m[1][0] = yx; m[1][1] = yy; m[1][2] = yz;
    m[2][0] = zx; m[2][1] = zy; m[2][2] = zz;
  }

  explicit Matrix3x3(const Quaternion & q) {setRotation(q);}

  void setIdentity()
  {
    m[0][0] = 1; m[0][1] = 0; m[0][2] = 0;
    m[1][0] = 0; m[1][1] = 1; m[1][2] = 0;
    m[2][0] = 0; m[2][1] = 0; m[2][2] = 1;
  }

  void setRotation(const Quaternion & q)
  {
    const double d = q.length2();
    const double s = (d > 0.0) ? 2.0 / d : 0.0;
    const double xs = q.x() * s, ys = q.y() * s, zs = q.z() * s;
    const double wx = q.w() * xs, wy = q.w() * ys, wz = q.w() * zs;
    const double xx = q.x() * xs, xy = q.x() * ys, xz = q.x() * zs;
    const double yy = q.y() * ys, yz = q.y() * zs, zz = q.z() * zs;
    m[0][0] = 1.0 - (yy + zz); m[0][1] = xy - wz; m[0][2] = xz + wy;
    m[1][0] = xy + wz; m[1][1] = 1.0 - (xx + zz); m[1][2] = yz - wx;
    m[2][0] = xz - wy; m[2][1] = yz + wx; m[2][2] = 1.0 - (xx + yy);
  }

  void getRotation(Quaternion & q) const
  {
    const double trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.0) {
      const double s = 0.5 / std::sqrt(trace + 1.0);
      q.setValue((m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s, 0.25 / s);
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
      const double s = 2.0 * std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
      q.setValue(0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s);
    } else if (m[1][1] > m[2][2]) {
      const double s = 2.0 * std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
      q.setValue((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s);
    } else {
      const double s = 2.0 * std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
      q.setValue((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s);
    }
    q.normalize();
  }

  // tf2's matrix is row-major and its YPR is the same decomposition as getRPY()
  // with the arguments in the other order; callers that only want the yaw - the
  // map saver writing a YAML `origin` - spell it this way.
  void getEulerYPR(double & yaw, double & pitch, double & roll) const {getRPY(roll, pitch, yaw);}

  void getRPY(double & roll, double & pitch, double & yaw) const
  {
    roll = std::atan2(m[2][1], m[2][2]);
    pitch = std::atan2(-m[2][0], std::sqrt(m[2][1] * m[2][1] + m[2][2] * m[2][2]));
    yaw = std::atan2(m[1][0], m[0][0]);
  }

  Vector3 operator*(const Vector3 & v) const
  {
    return Vector3(
      m[0][0] * v.x() + m[0][1] * v.y() + m[0][2] * v.z(),
      m[1][0] * v.x() + m[1][1] * v.y() + m[1][2] * v.z(),
      m[2][0] * v.x() + m[2][1] * v.y() + m[2][2] * v.z());
  }

  Matrix3x3 operator*(const Matrix3x3 & o) const
  {
    Matrix3x3 r;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
      }
    }
    return r;
  }

  Matrix3x3 transpose() const
  {
    return Matrix3x3(
      m[0][0], m[1][0], m[2][0],
      m[0][1], m[1][1], m[2][1],
      m[0][2], m[1][2], m[2][2]);
  }

  Vector3 getRow(int i) const {return Vector3(m[i][0], m[i][1], m[i][2]);}

  double operator()(int i, int j) const {return m[i][j];}

  double m[3][3];
};

}  // namespace tf2

#endif  // TF2__LINEARMATH__MATRIX3X3_HPP_
