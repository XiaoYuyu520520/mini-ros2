# ompl (the subset nav2_smac_planner needs)

`nav2_smac_planner` links against OMPL for one thing: the shortest path between
two poses for a car that cannot turn tighter than a fixed radius. Hybrid-A* uses
that as its heuristic (`distance_heuristic.cpp`) and to finish a search
analytically (`analytic_expansion_impl.hpp`); the state lattice uses it for its
heuristic too. Everything else in OMPL - the planners, the other state spaces,
the optimisers, `StateValidityChecker` - is unused here.

Upstream OMPL is a large dependency (it needs Boost and Eigen, and ships its own
build system). This port implements the API surface above instead, the same way
`third_party/laser_geometry` stands in for that package. Nothing in
`nav2_extracted/` is modified.

## What is implemented

| Header | What it provides |
| --- | --- |
| `ompl/config.h` | `OMPL_VERSION_VALUE`, used by the `#if` in `analytic_expansion.hpp` |
| `ompl/base/StateSpace.h` | `State` (a vector of reals) and `StateSpace` |
| `ompl/base/ScopedState.h` | `ScopedState<>` |
| `ompl/base/spaces/SE2StateSpace.h` | euclidean distance, linear interpolation |
| `ompl/base/spaces/DubinsStateSpace.h` | the six Dubins words, solved in closed form |
| `ompl/base/spaces/ReedsSheppStateSpace.h` | see the trade-off below |

`OMPL_VERSION_VALUE` reports the 1.x generation on purpose:
`AnalyticExpansion::countDirectionChanges()` picks between OMPL 1.x's
`ReedsSheppStateSpace::ReedsSheppPath` and OMPL 2.x's `PathType` with
`#if OMPL_VERSION_VALUE >= 2000000`, and the 1.x shapes are what these headers
declare (`PathType` is also aliased, so the newer branch would compile too).

## Dubins is exact, Reeds-Shepp is not

`DubinsStateSpace` solves all six words - the four `arc-straight-arc` ones from
the tangent between the two end circles, the two `arc-arc-arc` ones from the
circle that touches both - and returns the shortest. It is the same answer OMPL
gives.

`ReedsSheppStateSpace`, which may also drive in reverse, is **an approximation**.
Upstream searches the full family of 48 words. This port takes the shorter of

* the shortest forward Dubins path, and
* that same path driven from its far end (``reverse(Dubins(to -> from))``),
  which is the shortest way of reaching the goal without ever driving forwards.

Every plan it returns is a real, drivable Reeds-Shepp path, but a manoeuvre that
would need a direction change *inside* one word is planned as one reverse leg
instead, so it can be longer than upstream's optimum. The consequences:

* planning still succeeds - the path is valid and collision-checked like any
  other, it can simply take a wider line than upstream would;
* the heuristic can over-estimate the true Reeds-Shepp distance, and an
  over-estimating heuristic is not admissible, so A\* may return a slightly
  longer path than the optimum. It never returns a path that is not there.

`motion_model: "DUBIN"` in `SmacPlannerHybrid` avoids the question entirely and
is the recommended configuration for a differential-drive robot; use
`REEDS_SHEPP` when you want the planner to be able to back up, accepting that it
plans those legs the long way round.

## Conventions

* A path is a list of legs. A leg has a **signed length** - negative means the
  robot drives that leg backwards - and a **signed heading change**; their ratio
  is the curvature, and it does not change when a leg is driven the other way,
  because a steering angle does not know which way the wheels turn.
* A pose's heading is the robot's own heading, so a reverse leg moves against
  it. This is the convention a controller expects.
* Angles come out wrapped into `[0, 2*pi)`.

## It checks its own answers

Each solved word is turned back into a path, driven forward by `se2PoseAt()` and
compared with the pose that was asked for; a word that does not land on it is
not offered. Half of a right answer is worse than none: a word with a wrong
angle in it usually has a *short* wrong length, so it would win the minimum and
send the robot somewhere else.

## Tests

`nav2/ros2_shim/tests/ompl_spaces.cpp` (`ctest -R nav2_smac_ompl_spaces`) checks,
over 900 pose pairs at three turning radii:

* every word offered lands on the goal, to 1e-14 m;
* every word the geometry says can exist is offered - determined from the
  circles, not from the solver - so a word cannot be dropped quietly;
* integrating the interpolated path returns `distance()`, and no step turns
  tighter than the turning radius;
* Reeds-Shepp never costs more than Dubins, and its reverse leg starts and ends
  where it should;
* hand-checkable answers: straight lines, a half circle, and the loop a
  forward-only car needs to end up 1 m behind itself.
