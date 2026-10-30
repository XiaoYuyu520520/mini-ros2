// The Dubins and Reeds-Shepp spaces are closed-form geometry, and a wrong
// angle in one of them does not fail loudly: it returns a path that is simply
// somewhere else, which nav2 then chases. So the properties checked here are
// the ones a wrong solution breaks:
//
//   1. every word the solver offers really does end at the goal, checked by
//      driving the path it returns - not by re-reading the algebra that
//      produced it,
//   2. every word the geometry says is feasible is offered. A word dropped
//      quietly shortens nothing but lengthens plans, and shows up only as
//      "the planner took a detour",
//   3. the interpolated path is arc-length parameterised: integrating it must
//      give back exactly `distance()`,
//   4. the interpolated path never turns tighter than the turning radius, which
//      is the one thing a Dubins car cannot do,
//   5. Reeds-Shepp never costs more than Dubins, and its reverse leg still
//      starts and ends where it was asked to.
//
// It runs without the shared-memory graph.
#include "ompl/base/ScopedState.h"
#include "ompl/base/spaces/DubinsStateSpace.h"
#include "ompl/base/spaces/ReedsSheppStateSpace.h"
#include "ompl/base/spaces/SE2StateSpace.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures;
  }
}

void section(const char * name)
{
  std::printf("%s\n", name);
}

bool close(double a, double b, double tolerance)
{
  return std::fabs(a - b) <= tolerance;
}

double wrapToPi(double angle)
{
  const double pi = 3.14159265358979323846;
  while (angle > pi) {
    angle -= 2.0 * pi;
  }
  while (angle < -pi) {
    angle += 2.0 * pi;
  }
  return angle;
}

ompl::base::State pose(double x, double y, double theta)
{
  ompl::base::State state;
  state.reals = {x, y, theta};
  return state;
}

double headingError(const ompl::base::State & a, const ompl::base::State & b)
{
  return std::fabs(wrapToPi(a.reals[2] - b.reals[2]));
}

double positionError(const ompl::base::State & a, const ompl::base::State & b)
{
  return std::hypot(a.reals[0] - b.reals[0], a.reals[1] - b.reals[1]);
}

/// A fixed sequence, so a failure is reproducible from the numbers in the log.
struct Random
{
  unsigned long long state{0x9E3779B97F4A7C15ULL};

  double next()
  {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>((state >> 11) & 0x1FFFFFFFFFFFFFULL) /
           static_cast<double>(0x1FFFFFFFFFFFFFULL);
  }

  double between(double low, double high)
  {
    return low + (high - low) * next();
  }
};

/// Centre of the circle a turn of the given direction sits on, in world
/// coordinates. This is the geometry the words are built from, derived here
/// from the pose rather than from the solver.
void turnCentre(
  const ompl::base::State & state, double rho, bool left, double centre[2])
{
  const double nx = -std::sin(state.reals[2]);
  const double ny = std::cos(state.reals[2]);
  const double sign = left ? 1.0 : -1.0;
  centre[0] = state.reals[0] + sign * rho * nx;
  centre[1] = state.reals[1] + sign * rho * ny;
}

/// Which of the six words can exist at all, decided from the circles:
///   - the two-arc words with a straight need their end circles to admit the
///     tangent the word uses (external for same-direction turns, internal -
///     which needs the centres at least two radii apart - otherwise),
///   - the three-arc words need the middle circle to touch both end circles,
///     so the end centres cannot be more than four radii apart.
/// `ok[6]` is indexed like `DubinsStateSpace::Word`.
void geometricFeasibility(
  const ompl::base::State & from, const ompl::base::State & to, double rho, bool ok[6])
{
  double startLeft[2];
  double startRight[2];
  double goalLeft[2];
  double goalRight[2];
  turnCentre(from, rho, true, startLeft);
  turnCentre(from, rho, false, startRight);
  turnCentre(to, rho, true, goalLeft);
  turnCentre(to, rho, false, goalRight);

  const auto separation = [](const double a[2], const double b[2]) {
      return std::hypot(a[0] - b[0], a[1] - b[1]);
    };

  // A same-direction word is always available: when the two centres coincide
  // the word collapses to a single arc.
  ok[ompl::base::DubinsStateSpace::LSL] = true;
  ok[ompl::base::DubinsStateSpace::RSR] = true;
  ok[ompl::base::DubinsStateSpace::LSR] = separation(startLeft, goalRight) >= 2.0 * rho;
  ok[ompl::base::DubinsStateSpace::RSL] = separation(startRight, goalLeft) >= 2.0 * rho;
  ok[ompl::base::DubinsStateSpace::RLR] = separation(startRight, goalRight) <= 4.0 * rho;
  ok[ompl::base::DubinsStateSpace::LRL] = separation(startLeft, goalLeft) <= 4.0 * rho;
}

