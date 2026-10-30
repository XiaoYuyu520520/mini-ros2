import psutil
import os
import time

# --- 可配置参数 ---
CPU_SAMPLE_INTERVAL = 0.1  # 采样间隔（秒），越小越实时但自身开销越大
# ------------------

class ProcessMonitor:
    def __init__(self, child_pid=None):
        """
        :param child_pid: 可选，需要监控的 C++ 子进程 PID
        """
        self.main_process = psutil.Process(os.getpid())
        self.child_process = None
        if child_pid:
            try:
                self.child_process = psutil.Process(child_pid)
            except psutil.NoSuchProcess:
                self.child_process = None

    def get_stats(self):
        """
        获取当前 Python 进程和 C++ 子进程的 CPU 占用率
        注意：首次调用 cpu_percent() 会返回 0.0
        """
        stats = {
            "python_cpu": 0.0,
            "cpp_cpu": 0.0,
            "system_cpu": psutil.cpu_percent(interval=None)
        }

        try:
            # interval=None 表示非阻塞，计算自上次调用以来的百分比
            stats["python_cpu"] = self.main_process.cpu_percent(interval=None)
            
            if self.child_process and self.child_process.is_running():
                stats["cpp_cpu"] = self.child_process.cpu_percent(interval=None)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass

        return stats

def print_cpu_usage(stats):
    """格式化输出"""
    print(f"\r[监控] 系统: {stats['system_cpu']}% | "
          f"Python (消费者): {stats['python_cpu']}% | "
          f"C++ (生产者): {stats['cpp_cpu']}%", end="")
