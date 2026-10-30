#include "solver_plugins/eigen_pose_graph_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include <Eigen/Cholesky>
#include <Eigen/SparseCholesky>

#include "pluginlib/class_list_macros.hpp"

namespace solver_plugins
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double NormalizeAngle(double angle)
{
  while (angle > kPi) {
    angle -= kTwoPi;
  }
  while (angle < -kPi) {
    angle += kTwoPi;
  }
  return angle;
}

Eigen::Matrix3d ToEigen(const karto::Matrix3 & matrix)
{
  Eigen::Matrix3d out;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      out(row, col) = matrix(row, col);
    }
  }
  return out;
}

}  // namespace


EigenPoseGraphSolver::EigenPoseGraphSolver() = default;

EigenPoseGraphSolver::~EigenPoseGraphSolver() = default;

/*****************************************************************************/
void EigenPoseGraphSolver::Configure(rclcpp_lifecycle::LifecycleNode::SharedPtr node)
/*****************************************************************************/
{
  logger_ = node->get_logger();

  if (!node->has_parameter("solver_max_iterations")) {
    node->declare_parameter("solver_max_iterations", max_iterations_);
  }
  max_iterations_ = static_cast<int>(node->get_parameter("solver_max_iterations").as_int());

  if (!node->has_parameter("solver_convergence_tolerance")) {
    node->declare_parameter("solver_convergence_tolerance", convergence_tolerance_);
  }
  convergence_tolerance_ = node->get_parameter("solver_convergence_tolerance").as_double();

  // slam_toolbox declares and reads `debug_logging` itself, but Configure() is
  // called before that happens, so tolerate an undeclared parameter here.
  if (!node->has_parameter("debug_logging")) {
    node->declare_parameter("debug_logging", debug_logging_);
  }
  debug_logging_ = node->get_parameter("debug_logging").as_bool();

  corrections_.clear();
  RCLCPP_INFO(
    logger_, "EigenPoseGraphSolver: configured with %d max iterations.", max_iterations_);
}

/*****************************************************************************/
Eigen::Vector3d EigenPoseGraphSolver::Residual(
  const Eigen::Vector3d & a, const Eigen::Vector3d & b, const Constraint & c)
/*****************************************************************************/
{
  const double cos_a = std::cos(a.z());
  const double sin_a = std::sin(a.z());
  const double dx = b.x() - a.x();
  const double dy = b.y() - a.y();

  Eigen::Vector3d residual;
  residual.x() = cos_a * dx + sin_a * dy - c.mean.x();
  residual.y() = -sin_a * dx + cos_a * dy - c.mean.y();
  residual.z() = NormalizeAngle(b.z() - a.z() - c.mean.z());
  return c.sqrt_information * residual;
}

/*****************************************************************************/
void EigenPoseGraphSolver::BuildSystem(
  const Eigen::VectorXd & x, const std::vector<int> & free_index,
  const std::vector<Edge, Eigen::aligned_allocator<Edge>> & edges,
  Eigen::SparseMatrix<double> & hessian, Eigen::VectorXd & gradient,
  double & cost) const
