tmux ls 2>&1
echo "=====sim pane====="
tmux capture-pane -p -t sim 2>&1 | grep -v '^$' | tail -8
echo "=====port 50051====="
(ss -ltnp 2>/dev/null || netstat -ltnp 2>/dev/null) | grep -E '50051|50111' || echo "neither port listening"
