#ifndef TF2__CONVERT_HPP_
#define TF2__CONVERT_HPP_

#include <type_traits>

namespace tf2
{

// Declared up front because impl::Converter below calls them. The concrete
// overloads (Pose/Transform/... in tf2_geometry_msgs) are found through ADL at
// instantiation time, which is why Converter calls them *unqualified*: a
// `tf2::toMsg(...)` qualified call would not consider them.
template<class A, class B>
  requires std::is_assignable_v<B &, const A &>
void fromMsg(const A & a, B & b);
template<class A, class B>
  requires std::is_assignable_v<B &, const A &>
void toMsg(const A & a, B & b);

namespace impl
{

// Upstream tf2 dispatches conversions on *whether each side is a ROS message*,
// and ported nav2 code spells the specialisation out by hand:
//
//   tf2::impl::Converter<true, false>::convert(msg.pose.pose, pose_old);
//
// so the shim has to provide the same two-bool form. <true, false> is
// "message -> tf2", <false, true> is "tf2 -> message"; the other two
// combinations are plain assignment (tf2 -> tf2, message -> message).
template<bool AIsMessage, bool BIsMessage>
struct Converter
{
  template<class A, class B>
  static void convert(const A & a, B & b)
  {
    if constexpr (AIsMessage && !BIsMessage) {
      fromMsg(a, b);
    } else if constexpr (!AIsMessage && BIsMessage) {
      toMsg(a, b);
    } else {
      b = a;
    }
  }
};

// The shim's own convenience path, used by `tf2::convert` when it is not told
// which side is a message: a pair with a concrete `fromMsg`/`toMsg` overload of
// its own - `tf2::convert(point, vector3)`, which is how the costmap's
// observation buffer turns a sensor origin into a tf2 vector - goes through
// that overload, and everything else is a plain copy.
//
// The overloads are found by ADL at instantiation time, so the concrete ones
// (tf2_geometry_msgs) only have to be visible in the translation unit that
// calls convert, not here; without them the pair is not convertible and the
// copy is skipped, exactly as before.
template<class A, class B>
struct TypeConverter
{
  static void convert(const A & a, B & b)
  {
    if constexpr (requires(const A & source, B & target) {fromMsg(source, target);}) {
      fromMsg(a, b);
    } else if constexpr (
      requires(const A & source, B & target) {target = toMsg(source);} &&
      std::is_assignable_v<B &, decltype(toMsg(a))>)
    {
      // tf2 -> message, which is how upstream spells it (`b = toMsg(a)`); the
      // concrete overloads mostly return the message rather than fill one in.
      b = toMsg(a);
    } else if constexpr (requires(const A & source, B & target) {toMsg(source, target);}) {
      toMsg(a, b);
    } else if constexpr (std::is_assignable_v<B &, const A &>) {
      b = a;
    } else {
      static_assert(
        std::is_assignable_v<B &, const A &>,
        "tf2::convert() has no fromMsg/toMsg overload and no assignment for this pair; "
        "upstream tf2 fails the same call at compile time");
    }
  }
};

}  // namespace impl

template<class A, class B>
void convert(const A & a, B & b)
{
  impl::TypeConverter<A, B>::convert(a, b);
}

// The same-type case is its own overload: upstream tf2 spells it out so that a
// message-to-message (or tf2-to-tf2) conversion is a copy. It is not just a
// formality -- the costmap's observation buffer turns its sensor origin into a
// pose with `tf2::convert(origin_point, origin_point)`, and a conversion that
// quietly does nothing leaves every observation raytracing from (0, 0, 0).
template<class A>
void convert(const A & a, A & b)
{
  if (&a != &b) {
    b = a;
  }
}

template<class A, class B>
B fromMsg(const A & a)
{
  B b;
  impl::TypeConverter<A, B>::convert(a, b);
  return b;
}

// Generic fallbacks: only usable when plain assignment is a valid conversion,
// so they never shadow (or compete with) the concrete overloads above.
template<class A, class B>
  requires std::is_assignable_v<B &, const A &>
void fromMsg(const A & a, B & b)
{
  b = a;
}

template<class A, class B>
  requires std::is_assignable_v<B &, const A &>
void toMsg(const A & a, B & b)
{
  b = a;
}

namespace impl
{
template<class A, class B>
struct ConverterToMsg
{
};
}  // namespace impl

template<class A>
auto toMsg(const A & a)
{
  return a;
}

}  // namespace tf2

#endif  // TF2__CONVERT_HPP_