/*****************************************************************************/
{
  int dofs = 0;
  for (const int offset : free_index) {
    if (offset >= 0) {
      dofs += 3;
    }
  }

  std::vector<Eigen::Triplet<double>> triplets;
  triplets.reserve(edges.size() * 36);
  hessian.resize(dofs, dofs);
  gradient = Eigen::VectorXd::Zero(dofs);
  cost = 0.0;

  // Central-difference step for the rotation term; positions are O(100 m) at
  // most in a mapping session, so 1e-6 keeps truncation well below the
  // measurement noise while staying clear of round-off.
  const double step = 1e-6;

  for (const Edge & edge : edges) {
    const Constraint & c = *edge.constraint;

    const Eigen::Vector3d pose_a = x.segment<3>(3 * edge.node_a);
    const Eigen::Vector3d pose_b = x.segment<3>(3 * edge.node_b);

    double local[6] = {
      pose_a.x(), pose_a.y(), pose_a.z(),
      pose_b.x(), pose_b.y(), pose_b.z()};

    Eigen::Matrix<double, 3, 6> jacobian;
    for (int col = 0; col < 6; ++col) {
      local[col] += step;
      Eigen::Vector3d plus;
      plus << local[0], local[1], local[2];
      Eigen::Vector3d plus_b;
      plus_b << local[3], local[4], local[5];
      const Eigen::Vector3d forward = Residual(plus, plus_b, c);

      local[col] -= 2.0 * step;
      Eigen::Vector3d minus;
      minus << local[0], local[1], local[2];
      Eigen::Vector3d minus_b;
      minus_b << local[3], local[4], local[5];
      const Eigen::Vector3d backward = Residual(minus, minus_b, c);

      local[col] += step;
      jacobian.col(col) = (forward - backward) / (2.0 * step);
    }

    const Eigen::Vector3d residual = Residual(pose_a, pose_b, c);
    cost += residual.squaredNorm();

    // Hessian contribution J^T J and gradient contribution J^T r, with the
    // gauge anchor's three dofs projected out.
    for (int i = 0; i < 6; ++i) {
      const int dof_i = free_index[i < 3 ? edge.node_a : edge.node_b];
      if (dof_i < 0) {
        continue;
      }
      for (int j = 0; j < 6; ++j) {
        const int dof_j = free_index[j < 3 ? edge.node_a : edge.node_b];
        if (dof_j < 0) {
          continue;
        }
        double value = 0.0;
        for (int row = 0; row < 3; ++row) {
          value += jacobian(row, i) * jacobian(row, j);
        }
        if (value != 0.0) {
          triplets.emplace_back(dof_i + (i % 3), dof_j + (j % 3), value);
        }
      }

      double entry = 0.0;
      for (int row = 0; row < 3; ++row) {
        entry += jacobian(row, i) * residual(row);
      }
      gradient(dof_i + (i % 3)) += entry;
    }
  }

  hessian.setFromTriplets(triplets.begin(), triplets.end());
}

/*****************************************************************************/
const karto::ScanSolver::IdPoseVector & EigenPoseGraphSolver::GetCorrections() const
/*****************************************************************************/
{
  return corrections_;
}

/*****************************************************************************/
void EigenPoseGraphSolver::Clear()
/*****************************************************************************/
{
  corrections_.clear();
}

/*****************************************************************************/
void EigenPoseGraphSolver::Reset()
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  corrections_.clear();
  nodes_.clear();
  constraints_.clear();
}

/*****************************************************************************/
void EigenPoseGraphSolver::AddNode(karto::Vertex<karto::LocalizedRangeScan> * pVertex)
/*****************************************************************************/
{
  if (!pVertex || !pVertex->GetObject()) {
    return;
  }

  const karto::Pose2 pose = pVertex->GetObject()->GetCorrectedPose();
  const int id = pVertex->GetObject()->GetUniqueId();

  std::lock_guard<std::mutex> lock(mutex_);
  nodes_[id] = Eigen::Vector3d(pose.GetX(), pose.GetY(), pose.GetHeading());
}

/*****************************************************************************/
void EigenPoseGraphSolver::AddConstraint(karto::Edge<karto::LocalizedRangeScan> * pEdge)
/*****************************************************************************/
{
  if (!pEdge || !pEdge->GetSource() || !pEdge->GetTarget()) {
    return;
  }

  const int source_id = pEdge->GetSource()->GetObject()->GetUniqueId();
  const int target_id = pEdge->GetTarget()->GetObject()->GetUniqueId();
  if (source_id == target_id) {
    return;
  }

  auto * link = dynamic_cast<karto::LinkInfo *>(pEdge->GetLabel());
  if (link == nullptr) {
    return;
  }

  const karto::Pose2 difference = link->GetPoseDifference();
  const Eigen::Matrix3d information = ToEigen(link->GetCovariance().Inverse());

  // The information matrix must be positive definite for the Cholesky factor to
  // be real. Karto occasionally hands back a degenerate covariance for a scan
  // that never locked on, so fall back to an unweighted constraint.
  Eigen::Matrix3d sqrt_information = Eigen::Matrix3d::Identity();
  const Eigen::LLT<Eigen::Matrix3d> llt(information);
  if (llt.info() == Eigen::Success) {
    sqrt_information = llt.matrixU();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (nodes_.find(source_id) == nodes_.end() || nodes_.find(target_id) == nodes_.end()) {
    return;
  }

  // Karto re-adds an edge whenever it refines the covariance between the same
  // pair of scans; keep one constraint per pair so the normal equations stay
  // well formed.
  for (Constraint & existing : constraints_) {
    if ((existing.source_id == source_id && existing.target_id == target_id) ||
      (existing.source_id == target_id && existing.target_id == source_id))
    {
      existing.source_id = source_id;
      existing.target_id = target_id;
      existing.mean = Eigen::Vector3d(
        difference.GetX(), difference.GetY(), difference.GetHeading());
      existing.sqrt_information = sqrt_information;
      return;
    }
  }

  Constraint constraint;
  constraint.source_id = source_id;
  constraint.target_id = target_id;
  constraint.mean = Eigen::Vector3d(
    difference.GetX(), difference.GetY(), difference.GetHeading());
  constraint.sqrt_information = sqrt_information;
  constraints_.push_back(constraint);
}

/*****************************************************************************/
void EigenPoseGraphSolver::RemoveNode(kt_int32s id)
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  nodes_.erase(id);
  constraints_.erase(
    std::remove_if(
      constraints_.begin(), constraints_.end(),
      [id](const Constraint & c) {
        return c.source_id == id || c.target_id == id;
      }),
    constraints_.end());
}

