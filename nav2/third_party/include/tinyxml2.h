#ifndef ROS2_SHIM__TINYXML2_HPP_
#define ROS2_SHIM__TINYXML2_HPP_

// This used to be a private copy of tinyxml2 9.0, and it shadowed BT.CPP's
// vendored tinyxml2 in every translation unit of nav2_behavior_tree - this
// directory (`NAV2_TP`) is on that target's include path *before* BT.CPP's own
// 3rdparty/tinyxml2. The binary nevertheless links `libtinyxml2.a` built from
// BT.CPP's copy, and the two versions disagree about the class they both
// describe: `XMLDocument` is 776 bytes under 9.0 and 880 under 11.0 (the
// embedded `DynArray<T, int>` became `DynArray<T, size_t>`, among others).
//
// Nothing about that mismatch can be caught at link time - `XMLDocument`'s
// methods mangle the same either way - so `XMLDocument doc; doc.LoadFile(...)`
// in `BehaviorTreeEngine::parseTreeInfo()` compiled those 776 bytes and let the
// library's `LoadFile` write all 880 through the pointer, straight over the
// caller's stack frame. The symptom was a hard access violation with no stack
// at all (`bt_navigator` died in `on_activate()` with 0xC0000005, and gdb
// reported only `0x1 in ?? ()`), which is why it survived so long: the fault is
// nowhere near the code that caused it.
//
// Forwarding to the header the linked library was built from keeps a second
// copy from reappearing.
#include "../dl/btcpp_master/BehaviorTree.CPP-master/3rdparty/tinyxml2/tinyxml2.h"

#endif  // ROS2_SHIM__TINYXML2_HPP_
