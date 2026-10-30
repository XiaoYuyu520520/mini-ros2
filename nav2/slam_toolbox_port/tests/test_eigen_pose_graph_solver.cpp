// Verifies the Eigen pose-graph solver that replaces slam_toolbox's Ceres plugin
// on Windows.
//
// The graph is a square the scanner drives around. Two properties matter:
//   * given exact relative measurements and an odometry estimate that is only
//     slightly off, the solver lands on the true geometry;
//   * given a chain whose yaw errors accumulate, the loop closure pulls the
//     result back together instead of leaving the end where dead reckoning put
//     it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <Eigen/Core>

#include "karto_sdk/Mapper.h"
#include "solver_plugins/eigen_pose_graph_solver.hpp"

namespace
{

constexpr double kPi = 3.14159265358979323846;

int g_failures = 0;

void Check(bool condition, const char * what)
{
  std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
  std::fflush(stdout);
  if (!condition) {
    ++g_failures;
  }
}

double NormalizeAngle(double angle)
{
  while (angle > kPi) {
    angle -= 2.0 * kPi;
  }
  while (angle < -kPi) {
    angle += 2.0 * kPi;
  }
  return angle;
}

Eigen::Vector3d Compose(const Eigen::Vector3d & a, const Eigen::Vector3d & b)
{
  const double c = std::cos(a.z());
  const double s = std::sin(a.z());
  return Eigen::Vector3d(
    a.x() + c * b.x() - s * b.y(),
    a.y() + s * b.x() + c * b.y(),
    NormalizeAngle(a.z() + b.z()));
}

// Relative transform of `b` expressed in the frame of `a`.
Eigen::Vector3d Difference(const Eigen::Vector3d & a, const Eigen::Vector3d & b)
{
  const double c = std::cos(a.z());
  const double s = std::sin(a.z());
  const double dx = b.x() - a.x();
  const double dy = b.y() - a.y();
  return Eigen::Vector3d(c * dx + s * dy, -s * dx + c * dy, NormalizeAngle(b.z() - a.z()));
}

karto::Pose2 ToPose(const Eigen::Vector3d & p)
{
  return karto::Pose2(p.x(), p.y(), p.z());
}

karto::Matrix3 Covariance(double position_sigma, double yaw_sigma)
{
  karto::Matrix3 covariance;
  covariance(0, 0) = position_sigma * position_sigma;
  covariance(1, 1) = position_sigma * position_sigma;
  covariance(2, 2) = yaw_sigma * yaw_sigma;
  return covariance;
}

struct Graph
{
  std::vector<karto::LocalizedRangeScan *> scans;
  std::vector<karto::Vertex<karto::LocalizedRangeScan> *> vertices;
  std::vector<karto::Edge<karto::LocalizedRangeScan> *> edges;

  ~Graph()
  {
    for (auto * edge : edges) {
      delete edge;
    }
    for (auto * vertex : vertices) {
      delete vertex;
    }
    for (auto * scan : scans) {
      delete scan;
    }
  }

  void AddNode(int id, const Eigen::Vector3d & pose)
  {
    // Scans must be built through the sensor-name constructor. Karto's
    // `LaserRangeScan()` default constructor leaves m_pRangeReadings
    // uninitialised while `~LaserRangeScan()` calls `delete[]` on it, which
    // corrupts the heap. slam_toolbox never hits this because the mapper always
    // constructs scans from live readings.
    static const karto::Name sensor_name("laser");
    const karto::RangeReadingsVector readings{1.0, 1.0, 1.0};
    auto * scan = new karto::LocalizedRangeScan(sensor_name, readings);
    scan->SetUniqueId(id);
    scan->SetCorrectedPose(ToPose(pose));
    scans.push_back(scan);

    auto * vertex = new karto::Vertex<karto::LocalizedRangeScan>(scan);
    vertices.push_back(vertex);
  }