/*****************************************************************************/
void EigenPoseGraphSolver::RemoveConstraint(kt_int32s sourceId, kt_int32s targetId)
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  constraints_.erase(
    std::remove_if(
      constraints_.begin(), constraints_.end(),
      [sourceId, targetId](const Constraint & c) {
        return (c.source_id == sourceId && c.target_id == targetId) ||
               (c.source_id == targetId && c.target_id == sourceId);
      }),
    constraints_.end());
}

/*****************************************************************************/
void EigenPoseGraphSolver::ModifyNode(const int & unique_id, Eigen::Vector3d pose)
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = nodes_.find(unique_id);
  if (it == nodes_.end()) {
    return;
  }
  // Callers pass an incremental yaw measured in the marker frame, not an
  // absolute heading -- same contract as CeresSolver::ModifyNode.
  const double yaw = it->second.z();
  it->second = pose;
  it->second.z() += yaw;
}

/*****************************************************************************/
void EigenPoseGraphSolver::GetNodeOrientation(const int & unique_id, double & pose)
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = nodes_.find(unique_id);
  if (it != nodes_.end()) {
    pose = it->second.z();
  }
}

/*****************************************************************************/
std::unordered_map<int, Eigen::Vector3d> * EigenPoseGraphSolver::getGraph()
/*****************************************************************************/
{
  return &nodes_;
}

/*****************************************************************************/
void EigenPoseGraphSolver::WriteBack(
  const Eigen::VectorXd & x, const std::vector<int> & node_ids)
/*****************************************************************************/
{
  corrections_.clear();
  corrections_.reserve(node_ids.size());

  karto::Pose2 pose;
  for (std::size_t i = 0; i < node_ids.size(); ++i) {
    const Eigen::Vector3d value = x.segment<3>(3 * static_cast<int>(i));
    nodes_[node_ids[i]] = value;
    pose.SetX(value.x());
    pose.SetY(value.y());
    pose.SetHeading(NormalizeAngle(value.z()));
    corrections_.push_back(std::make_pair(node_ids[i], pose));
  }
}

