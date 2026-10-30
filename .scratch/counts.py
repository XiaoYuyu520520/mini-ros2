import io,re,os
def types(p, pat):
    t=io.open(p,encoding="utf8",errors="replace").read()
    s=sorted(set(re.findall(r'"([A-Za-z0-9_]+/'+pat+r'/[A-Za-z0-9_]+)"', t)))
    return s
a=types("bridge/windows/src/type_registry.cpp","msg")
b=types("bridge/windows/src/service_registry.cpp","srv")
c=types("bridge/windows/src/action_registry.cpp","action")
print("topic types:",len(a)); print(a)
print("services:",len(b)); print(b)
print("actions:",len(c)); print(c)
pr=io.open("gRPC/ros2_gRPC_sdk/proto/ros2_bridge.proto",encoding="utf8").read()
rpcs=re.findall(r"^\s*rpc\s+(\w+)",pr,re.M)
print("proto RPCs:",len(rpcs)); print(rpcs)
gen="nav2/ros2_shim/generated/include"
print("generated pkgs:",len([d for d in os.listdir(gen) if os.path.isdir(os.path.join(gen,d))]))
print(sorted(os.listdir(gen)))
for pat in ["rclpy","rosbag2","launch_ros"]:
    hit=False
    for dp,dn,fn in os.walk("."):
        if any(x in dp.split(os.sep) for x in ("build",".git","nav2_extracted")): continue
        if pat in dn: hit=True; print("DIR",pat,dp); break
    if not hit: print("no dir named",pat)