/// True when a feasibility decision sits on its own boundary, where the two
/// circles are tangent and the closed form is ill-conditioned. Those pairs are
/// left out of the "every feasible word is offered" check rather than tuned
/// against.
bool nearBoundary(const ompl::base::State & from, const ompl::base::State & to, double rho)
{
  double startLeft[2];
  double startRight[2];
  double goalLeft[2];
  double goalRight[2];
  turnCentre(from, rho, true, startLeft);
  turnCentre(from, rho, false, startRight);
  turnCentre(to, rho, true, goalLeft);
  turnCentre(to, rho, false, goalRight);

  const auto separation = [](const double a[2], const double b[2]) {
      return std::hypot(a[0] - b[0], a[1] - b[1]);
    };
  const double slack = 1e-6 * rho;
  const double values[] = {
    separation(startLeft, goalRight) - 2.0 * rho,
    separation(startRight, goalLeft) - 2.0 * rho,
    separation(startRight, goalRight) - 4.0 * rho,
    separation(startLeft, goalLeft) - 4.0 * rho,
  };
  for (double value : values) {
    if (std::fabs(value) < slack) {
      return true;
    }
  }
  return false;
}

const char * wordName(int word)
{
  switch (word) {
    case ompl::base::DubinsStateSpace::LSL: return "LSL";
    case ompl::base::DubinsStateSpace::LSR: return "LSR";
    case ompl::base::DubinsStateSpace::RSL: return "RSL";
    case ompl::base::DubinsStateSpace::RSR: return "RSR";
    case ompl::base::DubinsStateSpace::RLR: return "RLR";
    default: return "LRL";
  }
}

double distanceBetween(
  const ompl::base::StateSpace & space, const ompl::base::State & from,
  const ompl::base::State & to)
{
  return space.distance(&from, &to);
}

ompl::base::State interpolate(
  const ompl::base::StateSpace & space, const ompl::base::State & from,
  const ompl::base::State & to, double t)
{
  ompl::base::State out;
  out.reals.resize(3);
  space.interpolate(&from, &to, t, &out);
  return out;
}

}  // namespace