/*****************************************************************************/
void EigenPoseGraphSolver::Compute()
/*****************************************************************************/
{
  std::lock_guard<std::mutex> lock(mutex_);
  corrections_.clear();

  if (nodes_.size() < 2) {
    return;
  }

  // Deterministic ordering: ascending karto id, which is insertion order for
  // scans. The lowest id becomes the gauge anchor, matching the fixed first
  // node CeresSolver pins.
  std::vector<int> node_ids;
  node_ids.reserve(nodes_.size());
  for (const auto & entry : nodes_) {
    node_ids.push_back(entry.first);
  }
  std::sort(node_ids.begin(), node_ids.end());

  const int count = static_cast<int>(node_ids.size());
  std::unordered_map<int, int> position;
  position.reserve(count);
  for (int i = 0; i < count; ++i) {
    position[node_ids[i]] = i;
  }

  std::vector<Edge, Eigen::aligned_allocator<Edge>> edges;
  edges.reserve(constraints_.size());
  for (const Constraint & c : constraints_) {
    auto source = position.find(c.source_id);
    auto target = position.find(c.target_id);
    if (source == position.end() || target == position.end() || source == target) {
      continue;
    }
    Edge edge;
    edge.constraint = &c;
    edge.node_a = source->second;
    edge.node_b = target->second;
    edges.push_back(edge);
  }

  Eigen::VectorXd x(3 * count);
  for (int i = 0; i < count; ++i) {
    x.segment<3>(3 * i) = nodes_[node_ids[i]];
  }

  if (edges.empty()) {
    WriteBack(x, node_ids);
    return;
  }

  std::vector<int> free_index(count, -1);
  int dofs = 0;
  for (int i = 1; i < count; ++i) {
    free_index[i] = dofs;
    dofs += 3;
  }

  Eigen::SparseMatrix<double> hessian;
  Eigen::VectorXd gradient;
  double cost = 0.0;
  BuildSystem(x, free_index, edges, hessian, gradient, cost);
  const double reference_cost = cost > 0.0 ? cost : 1.0;

  // Levenberg-Marquardt. A rejected trial is cheap and must not consume the
  // iteration budget: only accepted steps count towards max_iterations_, while a
  // hard cap on total trials stops a pathological graph from spinning. Counting
  // trials instead was what made a badly dead-reckoned chain stall far from the
  // optimum -- the first few full Gauss-Newton steps overshoot, and the
  // rejections that follow were eating the whole budget.
  double lambda = 1e-3;
  int accepted = 0;
  int trials = 0;
  const int max_trials = 20 * max_iterations_ + 100;

  while (accepted < max_iterations_ && trials < max_trials) {
    ++trials;

    Eigen::SparseMatrix<double> damped = hessian;
    for (int k = 0; k < dofs; ++k) {
      const double diagonal = hessian.coeff(k, k);
      damped.coeffRef(k, k) += lambda * (diagonal > 0.0 ? diagonal : 1.0) + 1e-12;
    }

    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> factorization;
    factorization.compute(damped);
    if (factorization.info() != Eigen::Success) {
      lambda *= 10.0;
      if (lambda > 1e12) {
        break;
      }
      continue;
    }

    const Eigen::VectorXd delta = factorization.solve(-gradient);
    if (factorization.info() != Eigen::Success || !delta.allFinite()) {
      lambda *= 10.0;
      if (lambda > 1e12) {
        break;
      }
      continue;
    }

    Eigen::VectorXd candidate = x;
    for (int i = 0; i < count; ++i) {
      if (free_index[i] < 0) {
        continue;
      }
      candidate.segment<3>(3 * i) += delta.segment<3>(free_index[i]);
      candidate(3 * i + 2) = NormalizeAngle(candidate(3 * i + 2));
    }

    Eigen::SparseMatrix<double> candidate_hessian;
    Eigen::VectorXd candidate_gradient;
    double candidate_cost = 0.0;
    BuildSystem(candidate, free_index, edges, candidate_hessian, candidate_gradient, candidate_cost);

    if (candidate_cost < cost) {
      const double improvement = cost - candidate_cost;
      x = candidate;
      cost = candidate_cost;
      hessian = candidate_hessian;
      gradient = candidate_gradient;
      ++accepted;
      lambda = lambda * 0.3 > 1e-12 ? lambda * 0.3 : 1e-12;
      if (improvement < convergence_tolerance_ * reference_cost) {
        break;
      }
    } else {
      lambda *= 10.0;
      if (lambda > 1e12) {
        break;
      }
    }
  }

  WriteBack(x, node_ids);

  if (debug_logging_) {
    RCLCPP_INFO(
      logger_, "EigenPoseGraphSolver: %d nodes, %d constraints, final cost %.6f.",
      count, static_cast<int>(edges.size()), cost);
  }
}

}  // namespace solver_plugins

#include "pluginlib/class_loader.hpp"

// slam_toolbox requests `solver_plugins::CeresSolver` -- that name is baked into
// its shipped configuration files and into SlamToolbox::setSolver()'s default --
// so the Eigen replacement has to answer to it. PLUGINLIB_EXPORT_CLASS would
// register the unqualified class name (EigenPoseGraphSolver) instead, and the
// shim's registry is keyed by exactly that.
namespace
{
struct EigenPoseGraphSolverRegistrar
{
  EigenPoseGraphSolverRegistrar()
  {
    ::pluginlib::detail::register_plugin<
      solver_plugins::EigenPoseGraphSolver, karto::ScanSolver>(
      "solver_plugins::CeresSolver");
  }
};

const EigenPoseGraphSolverRegistrar g_eigen_pose_graph_solver_registrar;
}  // namespace
