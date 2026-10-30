# -*- coding: utf-8 -*-
"""Which arm of the ``goal_msg.poses`` branch does this side take?

``NavigateThroughPoses.Goal().poses`` is a ``nav_msgs/Goals`` message with the
waypoints in its ``.goals`` field on the ported nav2 1.5.1 the shim runs, and a
plain ``geometry_msgs/PoseStamped[]`` list on Jazzy's real nav2. That is what
``src/racecar/scripts/*.py`` branch on, so this is the one line that says which
way each platform reads it. Run it both ways:

    python nav2/ros2_shim/python/shim_run.py .scratch/probe_ntp_poses.py
    ssh <ubuntu> 'source /opt/ros/jazzy/setup.bash && python3 - < .scratch/probe_ntp_poses.py'

The shim has to answer ``True`` and the car ``False``.
"""
from nav2_msgs.action import NavigateThroughPoses

poses = NavigateThroughPoses.Goal().poses
print("poses type   : %s.%s" % (type(poses).__module__, type(poses).__name__))
print("has .goals   : %s" % hasattr(poses, "goals"))
print("branch taken : %s" % ("goal_msg.poses.goals = poses" if hasattr(poses, "goals")
                            else "goal_msg.poses = poses"))
