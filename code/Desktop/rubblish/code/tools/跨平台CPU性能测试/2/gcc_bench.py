import os
import time
import shutil
import tempfile
import subprocess

# =====================================================================
# 可配置参数区
# =====================================================================
GCC_CMD = "gcc"                  # GCC 命令或绝对路径 (例如 "aarch64-linux-gnu-gcc")
CFLAGS = ["-O2", "-c"]           # 编译参数 (-c 表示只编译不链接，-O2 测试优化耗时)
NUM_FILES = 10                   # 生成的 C 源文件数量
FUNCTIONS_PER_FILE = 5000        # 每个 C 文件中生成的函数数量
CLEAN_UP = True                  # 测试结束后是否清理临时目录
# =====================================================================

def generate_c_file(file_path, num_functions):
    """生成包含大量简单函数的 C 语言源文件，用于给编译器制造压力"""
    with open(file_path, 'w') as f:
        f.write("#include <stdio.h>\n\n")
        for i in range(num_functions):
            f.write(f"int dummy_func_{i}(int a, int b) {{\n")
            f.write(f"    int c = a + b * {i};\n")
            f.write(f"    int d = c ^ (a << 2);\n")
            f.write(f"    return d + {i};\n")
            f.write("}\n\n")

def run_benchmark():
    print(f"开始 GCC 编译速度测试...")
    print(f"配置: {NUM_FILES} 个源文件, 每个文件 {FUNCTIONS_PER_FILE} 个函数")
    print(f"编译参数: {' '.join([GCC_CMD] + CFLAGS)}")
    print("-" * 50)

    # 检查 GCC 是否存在
    if shutil.which(GCC_CMD) is None:
        print(f"错误: 找不到编译器 '{GCC_CMD}'，请确认已安装并配置了环境变量。")
        return

    # 创建临时工作目录
    temp_dir = tempfile.mkdtemp(prefix="gcc_bench_")
    print(f"创建临时工作目录: {temp_dir}")

    try:
        source_files = []
        # 1. 生成测试代码
        start_gen = time.time()
        for i in range(NUM_FILES):
            file_name = os.path.join(temp_dir, f"test_source_{i}.c")
            generate_c_file(file_name, FUNCTIONS_PER_FILE)
            source_files.append(file_name)
        print(f"生成测试代码耗时: {time.time() - start_gen:.2f} 秒")

        # 2. 执行编译并计时
        compile_commands = [GCC_CMD] + CFLAGS + source_files
        
        print("正在编译中，请稍候...")
        start_compile = time.perf_counter() # 使用高精度计时器
        
        process = subprocess.run(
            compile_commands,
            cwd=temp_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        end_compile = time.perf_counter()
        
        # 3. 输出结果
        if process.returncode == 0:
            print("-" * 50)
            print("✅ 编译成功！")
            print(f"⏱️  总编译耗时: {end_compile - start_compile:.4f} 秒")
            
            # 计算简单的吞吐量指标
            total_functions = NUM_FILES * FUNCTIONS_PER_FILE
            speed = total_functions / (end_compile - start_compile)
            print(f"🚀 编译速度: 约 {speed:.0f} 个函数/秒")
        else:
            print("-" * 50)
            print("❌ 编译失败！")
            print("错误信息:")
            print(process.stderr)

    finally:
        # 清理战场
        if CLEAN_UP:
            shutil.rmtree(temp_dir)
            print("-" * 50)
            print(f"已清理临时工作目录: {temp_dir}")

if __name__ == "__main__":
    run_benchmark()