#ifndef TF2__LINEARMATH__SCALAR_HPP_
#define TF2__LINEARMATH__SCALAR_HPP_

// tf2Scalar is the global-scope scalar typedef that tf2 inherits from Bullet's
// btScalar, and it is spelled unqualified at call sites (`tf2Scalar yaw = ...`
// in slam_toolbox's merge_maps_kinematic). tf2 is built double-precision here;
// the single-precision variant upstream also allows is not modelled.

typedef double tf2Scalar;

#endif  // TF2__LINEARMATH__SCALAR_HPP_
