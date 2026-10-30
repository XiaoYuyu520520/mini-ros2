#ifndef OMPL_BASE_SPACES_DETAIL_SE2PATH_H
#define OMPL_BASE_SPACES_DETAIL_SE2PATH_H

#include <cmath>

#include "ompl/base/StateSpace.h"

namespace ompl
{
namespace base
{
namespace detail
{

constexpr double kSe2Pi = 3.14159265358979323846;

/// Wrap an angle into [0, 2*pi).
inline double mod2pi(double angle)
{
  const double twoPi = 2.0 * kSe2Pi;
  double wrapped = std::fmod(angle, twoPi);
  if (wrapped < 0.0) {
    wrapped += twoPi;
  }
  return wrapped;
}

/// The left normal of a heading: `(cos, sin)` rotated by +90 degrees.
inline void leftNormal(double theta, double & nx, double & ny)
{
  nx = -std::sin(theta);
  ny = std::cos(theta);
}

/// One leg of a path, starting from wherever the previous leg ended.
///
/// `length` is signed because a Reeds-Shepp path can drive a leg backwards, and
/// `turn` is the signed heading change over it. Their ratio is the steering:
/// `turn / length` is the curvature, and it is the same whether the robot
/// drives the leg forwards or backwards, because a steering angle does not care
/// which way the wheels turn.
struct Se2Leg
{
  double length{0.0};
  double turn{0.0};

  bool straight() const {return turn == 0.0;}
};

/// Up to three legs, the number of moves Dubins' and Reeds-Shepp's words are
/// made of.
struct Se2Path
{
  int count{0};
  Se2Leg leg[3];

  /// Arc length, counting a leg driven backwards as the distance it covers.
  double length() const
  {
    double total = 0.0;
    for (int i = 0; i < count; ++i) {
      total += std::fabs(leg[i].length);
    }
    return total;
  }
};

/// The pose `s` metres along `path`, which leaves from `start`.
///
/// This is the only place that turns legs into positions, and it is also what
/// the unit tests integrate to check a closed-form solution against the path it
/// claims to describe.
inline void se2PoseAt(const Se2Path & path, const double start[3], double s, double out[3])
{
  double x = start[0];
  double y = start[1];
  double theta = start[2];

  for (int i = 0; i < path.count; ++i) {
    const Se2Leg & leg = path.leg[i];
    const double distance = std::fabs(leg.length);
    if (distance <= 0.0) {
      continue;
    }
    const double travelled = s < distance ? s : distance;
    s -= travelled;
    const double fraction = travelled / distance;

    if (leg.turn == 0.0) {
      // A straight leg advances along the heading, or backs up along it.
      const double advance = leg.length * fraction;
      x += advance * std::cos(theta);
      y += advance * std::sin(theta);
    } else {
      // A constant curvature leg turns about a fixed centre. The centre sits
      // on the side the steering points to - the sign of the curvature - and
      // stays there when the leg is driven backwards, which is what makes a
      // reverse arc curve the other way.
      const double radius = distance / std::fabs(leg.turn);
      const double sign = (leg.turn / leg.length) > 0.0 ? 1.0 : -1.0;
      double nx = 0.0;
      double ny = 0.0;
      leftNormal(theta, nx, ny);
      const double cx = x + sign * radius * nx;
      const double cy = y + sign * radius * ny;
      theta += leg.turn * fraction;
      x = cx + sign * radius * std::sin(theta);
      y = cy - sign * radius * std::cos(theta);
    }

    if (s <= 0.0) {
      break;
    }
  }

  out[0] = x;
  out[1] = y;
  out[2] = mod2pi(theta);
}

/// A path plus the pose it starts from, and whether it is driven in reverse.
struct Se2Solution
{
  Se2Path path;
  double start[3]{0.0, 0.0, 0.0};
  double length{0.0};
  bool reversed{false};
};

/// The same curve, driven from the far end.
///
/// Every leg comes back in the opposite order with its length and its heading
/// change negated - the steering, their ratio, is unchanged - and the new start
/// pose is where the original ended. The headings are the vehicle's own, so a
/// leg that was driven forwards is now driven backwards and the heading
/// sequence still describes the same curve.
inline Se2Solution reverseSe2Solution(const Se2Solution & solution)
{
  double end[3];
  se2PoseAt(solution.path, solution.start, solution.path.length(), end);

  Se2Solution reversed;
  reversed.path.count = solution.path.count;
  for (int i = 0; i < solution.path.count; ++i) {
    const Se2Leg & leg = solution.path.leg[solution.path.count - 1 - i];
    reversed.path.leg[i].length = -leg.length;
    reversed.path.leg[i].turn = -leg.turn;
  }
  reversed.start[0] = end[0];
  reversed.start[1] = end[1];
  reversed.start[2] = end[2];
  reversed.length = solution.length;
  reversed.reversed = !solution.reversed;
  return reversed;
}

}  // namespace detail
}  // namespace base
}  // namespace ompl

#endif  // OMPL_BASE_SPACES_DETAIL_SE2PATH_H
