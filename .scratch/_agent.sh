cat /tmp/ros2_peer/agent.log 2>&1 | tail -12
echo "=====PANE====="
tmux capture-pane -p -t ros2peer_agent 2>&1 | grep -v '^$' | tail -12
echo "=====PS====="
ps -eo args | grep -E 'ros2_grpc_sdk_agent' | grep -v grep
