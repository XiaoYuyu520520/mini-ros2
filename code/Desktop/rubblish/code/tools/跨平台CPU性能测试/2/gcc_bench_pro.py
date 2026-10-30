import os
import sys
import time
import shutil
import tempfile
import subprocess
from concurrent.futures import ThreadPoolExecutor

# =====================================================================
# 可配置参数区
# =====================================================================
COMPILER_NATIVE = "gcc"                     # 默认本地编译器
COMPILER_ARM = "aarch64-linux-gnu-gcc"      # -arm 参数对应的交叉编译器
COMPILER_X64 = "x86_64-w64-mingw32-gcc"     # -x64 参数对应的交叉编译器

CFLAGS = ["-O2", "-c"]                      # 编译参数
NUM_FILES = 16                              # 源文件数量 (设为核心数的倍数最佳)
FUNCTIONS_PER_FILE = 3000                   # 每个文件的函数量
CLEAN_UP = True                             # 测试结束后是否清理
# =====================================================================

def get_compiler():
    """解析命令行参数，决定使用的编译器"""
    target_cmd = COMPILER_NATIVE
    if "-arm" in sys.argv:
        target_cmd = COMPILER_ARM
    elif "-x64" in sys.argv:
        target_cmd = COMPILER_X64
        
    # 检查目标编译器是否存在
    if shutil.which(target_cmd) is None:
        print(f"⚠️  警告: 找不到指定的编译器 '{target_cmd}'")
        if shutil.which(COMPILER_NATIVE):
            print(f"🔄 回退使用默认编译器: '{COMPILER_NATIVE}'")
            return COMPILER_NATIVE
        else:
            print("❌ 错误: 环境中未找到任何可用的 GCC 编译器，请先安装。")
            sys.exit(1)
            
    return target_cmd

def generate_c_file(file_path, num_functions):
    """生成测试用 C 源码"""
    with open(file_path, 'w') as f:
        f.write("#include <stdio.h>\n\n")
        for i in range(num_functions):
            f.write(f"int dummy_func_{i}(int a, int b) {{\n")
            f.write(f"    int c = a + b * {i};\n")
            f.write(f"    int d = c ^ (a << 2);\n")
            f.write(f"    return d + {i};\n")
            f.write("}\n\n")

def pin_to_last_core():
    """子进程执行前，将 CPU 亲和性绑定到最后一个核心 (仅限 Linux 平台)"""
    try:
        if hasattr(os, 'sched_setaffinity'):
            last_core = os.cpu_count() - 1
            os.sched_setaffinity(0, {last_core})
    except Exception:
        pass

def compile_single_file(args):
    """单文件编译任务封装，供线程池调用"""
    compiler, source, cwd = args
    cmd = [compiler] + CFLAGS + [source]
    return subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

def run_benchmark():
    compiler = get_compiler()
    cpu_cores = os.cpu_count() or 1
    
    print("=" * 50)
    print("🚀 GCC 编译性能多维测试")
    print(f"🔧 编译器: {compiler}")
    print(f"📦 负载量: {NUM_FILES} 个文件 x {FUNCTIONS_PER_FILE} 函数/文件")
    print(f"💻 逻辑核心数: {cpu_cores}")
    print("=" * 50)

    temp_dir = tempfile.mkdtemp(prefix="gcc_bench_")
    
    try:
        # 1. 准备测试源码
        source_files = []
        for i in range(NUM_FILES):
            file_name = f"test_source_{i}.c"
            file_path = os.path.join(temp_dir, file_name)
            generate_c_file(file_path, FUNCTIONS_PER_FILE)
            source_files.append(file_name) # 存储相对路径
            
        print("\n▶ 开始单核编译测试 (强制绑定至最后 1 个核心)...")
        start_single = time.perf_counter()
        
        # 将所有文件传给单个 gcc 进程以单线程顺序执行，使用 preexec_fn 锁核心
        single_cmd = [compiler] + CFLAGS + source_files
        process_single = subprocess.run(
            single_cmd,
            cwd=temp_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            preexec_fn=pin_to_last_core if sys.platform.startswith('linux') else None,
            text=True
        )
        
        end_single = time.perf_counter()
        if process_single.returncode == 0:
            single_time = end_single - start_single
            print(f"✅ 单核编译耗时: {single_time:.4f} 秒")
        else:
            print("❌ 单核编译失败!")
            print(process_single.stderr)
            return

        # 为多核测试清理单核编译产生的 .o 文件
        for f in os.listdir(temp_dir):
            if f.endswith('.o'):
                os.remove(os.path.join(temp_dir, f))

        print(f"\n▶ 开始多核并发编译测试 (调度全部 {cpu_cores} 个核心)...")
        start_multi = time.perf_counter()
        
        # 利用多线程池并发调用 gcc，操作系统会自动将进程分配到所有可用核心
        tasks = [(compiler, src, temp_dir) for src in source_files]
        with ThreadPoolExecutor(max_workers=cpu_cores) as executor:
            results = list(executor.map(compile_single_file, tasks))
            
        end_multi = time.perf_counter()
        
        # 检查多核编译是否全部成功
        if all(res.returncode == 0 for res in results):
            multi_time = end_multi - start_multi
            print(f"✅ 多核编译耗时: {multi_time:.4f} 秒")
            
            # 总结输出
            print("-" * 50)
            print("📊 测试结果总结:")
            print(f"单核速度: {NUM_FILES * FUNCTIONS_PER_FILE / single_time:.0f} 函数/秒")
            print(f"多核速度: {NUM_FILES * FUNCTIONS_PER_FILE / multi_time:.0f} 函数/秒")
            print(f"多核提升倍率: {single_time / multi_time:.2f}x")
        else:
            print("❌ 多核编译过程中出现失败！")
            for res in results:
                if res.returncode != 0:
                    print(res.stderr)

    finally:
        if CLEAN_UP:
            shutil.rmtree(temp_dir)
            print("\n🧹 已清理临时测试文件。")

if __name__ == "__main__":
    run_benchmark()