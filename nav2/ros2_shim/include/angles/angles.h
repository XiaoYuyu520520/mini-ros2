#ifndef ANGLES__ANGLES_H_
#define ANGLES__ANGLES_H_

#include <cmath>
#include <limits>

namespace angles
{

static constexpr double PI = 3.14159265358979323846;
static constexpr double TWO_PI = 2.0 * PI;

inline double from_degrees(double degrees) {return degrees * PI / 180.0;}
inline double to_degrees(double radians) {return radians * 180.0 / PI;}
inline double from_radians(double radians) {return radians;}

inline double normalize_angle_positive(double angle)
{
  return std::fmod(std::fmod(angle, TWO_PI) + TWO_PI, TWO_PI);
}

inline double normalize_angle(double angle)
{
  double a = normalize_angle_positive(angle);
  if (a > PI) {
    a -= TWO_PI;
  }
  return a;
}

inline double shortest_angular_distance(double from, double to)
{
  return normalize_angle(to - from);
}

inline double two_pi_complement(double angle)
{
  if (angle > 0.0) {
    return TWO_PI - angle;
  }
  return -TWO_PI - angle;
}

}  // namespace angles

#endif  // ANGLES__ANGLES_H_
