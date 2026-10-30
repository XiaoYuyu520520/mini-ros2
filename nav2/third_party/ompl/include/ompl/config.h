#ifndef OMPL_CONFIG_H
#define OMPL_CONFIG_H

// The OMPL subset this tree implements (see `third_party/ompl/README.md`).
//
// The version value is not decoration: nav2_smac_planner's
// `AnalyticExpansion::countDirectionChanges` picks between the OMPL 1.x
// signature (`ReedsSheppStateSpace::ReedsSheppPath`, `path.length_`) and the
// OMPL 2.x one (`PathType`, `path.length_` accessed through the class) with
// `#if OMPL_VERSION_VALUE >= 2000000`. Reporting the 1.x generation selects the
// older branch, which is the one this header set implements.
#define OMPL_VERSION_VALUE 10600
#define OMPL_VERSION "1.6.0-ros2_shim"

#endif  // OMPL_CONFIG_H
