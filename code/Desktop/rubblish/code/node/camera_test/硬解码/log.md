你这里已经找到关键了：你的 `libacl_dvpp.so` **不在**：

```bash
/usr/local/Ascend/ascend-toolkit/latest/lib64
```

而是在：

```bash
/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64
```

所以你之前的 `-L/usr/local/Ascend/ascend-toolkit/latest/lib64 -lascendcl` 只链接到了 `ascendcl`，没有链接到 DVPP 库。直接这样编译：✅

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh

g++ -std=c++17 -O2 -Wall -Wextra jpegd_single.cpp -o jpegd_single \
  -I/usr/local/Ascend/ascend-toolkit/latest/include \
  -L/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -L/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -lacl_dvpp \
  -lascendcl
```

注意：如果你的源码第一行已经有：

```cpp
#define ENABLE_DVPP_INTERFACE
```

编译命令里就**不要再加**：

```bash
-DENABLE_DVPP_INTERFACE
```

否则只是重复定义 warning，不影响但没必要。

---

如果还报 undefined reference，执行这个确认符号在不在库里：

```bash
nm -D /usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64/libacl_dvpp.so | grep acldvppJpegDecodeAsync
```

正常应该有输出。

也可以查一下库架构是不是对的：

```bash
uname -m
file /usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64/libacl_dvpp.so
```

如果你是在 Atlas 板子本机上编译，`uname -m` 大概率是：

```text
aarch64
```

那就应该用：

```bash
/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64/libacl_dvpp.so
```

不要用这些：

```text
devlib/x86_64
runtime/lib64/stub
runtime/lib64/stub/x86_64
```

这些是交叉编译或 stub 场景，不适合你现在板端直接跑硬解。

编译通过后运行：

```bash
./jpegd_single photos/1.jpg
```

循环测试：

```bash
./jpegd_single photos/1.jpg 100
```







root@davinci-mini:~/Desktop/code/node/camera/硬解码# source /usr/local/Ascend/ascend-toolkit/set_env.sh

g++ -std=c++17 -O2 -Wall -Wextra jpegd_single.cpp -o jpegd_single \
  -I/usr/local/Ascend/ascend-toolkit/latest/include \
  -L/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -L/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/lib64 \
  -Wl,-rpath,/usr/local/Ascend/ascend-toolkit/latest/aarch64-linux/lib64 \
  -lacl_dvpp \
  -lascendcl
root@davinci-mini:~/Desktop/code/node/camera/硬解码# ./jpegd_single photos/1.jpg
[INFO] input      : photos/1.jpg
[INFO] loop_count : 1
[INFO] jpeg size  : 246960 bytes
[INFO] jpeg info  : 1280x720, components=3
[INFO] out nv12   : 1280x720, stride=1280x720
[INFO] predicted output size : 1382400
[INFO] calculated output size: 1382400
[INFO] allocated output size : 1382400
[INFO] decode loops: 1
[INFO] total time  : 2.08632 ms
[INFO] avg time    : 2.08632 ms/frame
[INFO] approx fps  : 479.314
[OK] decode success
[OK] output full   : out/decoded_full.nv12
[OK] output packed : out/decoded_packed.nv12
[OK] meta          : out/meta.txt
root@davinci-mini:~/Desktop/code/node/camera/硬解码# ^C
root@davinci-mini:~/Desktop/code/node/camera/硬解码#

