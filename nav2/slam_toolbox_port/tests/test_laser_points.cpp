// Karto turns a LaserScan into Cartesian points using the angle range and
// angular resolution stored on its LaserRangeFinder, *not* the values carried in
// the message. slam_toolbox builds the occupancy grid out of those points, so a
// wrong angle interpretation there produces a map that has nothing to do with
// the room the robot is standing in -- while the poses, the matcher input and
// the published /pose all still look plausible.
//
// This test scans a known room from a known pose and checks the points land on
// the walls.
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

#include "karto_sdk/Karto.h"

namespace
{

int g_failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("  FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++g_failures; \
    } \
  } while (0)

constexpr double kPi = 3.14159265358979323846;
constexpr int kBeams = 360;
constexpr double kRoomHalfX = 4.0;
constexpr double kRoomHalfY = 3.0;

// Distance from (x, y) to the room border along `angle`.
double raycast(double x, double y, double angle)
{
  const double dx = std::cos(angle);
  const double dy = std::sin(angle);
  double best = std::numeric_limits<double>::max();

  if (std::fabs(dx) > 1e-9) {
    const double wall = dx > 0.0 ? kRoomHalfX : -kRoomHalfX;
    const double t = (wall - x) / dx;
    if (t > 0.0 && std::fabs(y + t * dy) <= kRoomHalfY) {
      best = std::min(best, t);
    }
  }
  if (std::fabs(dy) > 1e-9) {
    const double wall = dy > 0.0 ? kRoomHalfY : -kRoomHalfY;
    const double t = (wall - y) / dy;
    if (t > 0.0 && std::fabs(x + t * dx) <= kRoomHalfX) {
      best = std::min(best, t);
    }
  }
  return best;
}

}  // namespace

int main()
{
  try {
  // Same sequence of calls as laser_utils::LaserAssistant::makeLaser().
  karto::LaserRangeFinder * laser = karto::LaserRangeFinder::CreateLaserRangeFinder(
    karto::LaserRangeFinder_Custom, karto::Name("Custom Described Lidar"));
  laser->SetOffsetPose(karto::Pose2(0.0, 0.0, 0.0));
  laser->SetMinimumRange(0.05);
  laser->SetMaximumRange(30.0);
  laser->SetMinimumAngle(-kPi);
  laser->SetMaximumAngle(kPi);
  laser->SetAngularResolution(2.0 * kPi / kBeams);
  laser->SetIs360Laser(true);
  laser->SetRangeThreshold(25.0);
  // LocalizedRangeScan::GetLaserRangeFinder() resolves the sensor by name
  // through Karto's global sensor manager, exactly like slam_toolbox does.
  karto::SensorManager::GetInstance()->RegisterSensor(laser);

  const double span_deg = (laser->GetMaximumAngle() - laser->GetMinimumAngle()) * 180.0 / kPi;
  const double res_deg = laser->GetAngularResolution() * 180.0 / kPi;
  std::printf(
    "  laser reports %u readings over %.1f deg at %.3f deg/beam\n",
    laser->GetNumberOfRangeReadings(), span_deg, res_deg);
  std::printf(
    "  angles %.1f..%.1f deg, resolution %.3f deg, is360 %d, threshold %.1f m\n",
    laser->GetMinimumAngle() * 180.0 / kPi, laser->GetMaximumAngle() * 180.0 / kPi,
    res_deg, laser->GetIs360Laser(), laser->GetRangeThreshold());

  const double px = 1.5, py = 0.0, pyaw = kPi / 2.0;
  // ranges[i] is the distance measured at bearing (angle_min + i * increment)
  // *in the sensor frame*, so the ray that produced it points along that
  // bearing rotated by the sensor's own yaw. Getting this wrong leaves the
  // scan geometrically rotated with respect to the pose it is stamped with.
  std::vector<double> readings(kBeams);
  for (int i = 0; i < kBeams; ++i) {
    readings[i] = raycast(px, py, pyaw - kPi + i * (2.0 * kPi / kBeams));
  }

  karto::LocalizedRangeScan * scan =
    new karto::LocalizedRangeScan(laser->GetName(), readings);
  scan->SetOdometricPose(karto::Pose2(px, py, pyaw));
  scan->SetCorrectedPose(karto::Pose2(px, py, pyaw));
  CHECK(scan->GetNumberOfRangeReadings() == static_cast<kt_int32u>(kBeams));

  const karto::PointVectorDouble points =
    laser->GetPointReadings(scan, nullptr, false, false);

  double min_x = std::numeric_limits<double>::max(), max_x = -min_x;
  double min_y = std::numeric_limits<double>::max(), max_y = -min_y;
  size_t inside = 0;
  bool bins[36] = {false};
  for (const auto & point : points) {
    min_x = std::min(min_x, point.GetX());
    max_x = std::max(max_x, point.GetX());
    min_y = std::min(min_y, point.GetY());
    max_y = std::max(max_y, point.GetY());
    if (std::fabs(point.GetX()) <= kRoomHalfX + 0.06 &&
      std::fabs(point.GetY()) <= kRoomHalfY + 0.06)
    {
      ++inside;
    }
    const double bearing = std::atan2(point.GetY() - py, point.GetX() - px);
    int bin = static_cast<int>((bearing + kPi) / (2.0 * kPi) * 36.0);
    bin = bin < 0 ? 0 : (bin > 35 ? 35 : bin);
    bins[bin] = true;
  }

  int covered = 0;
  for (bool seen : bins) {
    covered += seen ? 1 : 0;
  }

  std::printf(
    "  %zu points, bbox %.2f x %.2f m, %zu/%d on the room border, "
    "%d/36 bearing bins covered\n",
    points.size(), max_x - min_x, max_y - min_y, inside, kBeams, covered);

  // The scan is a full turn of the laser, so its points have to be there and
  // they have to be on the walls of the room it was taken in.
  CHECK(points.size() == static_cast<size_t>(kBeams));
  CHECK(inside == static_cast<size_t>(kBeams));
  CHECK(covered == 36);

  delete scan;
  karto::SensorManager::GetInstance()->UnregisterSensor(laser);
  delete laser;

  } catch (const karto::Exception & e) {
    std::fflush(stdout);
    std::printf("  FAIL: karto::Exception: %s\n", e.GetErrorMessage().c_str());
    return 1;
  }

  if (g_failures == 0) {
    std::printf("  laser points: all checks passed\n");
  }
  std::fflush(stdout);
  return g_failures == 0 ? 0 : 1;
}
