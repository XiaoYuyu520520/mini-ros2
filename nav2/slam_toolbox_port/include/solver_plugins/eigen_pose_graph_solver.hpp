#ifndef SOLVER_PLUGINS__EIGEN_POSE_GRAPH_SOLVER_HPP_
#define SOLVER_PLUGINS__EIGEN_POSE_GRAPH_SOLVER_HPP_

// A karto::ScanSolver for the Windows port of slam_toolbox.
//
// slam_toolbox ships exactly one solver plugin -- `solver_plugins::CeresSolver`
// -- and Ceres drags in SuiteSparse, glog, gflags, OpenBLAS and TBB. On Windows
// those exist only as MSYS2 DLLs, which is incompatible with this port's goal of
// self-contained statically linked executables. Eigen is header-only, so the
// pose-graph optimisation is reimplemented here directly and the whole SLAM
// stack stays free of third-party run-time DLLs.
//
// The problem is the same one CeresSolver builds, and the residual matches
// `solvers::PoseGraph2dErrorTerm` (solvers/ceres_utils.h) exactly:
//
//   r = [ R(yaw_a)^T (p_b - p_a) - p_ab ; wrap(yaw_b - yaw_a - yaw_ab) ]
//   r <- sqrt_information * r
//
// Jacobians are evaluated by central differences. The graphs slam_toolbox builds
// during a mapping session are small (one node per throttled scan plus a handful
// of loop closures), so the six extra residual evaluations per edge cost far
// less than the risk of hand-deriving derivatives for the rotation term. The
// normal equations are assembled sparsely and solved by a sparse LDL^T
// factorisation under Levenberg-Marquardt damping.

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <Eigen/Core>
#include <Eigen/SparseCore>

#include "karto_sdk/Mapper.h"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"

namespace solver_plugins
{

class EigenPoseGraphSolver : public karto::ScanSolver
{
public:
  EigenPoseGraphSolver();
  ~EigenPoseGraphSolver() override;

  // -- karto::ScanSolver interface ------------------------------------------
  void Configure(rclcpp_lifecycle::LifecycleNode::SharedPtr node) override;
  const karto::ScanSolver::IdPoseVector & GetCorrections() const override;
  void Compute() override;
  void Clear() override;
  void Reset() override;

  void AddNode(karto::Vertex<karto::LocalizedRangeScan> * pVertex) override;
  void AddConstraint(karto::Edge<karto::LocalizedRangeScan> * pEdge) override;
  void RemoveNode(kt_int32s id) override;
  void RemoveConstraint(kt_int32s sourceId, kt_int32s targetId) override;

  std::unordered_map<int, Eigen::Vector3d> * getGraph() override;
  void ModifyNode(const int & unique_id, Eigen::Vector3d pose) override;
  void GetNodeOrientation(const int & unique_id, double & pose) override;

private:
  struct Constraint
  {
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    int source_id = 0;
    int target_id = 0;
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    Eigen::Matrix3d sqrt_information = Eigen::Matrix3d::Identity();
  };

  // A constraint paired with the positions of its endpoints in the node table.
  struct Edge
  {
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW
    const Constraint * constraint = nullptr;
    int node_a = 0;
    int node_b = 0;
  };

  // Whitened measurement residual for the given estimate.
  static Eigen::Vector3d Residual(
    const Eigen::Vector3d & a, const Eigen::Vector3d & b, const Constraint & c);

  // Accumulate the normal equations and the cost for the given estimate.
  // `free_index[i]` is the offset of node i in the reduced parameter vector, or
  // -1 when the node is the gauge anchor and therefore held constant.
  void BuildSystem(
    const Eigen::VectorXd & x, const std::vector<int> & free_index,
    const std::vector<Edge, Eigen::aligned_allocator<Edge>> & edges,
    Eigen::SparseMatrix<double> & hessian, Eigen::VectorXd & gradient,
    double & cost) const;

  void WriteBack(const Eigen::VectorXd & x, const std::vector<int> & node_ids);

  // Nodes are keyed by karto unique id. getGraph() and GetNodeOrientation()
  // hand out references into this map, so entries are never invalidated.
  std::unordered_map<int, Eigen::Vector3d> nodes_;
  std::vector<Constraint, Eigen::aligned_allocator<Constraint>> constraints_;
  karto::ScanSolver::IdPoseVector corrections_;

  rclcpp::Logger logger_{rclcpp::get_logger("EigenPoseGraphSolver")};

  int max_iterations_ = 50;
  double convergence_tolerance_ = 1e-8;
  bool debug_logging_ = false;

  mutable std::mutex mutex_;
};

}  // namespace solver_plugins

#endif  // SOLVER_PLUGINS__EIGEN_POSE_GRAPH_SOLVER_HPP_