  // LinkInfo derives the measurement from the two poses it is handed, so
  // reconstruct the target pose from the measurement before feeding it.
  void AddConstraint(std::size_t from, std::size_t to, const Eigen::Vector3d & measurement)
  {
    const Eigen::Vector3d origin(0.0, 0.0, 0.0);
    const Eigen::Vector3d target = Compose(origin, measurement);
    auto * link = new karto::LinkInfo(
      ToPose(origin), ToPose(target), Covariance(0.05, 0.05));
    auto * edge = new karto::Edge<karto::LocalizedRangeScan>(vertices[from], vertices[to]);
    edge->SetLabel(link);
    edges.push_back(edge);
  }
};

std::vector<Eigen::Vector3d> SquareTruth()
{
  return {
    Eigen::Vector3d(0.0, 0.0, 0.0),
    Eigen::Vector3d(1.0, 0.0, 0.0),
    Eigen::Vector3d(1.0, 1.0, kPi / 2.0),
    Eigen::Vector3d(0.0, 1.0, kPi),
    Eigen::Vector3d(0.0, 0.0, -kPi / 2.0),
  };
}

std::vector<Eigen::Vector3d> SolveToPoses(
  const std::vector<Eigen::Vector3d> & start, const std::vector<Eigen::Vector3d> & truth,
  bool noisy_chain, double * loop_gap_out)
{
  const std::size_t count = truth.size();

  Graph graph;
  for (std::size_t i = 0; i < count; ++i) {
    graph.AddNode(static_cast<int>(i), start[i]);
  }

  solver_plugins::EigenPoseGraphSolver solver;
  for (auto * vertex : graph.vertices) {
    solver.AddNode(vertex);
  }

  for (std::size_t i = 0; i + 1 < count; ++i) {
    Eigen::Vector3d measurement = Difference(truth[i], truth[i + 1]);
    if (noisy_chain) {
      // A systematic error a real scan matcher would leave behind.
      measurement.x() += 0.01;
      measurement.z() = NormalizeAngle(measurement.z() + 0.02);
    }
    graph.AddConstraint(i, i + 1, measurement);
  }
  // Loop closure: the last scan sees the first one again, exactly.
  graph.AddConstraint(count - 1, 0, Difference(truth[count - 1], truth[0]));

  for (auto * edge : graph.edges) {
    solver.AddConstraint(edge);
  }
  solver.Compute();

  std::vector<Eigen::Vector3d> solved(count, Eigen::Vector3d::Zero());
  for (const auto & entry : solver.GetCorrections()) {
    const karto::Pose2 & pose = entry.second;
    solved[static_cast<std::size_t>(entry.first)] =
      Eigen::Vector3d(pose.GetX(), pose.GetY(), pose.GetHeading());
  }

  if (loop_gap_out != nullptr) {
    *loop_gap_out = (solved[count - 1].head<2>() - solved[0].head<2>()).norm();
  }

  double yaw = 0.0;
  solver.GetNodeOrientation(0, yaw);
  Check(std::fabs(NormalizeAngle(yaw - solved[0].z())) < 1e-12, "GetNodeOrientation");
  Check(solver.getGraph() != nullptr && solver.getGraph()->size() == count, "getGraph");
  Check(solver.GetCorrections().size() == count, "one correction per node");

  return solved;
}

void TestExactMeasurementsRecoverTruth()
{
  std::printf("exact measurements + slightly wrong odometry -> true geometry\n");

  const std::vector<Eigen::Vector3d> truth = SquareTruth();
  const std::size_t count = truth.size();

  // Odometry that is close but not right: a small heading and lateral bias that
  // grows along the chain.
  std::vector<Eigen::Vector3d> start = truth;
  for (std::size_t i = 0; i < count; ++i) {
    const double k = static_cast<double>(i);
    start[i].x() += 0.04 * k;
    start[i].y() -= 0.03 * k;
    start[i].z() = NormalizeAngle(start[i].z() + 0.05 * k);
  }

  const std::vector<Eigen::Vector3d> solved =
    SolveToPoses(start, truth, false, nullptr);

  double worst = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const double position_error = (solved[i].head<2>() - truth[i].head<2>()).norm();
    const double yaw_error = std::fabs(NormalizeAngle(solved[i].z() - truth[i].z()));
    worst = std::max(worst, std::max(position_error, yaw_error));
    std::printf(
      "  node %zu: odom (%.3f, %.3f, %.3f) -> solved (%.3f, %.3f, %.3f), truth (%.3f, %.3f, %.3f)\n",
      i, start[i].x(), start[i].y(), start[i].z(),
      solved[i].x(), solved[i].y(), solved[i].z(),
      truth[i].x(), truth[i].y(), truth[i].z());
  }
  std::printf("  worst error after solve: %.9f\n", worst);
  Check(worst < 1e-6, "solver recovers the exact geometry");
}