int main()
{
  const double pi = 3.14159265358979323846;
  const double radii[] = {0.2, 1.0, 2.5};
  const ompl::base::State origin = pose(0.0, 0.0, 0.0);
  const int pairCount = 300;
  Random random;

  // 1. Every word offered has to land on the goal.
  {
    section("dubins: a word is only offered if it drives to the goal");
    int checked = 0;
    double worst = 0.0;
    bool allClose = true;
    for (double rho : radii) {
      ompl::base::DubinsStateSpace space(rho);
      for (int i = 0; i < pairCount; ++i) {
        const ompl::base::State from =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const ompl::base::State to =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const auto words = space.solveWords(&from, &to);
        expect(!words.empty(), "at least one word is offered for a random pair");
        for (const auto & word : words) {
          double start[3] = {from.reals[0], from.reals[1], from.reals[2]};
          double end[3];
          ompl::base::detail::se2PoseAt(word.path, start, word.path.length(), end);
          const ompl::base::State landed = pose(end[0], end[1], end[2]);
          const double position = positionError(landed, to);
          const double heading = headingError(landed, to);
          if (position > worst) {
            worst = position;
          }
          if (position > 1e-6 || heading > 1e-6) {
            allClose = false;
            std::printf(
              "  %s rho=%.2f from (%.3f %.3f %.3f) to (%.3f %.3f %.3f): off by %.3e m / %.3e rad\n",
              wordName(word.word), rho, from.reals[0], from.reals[1], from.reals[2],
              to.reals[0], to.reals[1], to.reals[2], position, heading);
          }
          ++checked;
        }
      }
    }
    expect(allClose, "every offered word reconstructs the goal pose");
    std::printf("  %d word solutions, worst landing error %.3e m\n", checked, worst);
  }

  // 2. A word that exists has to be offered.
  {
    section("dubins: every geometrically feasible word is offered");
    int pairs = 0;
    int missing = 0;
    int offered = 0;
    for (double rho : radii) {
      ompl::base::DubinsStateSpace space(rho);
      for (int i = 0; i < pairCount; ++i) {
        const ompl::base::State from =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const ompl::base::State to =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        if (nearBoundary(from, to, rho)) {
          continue;
        }
        bool feasible[6];
        geometricFeasibility(from, to, rho, feasible);
        bool seen[6] = {false, false, false, false, false, false};
        for (const auto & word : space.solveWords(&from, &to)) {
          seen[word.word] = true;
        }
        ++pairs;
        for (int word = 0; word < 6; ++word) {
          if (feasible[word]) {
            ++offered;
            if (!seen[word]) {
              ++missing;
              std::printf(
                "  %s rho=%.2f from (%.3f %.3f %.3f) to (%.3f %.3f %.3f) was not offered\n",
                wordName(word), rho, from.reals[0], from.reals[1], from.reals[2],
                to.reals[0], to.reals[1], to.reals[2]);
            }
          }
        }
      }
    }
    expect(missing == 0, "no feasible word is dropped");
    std::printf("  %d pairs, %d feasible words, %d missing\n", pairs, offered, missing);
  }

  // 3. Arc length and 4. turning radius, measured on the interpolated path.
  {
    section("dubins: the interpolated path is arc-length parameterised and never turns tighter than rho");
    const int samples = 2000;
    double worstLength = 0.0;
    double worstCurvature = 0.0;
    int arcs = 0;
    for (double rho : radii) {
      ompl::base::DubinsStateSpace space(rho);
      for (int i = 0; i < pairCount; ++i) {
        const ompl::base::State from =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const ompl::base::State to =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const double claimed = distanceBetween(space, from, to);

        double integrated = 0.0;
        ompl::base::State previous = from;
        for (int step = 1; step <= samples; ++step) {
          const ompl::base::State current =
            interpolate(space, from, to, static_cast<double>(step) / samples);
          const double chord = positionError(previous, current);
          const double turned = std::fabs(wrapToPi(current.reals[2] - previous.reals[2]));
          // On an arc the heading change times the radius is the distance
          // travelled; on a straight the chord is.
          integrated += turned > 1e-12 ? turned * rho : chord;
          if (turned > 1e-9 && chord > 1e-6) {
            const double curvature = turned / chord;
            if (curvature > worstCurvature) {
              worstCurvature = curvature;
            }
            ++arcs;
          }
          previous = current;
        }

        // Three steps can straddle a leg boundary and lose the part of the
        // step that belongs to the next leg.
        const double slack = 3.0 * claimed / samples + 1e-9;
        const double error = std::fabs(integrated - claimed);
        if (error > worstLength) {
          worstLength = error;
        }
        if (error > slack) {
          std::printf(
            "  rho=%.2f from (%.3f %.3f %.3f) to (%.3f %.3f %.3f): distance %.6f, integrated %.6f\n",
            rho, from.reals[0], from.reals[1], from.reals[2], to.reals[0], to.reals[1],
            to.reals[2], claimed, integrated);
        }
        expect(error <= slack, "integrating the path gives back distance()");
      }
    }
    expect(arcs > 0, "the sample actually contains arcs");
    // The chord of an arc is shorter than the arc, so the measured curvature is
    // never below 1/rho and only exceeds it by the sampling error.
    expect(
      worstCurvature <= 1.0 / radii[0] * (1.0 + 1e-3),
      "no interpolated step turns tighter than the turning radius");
    std::printf(
      "  worst length error %.3e m over %d pairs, worst curvature %.6f (1/rho = %.6f)\n",
      worstLength, pairCount * 3, worstCurvature, 1.0 / radii[0]);
  }

  // 5. Known values, small enough to check by hand.
  {
    section("dubins: hand-checkable answers");
    ompl::base::DubinsStateSpace unit(1.0);
    expect(close(distanceBetween(unit, origin, origin), 0.0, 1e-12), "a pose is zero away from itself");
    expect(close(distanceBetween(unit, origin, pose(1.0, 0.0, 0.0)), 1.0, 1e-9),
      "straight ahead 1 m is 1 m");
    expect(close(distanceBetween(unit, origin, pose(2.0, 0.0, 0.0)), 2.0, 1e-9),
      "straight ahead 2 m is 2 m");
    expect(close(distanceBetween(unit, origin, pose(0.0, 2.0, pi)), pi, 1e-9),
      "a half circle round (0, 1) is pi long");
    // A straight line has no curvature, so the radius must not enter the answer.
    ompl::base::DubinsStateSpace tight(0.2);
    expect(close(distanceBetween(tight, origin, pose(3.0, 0.0, 0.0)), 3.0, 1e-9),
      "the turning radius does not change a straight line");
    // The turnaround case: 1 m behind, heading unchanged, radius 1. Driving
    // forwards needs a loop of at least pi; the Reeds-Shepp reverse leg does it
    // in one straight metre.
    const ompl::base::State behind = pose(-1.0, 0.0, 0.0);
    // Round and round and back: a half circle, a straight metre west, a half
    // circle. 2 * pi + 1.
    expect(close(distanceBetween(unit, origin, behind), 2.0 * pi + 1.0, 1e-9),
      "driving 1 m backwards on a forward-only path costs a loop");
  }

  // 6. Reeds-Shepp.
  {
    section("reeds-shepp: never worse than dubins, and its reverse leg lands where it should");
    int reversed = 0;
    int checked = 0;
    double worstPosition = 0.0;
    double worstHeading = 0.0;
    double worstStart = 0.0;
    for (double rho : radii) {
      ompl::base::DubinsStateSpace forward(rho);
      ompl::base::ReedsSheppStateSpace reedsShepp(rho);
      for (int i = 0; i < pairCount; ++i) {
        const ompl::base::State from =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));
        const ompl::base::State to =
          pose(random.between(-10.0, 10.0), random.between(-10.0, 10.0),
          random.between(-pi, pi));

        const double backward = distanceBetween(reedsShepp, from, to);
        const double forwardOnly = distanceBetween(forward, from, to);
        expect(backward <= forwardOnly + 1e-9, "reversing costs nothing extra");

        const auto solution = reedsShepp.shortestPath(&from, &to);
        if (solution.reversed) {
          ++reversed;
        }
        const ompl::base::State start = pose(
          solution.start[0], solution.start[1], solution.start[2]);
        worstStart = std::max(worstStart, positionError(start, from));
        expect(positionError(start, from) < 1e-9, "the reversed path leaves from the start pose");

        const ompl::base::State end = interpolate(reedsShepp, from, to, 1.0);
        worstPosition = std::max(worstPosition, positionError(end, to));
        worstHeading = std::max(worstHeading, headingError(end, to));
        ++checked;
      }
    }
    expect(worstPosition < 1e-6, "interpolating to 1.0 reaches the goal position");
    expect(worstHeading < 1e-6, "interpolating to 1.0 reaches the goal heading");
    expect(reversed > 0, "the reverse branch is actually used");
    std::printf(
      "  %d pairs, %d planned in reverse, worst end error %.3e m / %.3e rad, worst start error %.3e m\n",
      checked, reversed, worstPosition, worstHeading, worstStart);

    ompl::base::ReedsSheppStateSpace reedsShepp(1.0);
    const ompl::base::State behind = pose(-1.0, 0.0, 0.0);
    expect(close(distanceBetween(reedsShepp, origin, behind), 1.0, 1e-9),
      "1 m behind is 1 m backwards");
    const auto path = reedsShepp.reedsShepp(&origin, &behind);
    expect(path.length_[0] < 0.0, "the leg that backs up is reported as a negative length");
    expect(close(path.length_[1], 0.0, 1e-12) && close(path.length_[2], 0.0, 1e-12),
      "the reverse leg is the whole path");
  }

  // 7. SE2StateSpace: the holonomic space used for lattice omni heuristics.
  {
    section("se2: euclidean distance and shortest-angle interpolation");
    ompl::base::SE2StateSpace space;
    expect(close(distanceBetween(space, origin, pose(3.0, 4.0, 2.0)), 5.0, 1e-12),
      "distance ignores the heading");
    const ompl::base::State half = interpolate(space, origin, pose(2.0, 4.0, pi), 0.5);
    expect(close(half.reals[0], 1.0, 1e-12) && close(half.reals[1], 2.0, 1e-12),
      "positions interpolate linearly");
    // pi and -pi are the same heading, so going between them must not spin all
    // the way round.
    const ompl::base::State across = interpolate(space, origin, pose(0.0, 0.0, pi), 0.5);
    expect(close(std::fabs(wrapToPi(across.reals[2])), pi / 2.0, 1e-12),
      "headings take the short way round");
  }

  std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
