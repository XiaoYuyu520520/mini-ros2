// Closed-form Dubins words.
//
// Every word is solved in the frame that puts the starting pose at
// (0, 0, 0) with the heading along +x and measures lengths in units of the
// turning radius, which is what makes the six cases tractable. A word is then
// turned back into a `Se2Path`, driven forward by `se2PoseAt()` and kept only
// if it lands on the requested pose - the solution and its reconstruction are
// checked against each other rather than trusted.
#include "ompl/base/spaces/DubinsStateSpace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ompl
{
namespace base
{
namespace
{

using detail::mod2pi;
using detail::Se2Leg;
using detail::Se2Path;

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-9;

/// The goal pose in the frame of the start pose, in turning-radius units.
struct Normalised
{
  double gx{0.0};
  double gy{0.0};
  double heading{0.0};
};

/// One word, in normalised units.
struct Candidate
{
  int word{0};
  double first{0.0};   ///< angle of the leading arc
  double middle{0.0};  ///< straight length (CSC) or angle of the middle arc (CCC)
  double last{0.0};    ///< angle of the trailing arc
};

Normalised normalise(const State * from, const State * to, double rho)
{
  const double dx = to->reals[0] - from->reals[0];
  const double dy = to->reals[1] - from->reals[1];
  const double cosine = std::cos(from->reals[2]);
  const double sine = std::sin(from->reals[2]);

  Normalised problem;
  problem.gx = (dx * cosine + dy * sine) / rho;
  problem.gy = (-dx * sine + dy * cosine) / rho;
  problem.heading = mod2pi(to->reals[2] - from->reals[2]);
  return problem;
}

bool coincident(const State * from, const State * to)
{
  if (std::fabs(to->reals[0] - from->reals[0]) > kEps) {
    return false;
  }
  if (std::fabs(to->reals[1] - from->reals[1]) > kEps) {
    return false;
  }
  return std::fabs(mod2pi(to->reals[2] - from->reals[2] + kPi) - kPi) < kEps;
}

/// Centre of the circle a turn of the given direction sits on.
void turnCentre(double x, double y, double theta, bool left, double & cx, double & cy)
{
  double nx = 0.0;
  double ny = 0.0;
  detail::leftNormal(theta, nx, ny);
  const double sign = left ? 1.0 : -1.0;
  cx = x + sign * nx;
  cy = y + sign * ny;
}

/// Arc - straight - arc: the four words whose end circles have a common
/// external or internal tangent.
bool solveCsc(bool firstLeft, bool secondLeft, const Normalised & problem, Candidate & out)
{
  double c1x = 0.0;
  double c1y = 0.0;
  double c2x = 0.0;
  double c2y = 0.0;
  turnCentre(0.0, 0.0, 0.0, firstLeft, c1x, c1y);
  turnCentre(problem.gx, problem.gy, problem.heading, secondLeft, c2x, c2y);

  const double vx = c2x - c1x;
  const double vy = c2y - c1y;
  const double distance = std::hypot(vx, vy);

  if (firstLeft == secondLeft && distance <= kEps) {
    // The goal sits on the starting turn circle itself, so the word collapses
    // to one arc: drive round until the heading matches and the position comes
    // with it. This is not a rare curiosity - it is what a mirrored problem
    // produces - and without it the solver falls back to a full loop.
    out.word = firstLeft ? DubinsStateSpace::LSL : DubinsStateSpace::RSR;
    out.first = firstLeft ? mod2pi(problem.heading) : mod2pi(-problem.heading);
    out.middle = 0.0;
    out.last = 0.0;
    return true;
  }

  double psi = 0.0;
  double straight = 0.0;
  if (firstLeft == secondLeft) {
    // Both centres sit on the same side of the straight leg, so the leg runs
    // along the line joining them.
    psi = std::atan2(vy, vx);
    straight = distance;
  } else {
    // The centres sit on opposite sides and the leg is shorter than their
    // separation: v = p * u(psi) -+ 2 * n(psi), with the sign of 2 following
    // which turn comes second.
    if (distance < 2.0 - kEps) {
      return false;
    }
    straight = std::sqrt(std::max(0.0, distance * distance - 4.0));
    const double target = firstLeft ? -2.0 : 2.0;
    psi = std::atan2(vy, vx) - std::asin(target / distance);
  }

  if (firstLeft) {
    out.word = secondLeft ? DubinsStateSpace::LSL : DubinsStateSpace::LSR;
  } else {
    out.word = secondLeft ? DubinsStateSpace::RSL : DubinsStateSpace::RSR;
  }
  out.first = firstLeft ? mod2pi(psi) : mod2pi(-psi);
  out.middle = straight;
  out.last = secondLeft ? mod2pi(problem.heading - psi) : mod2pi(psi - problem.heading);
  return true;
}

/// Two-circle intersection; false when they do not meet.
bool circleIntersections(
  double ax, double ay, double ar, double bx, double by, double br, double out[4])
{
  const double dx = bx - ax;
  const double dy = by - ay;
  const double separation = std::hypot(dx, dy);
  if (separation <= kEps || separation > ar + br + kEps ||
    separation < std::fabs(ar - br) - kEps)
  {
    return false;
  }

  const double along = (ar * ar - br * br + separation * separation) / (2.0 * separation);
  const double halfChord = ar * ar - along * along;
  const double chord = halfChord > 0.0 ? std::sqrt(halfChord) : 0.0;
  const double mx = ax + along * dx / separation;
  const double my = ay + along * dy / separation;
  const double px = -dy * (chord / separation);
  const double py = dx * (chord / separation);

  out[0] = mx + px;
  out[1] = my + py;
  out[2] = mx - px;
  out[3] = my - py;
  return true;
}

/// Arc - arc - arc: the two words whose middle arc is tangent to both end
/// circles, which puts its centre two radii from each of them.
void solveCcc(bool firstLeft, const Normalised & problem, std::array<Candidate, 2> & out, int & count)
{
  double c1x = 0.0;
  double c1y = 0.0;
  double c3x = 0.0;
  double c3y = 0.0;
  turnCentre(0.0, 0.0, 0.0, firstLeft, c1x, c1y);
  turnCentre(problem.gx, problem.gy, problem.heading, firstLeft, c3x, c3y);

  double points[4];
  if (!circleIntersections(c1x, c1y, 2.0, c3x, c3y, 2.0, points)) {
    return;
  }

  for (int i = 0; i < 2; ++i) {
    const double c2x = points[2 * i];
    const double c2y = points[2 * i + 1];
    Candidate candidate;
    if (firstLeft) {
      // LRL: every junction heading is the normal pointing from the middle
      // centre back to the circle the robot hands over to.
      const double psi1 = std::atan2(c1y - c2y, c1x - c2x) - kPi / 2.0;
      const double psi2 = std::atan2(c3y - c2y, c3x - c2x) - kPi / 2.0;
      candidate.word = DubinsStateSpace::LRL;
      candidate.first = mod2pi(psi1);
      candidate.middle = mod2pi(psi1 - psi2);
      candidate.last = mod2pi(problem.heading - psi2);
    } else {
      // RLR: mirrored, so the junction normals point the other way and every
      // arc runs the other way round.
      const double psi1 = std::atan2(c2y - c1y, c2x - c1x) - kPi / 2.0;
      const double psi2 = std::atan2(c2y - c3y, c2x - c3x) - kPi / 2.0;
      candidate.word = DubinsStateSpace::RLR;
      candidate.first = mod2pi(-psi1);
      candidate.middle = mod2pi(psi2 - psi1);
      candidate.last = mod2pi(psi2 - problem.heading);
    }
    out[count++] = candidate;
  }
}

/// A word as legs. Left turns are positive, and a zero-length leg is dropped
/// so `count` stays the number of legs actually driven.
Se2Path buildPath(const Candidate & candidate, double rho)
{
  Se2Path path;
  const auto arc = [&](double turn) {
      if (std::fabs(turn) > kEps) {
        path.leg[path.count].length = std::fabs(turn) * rho;
        path.leg[path.count].turn = turn;
        ++path.count;
      }
    };
  const auto straight = [&](double length) {
      if (length > kEps) {
        path.leg[path.count].length = length * rho;
        path.leg[path.count].turn = 0.0;
        ++path.count;
      }
    };

  switch (candidate.word) {
    case DubinsStateSpace::LSL:
      arc(candidate.first); straight(candidate.middle); arc(candidate.last);
      break;
    case DubinsStateSpace::LSR:
      arc(candidate.first); straight(candidate.middle); arc(-candidate.last);
      break;
    case DubinsStateSpace::RSL:
      arc(-candidate.first); straight(candidate.middle); arc(candidate.last);
      break;
    case DubinsStateSpace::RSR:
      arc(-candidate.first); straight(candidate.middle); arc(-candidate.last);
      break;
    case DubinsStateSpace::RLR:
      arc(-candidate.first); arc(candidate.middle); arc(-candidate.last);
      break;
    case DubinsStateSpace::LRL:
      arc(candidate.first); arc(-candidate.middle); arc(candidate.last);
      break;
    default:
      break;
  }
  return path;
}

/// Drive the word and see where it ends up. A word whose algebra does not
/// reconstruct the goal is dropped: half of a right answer is worse than none.
bool landsOnGoal(const Se2Path & path, const Normalised & problem)
{
  const double start[3] = {0.0, 0.0, 0.0};
  double end[3];
  detail::se2PoseAt(path, start, path.length(), end);

  if (std::hypot(end[0] - problem.gx, end[1] - problem.gy) > 1e-6) {
    return false;
  }
  return std::fabs(mod2pi(end[2] - problem.heading + kPi) - kPi) < 1e-6;
}

void appendCandidates(const Normalised & problem, std::array<Candidate, 8> & out, int & count)
{
  Candidate candidate;
  if (solveCsc(true, true, problem, candidate)) {
    out[count++] = candidate;
  }
  if (solveCsc(true, false, problem, candidate)) {
    out[count++] = candidate;
  }
  if (solveCsc(false, true, problem, candidate)) {
    out[count++] = candidate;
  }
  if (solveCsc(false, false, problem, candidate)) {
    out[count++] = candidate;
  }

  std::array<Candidate, 2> threeArc{};
  int threeArcCount = 0;
  solveCcc(true, problem, threeArc, threeArcCount);
  for (int i = 0; i < threeArcCount; ++i) {
    out[count++] = threeArc[i];
  }
  threeArcCount = 0;
  solveCcc(false, problem, threeArc, threeArcCount);
  for (int i = 0; i < threeArcCount; ++i) {
    out[count++] = threeArc[i];
  }
}

}  // namespace

std::vector<DubinsStateSpace::WordSolution> DubinsStateSpace::solveWords(
  const State * from, const State * to) const
{
  std::vector<WordSolution> solutions;
  if (coincident(from, to)) {
    return solutions;
  }

  const Normalised problem = normalise(from, to, rho_);
  std::array<Candidate, 8> candidates{};
  int count = 0;
  appendCandidates(problem, candidates, count);

  for (int i = 0; i < count; ++i) {
    // Check the word where it was solved - in the frame of the start pose with
    // lengths in turning-radius units - and only then scale it to metres. The
    // goal in that frame is `problem`, so a path built in metres would be off
    // by the radius.
    const Se2Path normalised = buildPath(candidates[i], 1.0);
    if (!landsOnGoal(normalised, problem)) {
      continue;
    }

    WordSolution solution;
    solution.word = candidates[i].word;
    solution.path = normalised;
    for (int leg = 0; leg < solution.path.count; ++leg) {
      solution.path.leg[leg].length *= rho_;
    }
    solution.length = solution.path.length();
    solutions.push_back(solution);
  }

  std::sort(
    solutions.begin(), solutions.end(),
    [](const WordSolution & a, const WordSolution & b) {return a.word < b.word;});
  return solutions;
}

detail::Se2Solution DubinsStateSpace::shortestPath(const State * from, const State * to) const
{
  detail::Se2Solution solution;
  solution.start[0] = from->reals[0];
  solution.start[1] = from->reals[1];
  solution.start[2] = from->reals[2];

  const std::vector<WordSolution> solutions = solveWords(from, to);
  if (solutions.empty()) {
    // Either the poses coincide, or no word survived the landing check. An
    // empty path reports a length of zero, which is an admissible - if
    // pessimistic - heuristic rather than a wrong one.
    return solution;
  }

  const auto best = std::min_element(
    solutions.begin(), solutions.end(),
    [](const WordSolution & a, const WordSolution & b) {return a.length < b.length;});
  solution.path = best->path;
  solution.length = best->length;
  return solution;
}

double DubinsStateSpace::distance(const State * from, const State * to) const
{
  return shortestPath(from, to).length;
}

void DubinsStateSpace::interpolate(
  const State * from, const State * to, double t, State * state) const
{
  const detail::Se2Solution solution = shortestPath(from, to);
  double travelled = t * solution.length;
  if (travelled < 0.0) {
    travelled = 0.0;
  }
  if (travelled > solution.length) {
    travelled = solution.length;
  }

  double pose[3];
  detail::se2PoseAt(solution.path, solution.start, travelled, pose);
  state->reals[0] = pose[0];
  state->reals[1] = pose[1];
  state->reals[2] = pose[2];
}

}  // namespace base
}  // namespace ompl