void TestLoopClosurePullsBackDrift()
{
  std::printf("drifting chain + loop closure -> the gap closes\n");

  const std::vector<Eigen::Vector3d> truth = SquareTruth();
  const std::size_t count = truth.size();

  // Dead reckoning: compose the same measurements the graph is given, exactly
  // what a front-end hands over before any optimisation.
  std::vector<Eigen::Vector3d> start(count, Eigen::Vector3d::Zero());
  for (std::size_t i = 0; i + 1 < count; ++i) {
    Eigen::Vector3d measurement = Difference(truth[i], truth[i + 1]);
    measurement.x() += 0.01;
    measurement.z() = NormalizeAngle(measurement.z() + 0.02);
    start[i + 1] = Compose(start[i], measurement);
  }
  const double dead_reckoned_gap = (start[count - 1].head<2>() - start[0].head<2>()).norm();

  double solved_gap = 0.0;
  const std::vector<Eigen::Vector3d> solved =
    SolveToPoses(start, truth, true, &solved_gap);

  const double dead_reckoned_error = (start[count - 1].head<2>() - truth[count - 1].head<2>()).norm();
  const double solved_error = (solved[count - 1].head<2>() - truth[count - 1].head<2>()).norm();

  std::printf(
    "  loop gap %.4f (dead reckoning) -> %.4f (solved)\n", dead_reckoned_gap, solved_gap);
  std::printf(
    "  final node error %.4f -> %.4f\n", dead_reckoned_error, solved_error);
  Check(dead_reckoned_gap > 0.05, "the dead-reckoned chain really is open");
  Check(solved_gap < 0.2 * dead_reckoned_gap, "loop closure gap shrinks by 5x or more");
  Check(solved_error < dead_reckoned_error, "final node moves closer to the truth");
}

void TestRemovalAndReset()
{
  std::printf("constraint and node removal\n");

  const std::vector<Eigen::Vector3d> truth = SquareTruth();

  Graph graph;
  for (std::size_t i = 0; i < truth.size(); ++i) {
    graph.AddNode(static_cast<int>(i), truth[i]);
  }

  solver_plugins::EigenPoseGraphSolver solver;
  for (auto * vertex : graph.vertices) {
    solver.AddNode(vertex);
  }
  for (std::size_t i = 0; i + 1 < truth.size(); ++i) {
    graph.AddConstraint(i, i + 1, Difference(truth[i], truth[i + 1]));
  }
  graph.AddConstraint(truth.size() - 1, 0, Difference(truth[truth.size() - 1], truth[0]));
  for (auto * edge : graph.edges) {
    solver.AddConstraint(edge);
  }

  solver.Compute();
  Check(solver.GetCorrections().size() == truth.size(), "solve before removal");

  solver.RemoveConstraint(4, 0);
  solver.Compute();
  Check(solver.GetCorrections().size() == truth.size(), "constraint removal keeps nodes");

  solver.RemoveNode(4);
  solver.Compute();
  Check(solver.GetCorrections().size() == truth.size() - 1, "node removal drops the node");

  solver.Reset();
  Check(solver.GetCorrections().empty(), "reset clears corrections");
  Check(solver.getGraph() != nullptr && solver.getGraph()->empty(), "reset clears the graph");
}

}  // namespace

int main()
{
  // Unbuffered: if a check trips a memory problem the output still reaches the
  // console instead of dying in the stdio buffer.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  TestExactMeasurementsRecoverTruth();
  TestLoopClosurePullsBackDrift();
  TestRemovalAndReset();

  if (g_failures == 0) {
    std::printf("ALL EIGEN POSE GRAPH SOLVER TESTS PASSED\n");
    return 0;
  }
  std::printf("%d CHECK(S) FAILED\n", g_failures);
  return 1;
}
