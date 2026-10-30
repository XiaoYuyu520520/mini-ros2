#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
跨平台 CPU 原生基准测试
======================

功能：
1. Python 文件内嵌 C++17 基准程序。
2. 自动寻找 g++/clang++，使用 -O2 编译。
3. 自动比较 Python 脚本、二进制和构建信息；仅在需要时重编译。
4. 自动检测 noexec/FUSE；失败时回退到 /data/local/tmp、用户缓存或临时目录。
5. 测试：单核整数、单核浮点，以及固定时长吞吐模式的多核整数、浮点。
6. 测试：单线程与多线程内存顺序读、写、复制带宽。
7. 内存工作集自动扩展，并通过数组交替与线程分片轮换降低缓存命中污染。
8. 多核 CPU 允许快核心完成更多工作，适配 X4/A720/A520 等异构架构。
9. Windows / Linux 均可运行。
10. 位置参数采用 1 开始编号：

       python cpu_native_benchmark.py 2

   表示单核阶段绑定到“第 2 个可用逻辑处理器”。

示例：
    python cpu_native_benchmark.py 2
    python cpu_native_benchmark.py 2 --threads 16 --seconds 1.5 --repeats 3
    python cpu_native_benchmark.py 1 --rebuild

Windows 需要安装 MinGW-w64 g++，并将 g++.exe 加入 PATH。
Linux 通常安装 build-essential 后即可使用。
"""

from __future__ import annotations

import argparse
import errno
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any

BENCH_VERSION = "2.6.0"

# 固定参考吞吐率。分数仅用于同版本脚本之间比较。
# 参考机在对应吞吐率下得 10000 分。
REFERENCE_INT_MITER_S = 200.0
REFERENCE_FLOAT_MITER_S = 100.0
REFERENCE_MEMORY_GB_S = 50.0

CPP_SOURCE = r'''// Embedded C++17 CPU benchmark
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#elif defined(__linux__)
  #include <pthread.h>
  #include <sched.h>
  #include <unistd.h>
#else
  #error Unsupported operating system. Only Windows and Linux are supported.
#endif

namespace {

constexpr const char* kVersion = "2.6.0";
volatile std::uint64_t g_integer_sink = 0;
volatile double g_float_sink = 0.0;

struct CpuLocation {
    unsigned group = 0;
    unsigned index = 0;
    int os_id = 0;
};

struct RunResult {
    double seconds = 0.0;
    std::uint64_t iterations = 0;
    std::uint64_t integer_checksum = 0;
    double float_checksum = 0.0;
    std::uint64_t bytes = 0;
};

static inline std::uint64_t rotl64(std::uint64_t x, unsigned r) {
    return (x << r) | (x >> (64U - r));
}

std::vector<CpuLocation> get_available_cpus() {
    std::vector<CpuLocation> cpus;
#if defined(_WIN32)
    const WORD groups = GetActiveProcessorGroupCount();
    int linear_id = 0;
    for (WORD g = 0; g < groups; ++g) {
        const DWORD count = GetActiveProcessorCount(g);
        for (DWORD i = 0; i < count; ++i) {
            CpuLocation loc;
            loc.group = static_cast<unsigned>(g);
            loc.index = static_cast<unsigned>(i);
            loc.os_id = linear_id++;
            cpus.push_back(loc);
        }
    }
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        throw std::runtime_error("sched_getaffinity failed");
    }
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &set)) {
            CpuLocation loc;
            loc.group = 0;
            loc.index = static_cast<unsigned>(cpu);
            loc.os_id = cpu;
            cpus.push_back(loc);
        }
    }
#endif
    if (cpus.empty()) {
        throw std::runtime_error("No available logical processors detected");
    }
    return cpus;
}

bool pin_current_thread(const CpuLocation& cpu) {
#if defined(_WIN32)
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(cpu.group);
    affinity.Mask = (KAFFINITY(1) << cpu.index);
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
#elif defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu.index, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#endif
}

std::uint64_t integer_kernel(std::uint64_t iterations, std::uint64_t seed) {
    std::uint64_t a = 0x243f6a8885a308d3ULL ^ seed;
    std::uint64_t b = 0x13198a2e03707344ULL + seed;
    std::uint64_t c = 0xa4093822299f31d0ULL ^ (seed << 1U);
    std::uint64_t d = 0x082efa98ec4e6c89ULL + (seed << 2U);

    for (std::uint64_t i = 1; i <= iterations; ++i) {
        const std::uint64_t divisor = (b & 0xFFFFFULL) + 1ULL;
        const std::uint64_t quotient = a / divisor;
        const std::uint64_t remainder = a % divisor;

        a = (a ^ (b + 0x9e3779b97f4a7c15ULL + i))
            * 0xbf58476d1ce4e5b9ULL + quotient;
        b = rotl64(b + c + remainder, 17U) ^ a;
        c = c * 0x94d049bb133111ebULL + (d ^ i) + quotient;
        d = rotl64(d ^ a, 29U) + b + remainder;
    }

    return a ^ b ^ c ^ d;
}

double float_kernel(std::uint64_t iterations, std::uint64_t seed) {
    double a = 1.125 + static_cast<double>(seed & 255U) * 1.0e-6;
    double b = 0.875 + static_cast<double>((seed >> 8U) & 255U) * 1.0e-6;
    double c = 1.375 + static_cast<double>((seed >> 16U) & 255U) * 1.0e-6;
    double d = 0.625 + static_cast<double>((seed >> 24U) & 255U) * 1.0e-6;

    for (std::uint64_t i = 0; i < iterations; ++i) {
        const double root = std::sqrt(a + 1.000000119);
        a = root + b * 0.0000017 + 0.0000003;
        b = b * 0.9999993 + c * 0.0000004 + a * 0.0000002;
        c = c / 1.0000007 + d * 0.0000005 + root * 0.0000001;
        d = d * 0.9999991 + a * 0.0000006 - b * 0.0000002;

        if ((i & 0xFFFFFULL) == 0xFFFFFULL) {
            // 防止长时间运行时数值偏离正常范围，同时保留浮点依赖链。
            a = std::fmod(std::fabs(a), 2.0) + 0.5;
            b = std::fmod(std::fabs(b), 2.0) + 0.5;
            c = std::fmod(std::fabs(c), 2.0) + 0.5;
            d = std::fmod(std::fabs(d), 2.0) + 0.5;
        }
    }

    return a + b + c + d;
}

class StartGate {
public:
    explicit StartGate(std::size_t expected) : expected_(expected) {}

    void worker_ready_and_wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++ready_;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
    }

    void wait_until_ready() {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return ready_ >= expected_; });
    }

    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t expected_ = 0;
    std::size_t ready_ = 0;
    bool released_ = false;
};

// 可重复使用的阶段屏障。内存测试在每轮结束时同步，随后轮换线程分片，
// 避免每个线程持续命中同一小块私有缓存。
class CyclicBarrier {
public:
    explicit CyclicBarrier(std::size_t parties)
        : parties_(parties), remaining_(parties) {
        if (parties == 0U) {
            throw std::runtime_error("Barrier requires at least one party");
        }
    }

    template <typename Completion>
    void arrive_and_wait(Completion completion) {
        std::unique_lock<std::mutex> lock(mutex_);
        const std::size_t generation = generation_;
        if (--remaining_ == 0U) {
            completion();
            remaining_ = parties_;
            ++generation_;
            condition_.notify_all();
            return;
        }
        condition_.wait(lock, [&] { return generation_ != generation; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t parties_;
    std::size_t remaining_;
    std::size_t generation_ = 0U;
};

RunResult run_single_integer(std::uint64_t iterations, const CpuLocation& cpu) {
    RunResult result;
    bool affinity_ok = false;
    std::thread worker([&] {
        affinity_ok = pin_current_thread(cpu);
        if (!affinity_ok) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto checksum = integer_kernel(iterations, 1U);
        const auto end = std::chrono::steady_clock::now();
        result.seconds = std::chrono::duration<double>(end - start).count();
        result.iterations = iterations;
        result.integer_checksum = checksum;
        g_integer_sink ^= checksum;
    });
    worker.join();
    if (!affinity_ok) {
        throw std::runtime_error("Failed to set single-thread CPU affinity");
    }
    return result;
}

RunResult run_single_float(std::uint64_t iterations, const CpuLocation& cpu) {
    RunResult result;
    bool affinity_ok = false;
    std::thread worker([&] {
        affinity_ok = pin_current_thread(cpu);
        if (!affinity_ok) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto checksum = float_kernel(iterations, 2U);
        const auto end = std::chrono::steady_clock::now();
        result.seconds = std::chrono::duration<double>(end - start).count();
        result.iterations = iterations;
        result.float_checksum = checksum;
        g_float_sink += checksum;
    });
    worker.join();
    if (!affinity_ok) {
        throw std::runtime_error("Failed to set single-thread CPU affinity");
    }
    return result;
}

RunResult run_multi_integer(
    double target_seconds,
    std::size_t thread_count,
    const std::vector<CpuLocation>& cpus
) {
    constexpr std::uint64_t kChunkIterations = 65536U;
    StartGate gate(thread_count);
    std::vector<std::thread> workers;
    std::vector<std::uint64_t> checksums(thread_count, 0U);
    std::vector<std::uint64_t> completed_iterations(thread_count, 0U);
    std::vector<unsigned char> affinity_ok(thread_count, 0U);
    workers.reserve(thread_count);

    std::chrono::steady_clock::time_point shared_start;
    std::chrono::steady_clock::time_point shared_deadline;

    for (std::size_t t = 0; t < thread_count; ++t) {
        workers.emplace_back([&, t] {
            affinity_ok[t] = pin_current_thread(cpus[t % cpus.size()]) ? 1U : 0U;
            gate.worker_ready_and_wait();
            if (!affinity_ok[t]) {
                return;
            }

            std::uint64_t local_checksum = 0U;
            std::uint64_t local_iterations = 0U;
            std::uint64_t seed = 100U + static_cast<std::uint64_t>(t) * 4099U;
            do {
                local_checksum ^= integer_kernel(kChunkIterations, seed++);
                local_iterations += kChunkIterations;
            } while (std::chrono::steady_clock::now() < shared_deadline);

            checksums[t] = local_checksum;
            completed_iterations[t] = local_iterations;
        });
    }

    gate.wait_until_ready();
    shared_start = std::chrono::steady_clock::now();
    shared_deadline = shared_start
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(target_seconds)
        );
    gate.release();
    for (auto& worker : workers) {
        worker.join();
    }
    const auto end = std::chrono::steady_clock::now();

    if (std::find(affinity_ok.begin(), affinity_ok.end(), 0U) != affinity_ok.end()) {
        throw std::runtime_error("Failed to set one or more multi-thread CPU affinities");
    }

    RunResult result;
    result.seconds = std::chrono::duration<double>(end - shared_start).count();
    result.iterations = std::accumulate(
        completed_iterations.begin(), completed_iterations.end(), std::uint64_t{0}
    );
    result.integer_checksum = std::accumulate(
        checksums.begin(), checksums.end(), std::uint64_t{0},
        [](std::uint64_t x, std::uint64_t y) { return x ^ y; }
    );
    g_integer_sink ^= result.integer_checksum;
    return result;
}

RunResult run_multi_float(
    double target_seconds,
    std::size_t thread_count,
    const std::vector<CpuLocation>& cpus
) {
    constexpr std::uint64_t kChunkIterations = 16384U;
    StartGate gate(thread_count);
    std::vector<std::thread> workers;
    std::vector<double> checksums(thread_count, 0.0);
    std::vector<std::uint64_t> completed_iterations(thread_count, 0U);
    std::vector<unsigned char> affinity_ok(thread_count, 0U);
    workers.reserve(thread_count);

    std::chrono::steady_clock::time_point shared_start;
    std::chrono::steady_clock::time_point shared_deadline;

    for (std::size_t t = 0; t < thread_count; ++t) {
        workers.emplace_back([&, t] {
            affinity_ok[t] = pin_current_thread(cpus[t % cpus.size()]) ? 1U : 0U;
            gate.worker_ready_and_wait();
            if (!affinity_ok[t]) {
                return;
            }

            double local_checksum = 0.0;
            std::uint64_t local_iterations = 0U;
            std::uint64_t seed = 200U + static_cast<std::uint64_t>(t) * 4099U;
            do {
                local_checksum += float_kernel(kChunkIterations, seed++);
                local_iterations += kChunkIterations;
            } while (std::chrono::steady_clock::now() < shared_deadline);

            checksums[t] = local_checksum;
            completed_iterations[t] = local_iterations;
        });
    }

    gate.wait_until_ready();
    shared_start = std::chrono::steady_clock::now();
    shared_deadline = shared_start
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(target_seconds)
        );
    gate.release();
    for (auto& worker : workers) {
        worker.join();
    }
    const auto end = std::chrono::steady_clock::now();

    if (std::find(affinity_ok.begin(), affinity_ok.end(), 0U) != affinity_ok.end()) {
        throw std::runtime_error("Failed to set one or more multi-thread CPU affinities");
    }

    RunResult result;
    result.seconds = std::chrono::duration<double>(end - shared_start).count();
    result.iterations = std::accumulate(
        completed_iterations.begin(), completed_iterations.end(), std::uint64_t{0}
    );
    result.float_checksum = std::accumulate(checksums.begin(), checksums.end(), 0.0);
    g_float_sink += result.float_checksum;
    return result;
}

enum class MemoryOperation {
    Read,
    Write,
    Copy,
};

struct MemoryBuffers {
    explicit MemoryBuffers(std::size_t elements)
        : source(elements), destination(elements) {
        if (elements == 0U) {
            throw std::runtime_error("Memory buffer size is zero");
        }
        for (std::size_t i = 0; i < elements; ++i) {
            source[i] = 0x9e3779b97f4a7c15ULL
                ^ (static_cast<std::uint64_t>(i) * 0xbf58476d1ce4e5b9ULL);
            destination[i] = 0x243f6a8885a308d3ULL + static_cast<std::uint64_t>(i);
        }
        // 初始化阶段已预触碰所有页面，避免把首次缺页中断计入正式带宽。
        g_integer_sink ^= source.front() ^ source.back()
                        ^ destination.front() ^ destination.back();
    }

    std::vector<std::uint64_t> source;
    std::vector<std::uint64_t> destination;
};

std::uint64_t memory_read_pass(
    const std::uint64_t* data,
    std::size_t begin,
    std::size_t end
) {
    std::uint64_t s0 = 0U;
    std::uint64_t s1 = 0U;
    std::uint64_t s2 = 0U;
    std::uint64_t s3 = 0U;
    std::uint64_t s4 = 0U;
    std::uint64_t s5 = 0U;
    std::uint64_t s6 = 0U;
    std::uint64_t s7 = 0U;
    std::size_t i = begin;
    for (; i + 7U < end; i += 8U) {
        s0 ^= data[i];
        s1 ^= data[i + 1U];
        s2 ^= data[i + 2U];
        s3 ^= data[i + 3U];
        s4 ^= data[i + 4U];
        s5 ^= data[i + 5U];
        s6 ^= data[i + 6U];
        s7 ^= data[i + 7U];
    }
    for (; i < end; ++i) {
        s0 ^= data[i];
    }
    return s0 ^ s1 ^ s2 ^ s3 ^ s4 ^ s5 ^ s6 ^ s7;
}

std::uint64_t memory_write_pass(
    std::uint64_t* data,
    std::size_t begin,
    std::size_t end,
    std::uint64_t value
) {
    std::fill(data + begin, data + end, value);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const std::size_t sample = begin + ((end - begin) / 2U);
    return data[sample];
}

std::uint64_t memory_copy_pass(
    const std::uint64_t* source,
    std::uint64_t* destination,
    std::size_t begin,
    std::size_t end
) {
    const std::size_t count = end - begin;
    std::memcpy(
        destination + begin,
        source + begin,
        count * sizeof(std::uint64_t)
    );
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const std::size_t sample = begin + (count / 2U);
    return destination[sample];
}

RunResult run_single_memory(
    MemoryOperation operation,
    MemoryBuffers& buffers,
    double target_seconds,
    const CpuLocation& cpu
) {
    RunResult result;
    bool affinity_ok = false;
    std::thread worker([&] {
        affinity_ok = pin_current_thread(cpu);
        if (!affinity_ok) {
            return;
        }

        const std::size_t elements = buffers.source.size();
        const std::uint64_t bytes_per_pass = static_cast<std::uint64_t>(elements)
            * static_cast<std::uint64_t>(sizeof(std::uint64_t))
            * (operation == MemoryOperation::Copy ? 2ULL : 1ULL);
        std::uint64_t passes = 0U;
        std::uint64_t checksum = 0U;
        const auto start = std::chrono::steady_clock::now();
        auto now = start;
        do {
            // 两个数组交替作为当前流，确保活跃工作集是两个数组之和，
            // 避免单线程反复读取一个刚好能装进 LLC 的数组。
            const bool odd = (passes & 1ULL) != 0ULL;
            if (operation == MemoryOperation::Read) {
                const std::uint64_t* data = odd
                    ? buffers.destination.data()
                    : buffers.source.data();
                checksum ^= memory_read_pass(data, 0U, elements);
            } else if (operation == MemoryOperation::Write) {
                std::uint64_t* data = odd
                    ? buffers.source.data()
                    : buffers.destination.data();
                checksum ^= memory_write_pass(
                    data, 0U, elements,
                    0x94d049bb133111ebULL ^ passes
                );
            } else {
                const std::uint64_t* input = odd
                    ? buffers.destination.data()
                    : buffers.source.data();
                std::uint64_t* output = odd
                    ? buffers.source.data()
                    : buffers.destination.data();
                checksum ^= memory_copy_pass(input, output, 0U, elements);
            }
            ++passes;
            now = std::chrono::steady_clock::now();
        } while (std::chrono::duration<double>(now - start).count() < target_seconds);

        result.seconds = std::chrono::duration<double>(now - start).count();
        result.iterations = passes;
        result.bytes = bytes_per_pass * passes;
        result.integer_checksum = checksum;
        g_integer_sink ^= checksum;
    });
    worker.join();
    if (!affinity_ok) {
        throw std::runtime_error("Failed to set single-thread memory CPU affinity");
    }
    return result;
}

RunResult run_multi_memory(
    MemoryOperation operation,
    MemoryBuffers& buffers,
    double target_seconds,
    std::size_t thread_count,
    const std::vector<CpuLocation>& cpus
) {
    StartGate start_gate(thread_count);
    CyclicBarrier epoch_barrier(thread_count);
    std::vector<std::thread> workers;
    std::vector<std::uint64_t> thread_bytes(thread_count, 0U);
    std::vector<std::uint64_t> checksums(thread_count, 0U);
    std::vector<unsigned char> affinity_ok(thread_count, 0U);
    workers.reserve(thread_count);

    const std::size_t elements = buffers.source.size();
    std::chrono::steady_clock::time_point shared_start;
    std::chrono::steady_clock::time_point shared_deadline;
    std::atomic<bool> stop{false};

    // 每轮所有线程覆盖完整数组，但线程到分片的映射都会轮换。
    // 下一轮同时切换 source/destination，故活跃工作集始终是两个数组之和。
    // 这样即使 32 线程下单个分片只有几 MiB，也不会让某线程永久命中同一分片。
    for (std::size_t t = 0; t < thread_count; ++t) {
        workers.emplace_back([&, t] {
            affinity_ok[t] = pin_current_thread(cpus[t % cpus.size()]) ? 1U : 0U;
            start_gate.worker_ready_and_wait();
            if (!affinity_ok[t]) {
                return;
            }

            std::uint64_t local_bytes = 0U;
            std::uint64_t checksum = 0U;
            std::size_t epoch = 0U;

            while (!stop.load(std::memory_order_acquire)) {
                const std::size_t slot = (t + epoch) % thread_count;
                const std::size_t begin = elements * slot / thread_count;
                const std::size_t end = elements * (slot + 1U) / thread_count;
                const bool odd = (epoch & 1U) != 0U;

                if (begin < end) {
                    const std::uint64_t range_bytes =
                        static_cast<std::uint64_t>(end - begin)
                        * static_cast<std::uint64_t>(sizeof(std::uint64_t))
                        * (operation == MemoryOperation::Copy ? 2ULL : 1ULL);

                    if (operation == MemoryOperation::Read) {
                        const std::uint64_t* data = odd
                            ? buffers.destination.data()
                            : buffers.source.data();
                        checksum ^= memory_read_pass(data, begin, end);
                    } else if (operation == MemoryOperation::Write) {
                        std::uint64_t* data = odd
                            ? buffers.source.data()
                            : buffers.destination.data();
                        checksum ^= memory_write_pass(
                            data, begin, end,
                            0x94d049bb133111ebULL
                                ^ static_cast<std::uint64_t>(epoch)
                                ^ (static_cast<std::uint64_t>(t) << 32U)
                        );
                    } else {
                        const std::uint64_t* input = odd
                            ? buffers.destination.data()
                            : buffers.source.data();
                        std::uint64_t* output = odd
                            ? buffers.source.data()
                            : buffers.destination.data();
                        checksum ^= memory_copy_pass(input, output, begin, end);
                    }
                    local_bytes += range_bytes;
                }

                epoch_barrier.arrive_and_wait([&] {
                    if (std::chrono::steady_clock::now() >= shared_deadline) {
                        stop.store(true, std::memory_order_release);
                    }
                });
                ++epoch;
            }

            thread_bytes[t] = local_bytes;
            checksums[t] = checksum;
        });
    }

    start_gate.wait_until_ready();
    shared_start = std::chrono::steady_clock::now();
    shared_deadline = shared_start
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(target_seconds)
        );
    start_gate.release();
    for (auto& worker : workers) {
        worker.join();
    }
    const auto end = std::chrono::steady_clock::now();

    if (std::find(affinity_ok.begin(), affinity_ok.end(), 0U) != affinity_ok.end()) {
        throw std::runtime_error("Failed to set one or more memory thread affinities");
    }

    RunResult result;
    result.seconds = std::chrono::duration<double>(end - shared_start).count();
    result.bytes = std::accumulate(
        thread_bytes.begin(), thread_bytes.end(), std::uint64_t{0}
    );
    result.iterations = 0U;
    result.integer_checksum = std::accumulate(
        checksums.begin(), checksums.end(), std::uint64_t{0},
        [](std::uint64_t x, std::uint64_t y) { return x ^ y; }
    );
    g_integer_sink ^= result.integer_checksum;
    return result;
}

template <typename Callable>
RunResult median_run(int repeats, Callable callable) {
    std::vector<RunResult> results;
    results.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        results.push_back(callable());
    }
    std::sort(results.begin(), results.end(), [](const RunResult& a, const RunResult& b) {
        return a.seconds < b.seconds;
    });
    return results[results.size() / 2U];
}

template <typename Callable>
RunResult median_iteration_rate_run(int repeats, Callable callable) {
    std::vector<RunResult> results;
    results.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        results.push_back(callable());
    }
    std::sort(results.begin(), results.end(), [](const RunResult& a, const RunResult& b) {
        const double rate_a = static_cast<double>(a.iterations) / a.seconds;
        const double rate_b = static_cast<double>(b.iterations) / b.seconds;
        return rate_a < rate_b;
    });
    return results[results.size() / 2U];
}

template <typename Callable>
RunResult median_byte_rate_run(int repeats, Callable callable) {
    std::vector<RunResult> results;
    results.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        results.push_back(callable());
    }
    std::sort(results.begin(), results.end(), [](const RunResult& a, const RunResult& b) {
        const double rate_a = static_cast<double>(a.bytes) / a.seconds;
        const double rate_b = static_cast<double>(b.bytes) / b.seconds;
        return rate_a < rate_b;
    });
    return results[results.size() / 2U];
}

std::uint64_t scale_iterations(std::uint64_t current, double elapsed, double target_seconds) {
    if (!(elapsed > 0.0) || !std::isfinite(elapsed)) {
        return current;
    }
    double scaled = static_cast<double>(current) * target_seconds / elapsed;
    scaled *= 1.05;  // 留少量余量，避免测试过短。
    const double minimum = 10000.0;
    const double maximum = static_cast<double>(std::numeric_limits<std::uint64_t>::max() / 1024U);
    scaled = std::max(minimum, std::min(scaled, maximum));
    return static_cast<std::uint64_t>(scaled);
}

std::uint64_t calibrate_integer(double target_seconds, const CpuLocation& cpu) {
    std::uint64_t iterations = 200000U;
    for (int pass = 0; pass < 3; ++pass) {
        const RunResult trial = run_single_integer(iterations, cpu);
        iterations = scale_iterations(iterations, trial.seconds, target_seconds);
        if (trial.seconds >= target_seconds * 0.75 && trial.seconds <= target_seconds * 1.50) {
            break;
        }
    }
    return iterations;
}

std::uint64_t calibrate_float(double target_seconds, const CpuLocation& cpu) {
    std::uint64_t iterations = 500000U;
    for (int pass = 0; pass < 3; ++pass) {
        const RunResult trial = run_single_float(iterations, cpu);
        iterations = scale_iterations(iterations, trial.seconds, target_seconds);
        if (trial.seconds >= target_seconds * 0.75 && trial.seconds <= target_seconds * 1.50) {
            break;
        }
    }
    return iterations;
}

struct Options {
    std::size_t core_one_based = 1U;
    std::size_t threads = 0U;
    double seconds = 1.0;
    int repeats = 3;
    std::size_t memory_mib = 512U;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--core") {
            options.core_one_based = static_cast<std::size_t>(std::stoull(require_value("--core")));
        } else if (arg == "--threads") {
            options.threads = static_cast<std::size_t>(std::stoull(require_value("--threads")));
        } else if (arg == "--seconds") {
            options.seconds = std::stod(require_value("--seconds"));
        } else if (arg == "--repeats") {
            options.repeats = std::stoi(require_value("--repeats"));
        } else if (arg == "--memory-mib") {
            options.memory_mib = static_cast<std::size_t>(std::stoull(require_value("--memory-mib")));
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.core_one_based == 0U) {
        throw std::runtime_error("--core uses 1-based numbering and must be >= 1");
    }
    if (!(options.seconds >= 0.2 && options.seconds <= 30.0)) {
        throw std::runtime_error("--seconds must be between 0.2 and 30");
    }
    if (options.repeats < 1 || options.repeats > 9 || (options.repeats % 2) == 0) {
        throw std::runtime_error("--repeats must be an odd number between 1 and 9");
    }
    if (options.memory_mib < 128U || options.memory_mib > 16384U) {
        throw std::runtime_error("--memory-mib must be between 128 and 16384");
    }
    return options;
}

void print_metric_json(const char* name, const RunResult& result, bool trailing_comma) {
    const double miter_s = static_cast<double>(result.iterations) / result.seconds / 1.0e6;
    std::cout << "    \"" << name << "\": {"
              << "\"seconds\":" << result.seconds << ","
              << "\"iterations\":" << result.iterations << ","
              << "\"miter_s\":" << miter_s << "}"
              << (trailing_comma ? "," : "") << "\n";
}

void print_memory_json(const char* name, const RunResult& result, bool trailing_comma) {
    const double gb_s = static_cast<double>(result.bytes) / result.seconds / 1.0e9;
    std::cout << "    \"" << name << "\": {"
              << "\"seconds\":" << result.seconds << ","
              << "\"bytes\":" << result.bytes << ","
              << "\"gb_s\":" << gb_s << "}"
              << (trailing_comma ? "," : "") << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        // Python 端用这个零负载入口验证二进制所在目录是否真的允许执行。
        if (argc == 2 && std::string(argv[1]) == "--probe") {
            return 0;
        }

        const Options options = parse_options(argc, argv);
        const std::vector<CpuLocation> cpus = get_available_cpus();

        if (options.core_one_based > cpus.size()) {
            throw std::runtime_error("Requested core is outside the available logical processor range");
        }

        const CpuLocation selected_cpu = cpus[options.core_one_based - 1U];
        std::size_t thread_count = options.threads == 0U ? cpus.size() : options.threads;
        thread_count = std::max<std::size_t>(1U, std::min(thread_count, cpus.size()));

        // 同时预热整数与浮点路径。
        (void)run_single_integer(50000U, selected_cpu);
        (void)run_single_float(100000U, selected_cpu);

        const std::uint64_t integer_iterations = calibrate_integer(options.seconds, selected_cpu);
        const std::uint64_t float_iterations = calibrate_float(options.seconds, selected_cpu);

        const RunResult single_integer = median_run(options.repeats, [&] {
            return run_single_integer(integer_iterations, selected_cpu);
        });
        const RunResult single_float = median_run(options.repeats, [&] {
            return run_single_float(float_iterations, selected_cpu);
        });
        const RunResult multi_integer = median_iteration_rate_run(options.repeats, [&] {
            return run_multi_integer(options.seconds, thread_count, cpus);
        });
        const RunResult multi_float = median_iteration_rate_run(options.repeats, [&] {
            return run_multi_float(options.seconds, thread_count, cpus);
        });

        const std::uint64_t total_memory_bytes = static_cast<std::uint64_t>(options.memory_mib)
            * 1024ULL * 1024ULL;
        const std::size_t memory_elements = static_cast<std::size_t>(
            total_memory_bytes / (2ULL * sizeof(std::uint64_t))
        );
        MemoryBuffers memory_buffers(memory_elements);

        const RunResult single_memory_read = median_byte_rate_run(options.repeats, [&] {
            return run_single_memory(
                MemoryOperation::Read, memory_buffers, options.seconds, selected_cpu
            );
        });
        const RunResult single_memory_write = median_byte_rate_run(options.repeats, [&] {
            return run_single_memory(
                MemoryOperation::Write, memory_buffers, options.seconds, selected_cpu
            );
        });
        const RunResult single_memory_copy = median_byte_rate_run(options.repeats, [&] {
            return run_single_memory(
                MemoryOperation::Copy, memory_buffers, options.seconds, selected_cpu
            );
        });
        const RunResult multi_memory_read = median_byte_rate_run(options.repeats, [&] {
            return run_multi_memory(
                MemoryOperation::Read, memory_buffers, options.seconds, thread_count, cpus
            );
        });
        const RunResult multi_memory_write = median_byte_rate_run(options.repeats, [&] {
            return run_multi_memory(
                MemoryOperation::Write, memory_buffers, options.seconds, thread_count, cpus
            );
        });
        const RunResult multi_memory_copy = median_byte_rate_run(options.repeats, [&] {
            return run_multi_memory(
                MemoryOperation::Copy, memory_buffers, options.seconds, thread_count, cpus
            );
        });

        std::cout << std::setprecision(12);
        std::cout << "{\n";
        std::cout << "  \"version\":\"" << kVersion << "\",\n";
        std::cout << "  \"available_cpus\":" << cpus.size() << ",\n";
        std::cout << "  \"selected_core_one_based\":" << options.core_one_based << ",\n";
        std::cout << "  \"selected_os_cpu\":" << selected_cpu.os_id << ",\n";
        std::cout << "  \"threads\":" << thread_count << ",\n";
        std::cout << "  \"target_seconds\":" << options.seconds << ",\n";
        std::cout << "  \"repeats\":" << options.repeats << ",\n";
        std::cout << "  \"memory_mib\":" << options.memory_mib << ",\n";
        const double memory_array_mib =
            static_cast<double>(memory_elements * sizeof(std::uint64_t)) / 1048576.0;
        std::cout << "  \"multi_cpu_mode\":\"fixed_time_throughput\",\n";
        std::cout << "  \"memory_array_mib\":" << memory_array_mib << ",\n";
        std::cout << "  \"memory_array_per_thread_mib\":"
                  << (memory_array_mib / static_cast<double>(thread_count))
                  << ",\n";
        std::cout << "  \"metrics\": {\n";
        print_metric_json("single_integer", single_integer, true);
        print_metric_json("single_float", single_float, true);
        print_metric_json("multi_integer", multi_integer, true);
        print_metric_json("multi_float", multi_float, true);
        print_memory_json("single_memory_read", single_memory_read, true);
        print_memory_json("single_memory_write", single_memory_write, true);
        print_memory_json("single_memory_copy", single_memory_copy, true);
        print_memory_json("multi_memory_read", multi_memory_read, true);
        print_memory_json("multi_memory_write", multi_memory_write, true);
        print_memory_json("multi_memory_copy", multi_memory_copy, false);
        std::cout << "  },\n";
        std::cout << "  \"checksum_integer\":" << g_integer_sink << ",\n";
        std::cout << "  \"checksum_float\":" << g_float_sink << "\n";
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "CPU benchmark error: " << ex.what() << "\n";
        return 2;
    }
}
'''


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="内嵌 C++17 的 Windows/Linux CPU 单核与多核基准测试",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "core",
        nargs="?",
        type=int,
        default=1,
        help="单核阶段使用第几个可用逻辑处理器，采用 1 开始编号；填 2 表示第二个",
    )
    parser.add_argument(
        "--threads",
        type=int,
        default=0,
        help="多核阶段线程数；0 表示使用全部可用逻辑处理器",
    )
    parser.add_argument(
        "--seconds",
        type=float,
        default=1.0,
        help="单次单核校准目标时长；多核在全核降频时可能更久",
    )
    parser.add_argument(
        "--repeats",
        type=int,
        default=3,
        help="每个正式项目重复次数，必须是 1~9 的奇数，最终取中位数",
    )
    parser.add_argument(
        "--memory-mib",
        type=int,
        default=0,
        help=(
            "内存测试总工作集 MiB；0 表示自动按线程数和可用内存选择，"
            "内部平均分配为两个数组"
        ),
    )
    parser.add_argument(
        "--opt-level",
        choices=["O0", "Og", "O1", "O2", "O3", "Ofast", "Os"],
        default="O2",
        help=(
            "C++编译优化等级。正式性能对比建议 O2；"
            "O0/Og 用于调试，不建议计入性能排行榜"
        ),
    )
    parser.add_argument("--rebuild", action="store_true", help="强制重新编译 C++ 程序")
    parser.add_argument(
        "--no-native",
        action="store_true",
        help="不添加 -march=native；便于生成可复制到其他机器的二进制",
    )
    parser.add_argument("--keep-source", action="store_true", help="保留生成的 C++ 源文件")
    return parser.parse_args()



def physical_memory_info_mib() -> tuple[int | None, int | None]:
    """返回（总物理内存 MiB，可用物理内存 MiB）。"""
    if os.name == "nt":
        try:
            import ctypes

            class MemoryStatusEx(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            status = MemoryStatusEx()
            status.dwLength = ctypes.sizeof(status)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
                return (
                    int(status.ullTotalPhys // (1024 * 1024)),
                    int(status.ullAvailPhys // (1024 * 1024)),
                )
        except Exception:
            return None, None
        return None, None

    # Linux 优先使用 MemAvailable；它比 SC_AVPHYS_PAGES 更能反映可回收页缓存。
    try:
        values: dict[str, int] = {}
        with open("/proc/meminfo", "r", encoding="utf-8") as handle:
            for line in handle:
                key, _, rest = line.partition(":")
                if key in {"MemTotal", "MemAvailable", "MemFree"}:
                    parts = rest.strip().split()
                    if parts:
                        values[key] = int(parts[0]) // 1024
        total = values.get("MemTotal")
        available = values.get("MemAvailable", values.get("MemFree"))
        if total is not None or available is not None:
            return total, available
    except (OSError, ValueError):
        pass

    try:
        total_pages = os.sysconf("SC_PHYS_PAGES")
        available_pages = os.sysconf("SC_AVPHYS_PAGES")
        page_size = os.sysconf("SC_PAGE_SIZE")
        total = int((total_pages * page_size) // (1024 * 1024)) if total_pages > 0 else None
        available = (
            int((available_pages * page_size) // (1024 * 1024))
            if available_pages > 0
            else None
        )
        return total, available
    except (AttributeError, OSError, ValueError):
        return None, None


def available_logical_cpus() -> int:
    """尽量按当前进程亲和性返回真正可用的逻辑处理器数量。"""
    if hasattr(os, "sched_getaffinity"):
        try:
            count = len(os.sched_getaffinity(0))
            if count > 0:
                return count
        except OSError:
            pass
    return max(1, os.cpu_count() or 1)


def resolve_memory_mib(
    args: argparse.Namespace,
) -> tuple[int, bool, int | None, int | None]:
    """解析内存工作集，优先保证 DRAM 工作集，同时保留安全余量。"""
    total_mib, available_mib = physical_memory_info_mib()
    if args.memory_mib > 0:
        return args.memory_mib, False, total_mib, available_mib

    threads = args.threads if args.threads > 0 else available_logical_cpus()

    # 两个数组合计。32线程桌面目标 2 GiB，6~8线程设备目标 512 MiB。
    desired = max(512, threads * 64)
    desired = min(desired, 4096)

    if available_mib is not None:
        # 旧版只使用可用内存18%，在后台繁忙的Windows上会退到128 MiB，
        # 导致整个源数组进入L3。新版最多使用约60%，并至少保留25%或128 MiB。
        reserve = max(128, min(512, int(available_mib * 0.25)))
        cap_by_fraction = int(available_mib * 0.60)
        cap_by_reserve = max(128, available_mib - reserve)
        safe_cap = max(128, min(cap_by_fraction, cap_by_reserve))
        selected = min(desired, safe_cap)
    else:
        selected = min(desired, 1024)

    selected = max(128, min(selected, 16384))
    selected = max(128, (selected // 64) * 64)
    return selected, True, total_mib, available_mib

def script_path() -> Path:
    if "__file__" in globals():
        return Path(__file__).resolve()
    return Path(sys.argv[0]).resolve()


def _deduplicate_paths(paths: list[Path]) -> list[Path]:
    result: list[Path] = []
    seen: set[str] = set()
    for path in paths:
        try:
            normalized = str(path.expanduser().resolve(strict=False))
        except OSError:
            normalized = str(path.expanduser().absolute())
        key = normalized.lower() if os.name == "nt" else normalized
        if key not in seen:
            seen.add(key)
            result.append(Path(normalized))
    return result


def cache_dir_candidates(base_script: Path) -> list[Path]:
    """按优先级返回缓存目录候选项。

    优先使用用户显式指定的目录，其次使用脚本旁边的缓存目录。
    Linux/Android 上额外尝试 /data/local/tmp、用户缓存和 /tmp。
    """
    candidates: list[Path] = []

    explicit = os.environ.get("CPU_BENCH_CACHE_DIR")
    if explicit:
        candidates.append(Path(explicit))

    candidates.append(base_script.parent / ".cpu_native_bench_cache")

    if os.name == "nt":
        local_appdata = os.environ.get("LOCALAPPDATA")
        if local_appdata:
            candidates.append(Path(local_appdata) / "cpu_native_bench")
        candidates.append(Path(tempfile.gettempdir()) / "cpu_native_bench")
    else:
        # Android 的 /sdcard 通常是 FUSE noexec；/data/local/tmp 通常允许 ELF 执行。
        candidates.append(Path("/data/local/tmp/cpu_native_bench"))

        xdg_cache = os.environ.get("XDG_CACHE_HOME")
        if xdg_cache:
            candidates.append(Path(xdg_cache) / "cpu_native_bench")

        try:
            candidates.append(Path.home() / ".cache" / "cpu_native_bench")
        except RuntimeError:
            pass

        uid_suffix = str(os.getuid()) if hasattr(os, "getuid") else "user"
        candidates.append(Path(tempfile.gettempdir()) / f"cpu_native_bench_{uid_suffix}")

    return _deduplicate_paths(candidates)


def check_cache_dir(directory: Path) -> tuple[bool, str]:
    """实际验证目录是否可写、可 chmod，并且允许直接执行文件。"""
    write_probe = directory / f".cpu_bench_write_probe_{os.getpid()}"
    exec_probe = directory / f".cpu_bench_exec_probe_{os.getpid()}.sh"

    try:
        directory.mkdir(parents=True, exist_ok=True)

        write_probe.write_bytes(b"ok")
        write_probe.unlink(missing_ok=True)

        if os.name == "nt":
            return True, "可写"

        # 仅检查可写并不足以识别 Android /sdcard 的 noexec。
        exec_probe.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8", newline="\n")
        exec_probe.chmod(0o700)

        try:
            completed = subprocess.run(
                [str(exec_probe)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                timeout=5,
                check=False,
            )
        except PermissionError:
            return False, "目录可写但禁止执行文件（可能是 noexec/FUSE）"
        except OSError as exc:
            return False, f"执行探针失败：{exc}"

        if completed.returncode != 0:
            detail = completed.stderr.decode("utf-8", errors="replace").strip()
            return False, f"执行探针退出码 {completed.returncode}" + (
                f"：{detail}" if detail else ""
            )

        return True, "可写且可执行"
    except OSError as exc:
        return False, f"不可用：{exc}"
    finally:
        for probe in (write_probe, exec_probe):
            try:
                probe.unlink(missing_ok=True)
            except OSError:
                pass


def usable_cache_dirs(base_script: Path) -> list[Path]:
    usable: list[Path] = []
    for candidate in cache_dir_candidates(base_script):
        ok, reason = check_cache_dir(candidate)
        if ok:
            usable.append(candidate)
        else:
            print(f"⚠️ 跳过缓存目录 {candidate}：{reason}")
    return usable


def compiler_candidates() -> list[str]:
    candidates: list[str] = []
    cxx = os.environ.get("CXX")
    if cxx:
        candidates.append(cxx)
    candidates.extend(["g++", "g++.exe", "clang++", "clang++.exe", "gcc", "gcc.exe"])

    found: list[str] = []
    seen: set[str] = set()
    for candidate in candidates:
        path = shutil.which(candidate)
        if path:
            normalized = str(Path(path).resolve())
            key = normalized.lower() if os.name == "nt" else normalized
            if key not in seen:
                found.append(normalized)
                seen.add(key)
    return found


def compiler_version(compiler: str) -> str:
    try:
        completed = subprocess.run(
            [compiler, "--version"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
        return completed.stdout.splitlines()[0].strip()
    except Exception:
        return "unknown"


def build_flags(
    compiler: str,
    use_native: bool,
    opt_level: str,
) -> list[str]:
    name = Path(compiler).name.lower()
    flags = [f"-{opt_level}", "-std=c++17"]

    # 调试等级保留断言、栈帧和详细调试符号；正式等级关闭断言。
    if opt_level in {"O0", "Og"}:
        flags.extend(["-g3", "-fno-omit-frame-pointer", "-fno-inline"])
    else:
        flags.append("-DNDEBUG")

    # Windows/MinGW 的 O0 + march=native 在部分机器上可能触发异常。
    # 默认安全禁用；确需测试可显式设置 CPU_BENCH_O0_NATIVE=1。
    allow_o0_native = os.environ.get("CPU_BENCH_O0_NATIVE") == "1"
    native_enabled = use_native and not (
        os.name == "nt" and opt_level == "O0" and not allow_o0_native
    )
    if native_enabled:
        flags.append("-march=native")
    elif use_native and os.name == "nt" and opt_level == "O0":
        print("⚠️ Windows/MinGW 的 -O0 默认禁用 -march=native；可设置 CPU_BENCH_O0_NATIVE=1 强制启用")

    if os.name != "nt":
        flags.append("-pthread")
    if name.startswith("gcc") and not name.startswith("g++"):
        flags.extend(["-x", "c++"])
    return flags


def build_signature(compiler: str, flags: list[str]) -> dict[str, Any]:
    source_hash = hashlib.sha256(CPP_SOURCE.encode("utf-8")).hexdigest()
    return {
        "bench_version": BENCH_VERSION,
        "source_sha256": source_hash,
        "compiler": str(Path(compiler).resolve()),
        "compiler_version": compiler_version(compiler),
        "flags": flags,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "python_bits": 64 if sys.maxsize > 2**32 else 32,
    }


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def needs_rebuild(
    py_file: Path,
    binary: Path,
    metadata_file: Path,
    expected_signature: dict[str, Any],
    force: bool,
) -> tuple[bool, str]:
    if force:
        return True, "用户指定 --rebuild"
    if not binary.exists():
        return True, "原生二进制不存在"
    if not metadata_file.exists():
        return True, "构建信息不存在"

    actual_signature = load_json(metadata_file)
    if actual_signature != expected_signature:
        return True, "C++源码、编译器、编译参数或平台发生变化"

    try:
        if binary.stat().st_mtime_ns < py_file.stat().st_mtime_ns:
            return True, "Python脚本时间晚于原生二进制"
    except OSError:
        return True, "无法比较脚本与二进制时间"

    return False, "二进制时间和构建签名均有效"


def compile_native(
    compiler: str,
    source_file: Path,
    binary: Path,
    metadata_file: Path,
    flags: list[str],
    signature: dict[str, Any],
) -> None:
    source_file.write_text(CPP_SOURCE, encoding="utf-8", newline="\n")

    name = Path(compiler).name.lower()
    command = [compiler, *flags, str(source_file), "-o", str(binary)]
    if name.startswith("gcc") and not name.startswith("g++"):
        command.append("-lstdc++")

    print("🔨 编译命令:")
    print("   " + " ".join(f'\"{x}\"' if " " in x else x for x in command))

    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"C++编译失败，退出码 {completed.returncode}\n\n{completed.stdout}"
        )

    if not binary.exists():
        raise RuntimeError("编译器返回成功，但未生成目标二进制")

    if os.name != "nt":
        try:
            binary.chmod(0o755)
        except OSError as exc:
            raise RuntimeError(f"无法为原生二进制设置执行权限：{exc}") from exc

    metadata_file.write_text(
        json.dumps(signature, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )




def probe_native_binary(binary: Path) -> tuple[bool, str]:
    """用 C++ 程序的 --probe 零负载入口验证 ELF/EXE 能否直接执行。"""
    try:
        completed = subprocess.run(
            [str(binary), "--probe"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except PermissionError:
        return False, "权限被拒绝，所在文件系统可能使用了 noexec"
    except OSError as exc:
        return False, f"无法启动二进制：{exc}"
    except subprocess.TimeoutExpired:
        return False, "执行探针超时"

    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip()
        return False, f"执行探针退出码 {completed.returncode}" + (
            f"：{detail}" if detail else ""
        )
    return True, "可执行"


def emergency_relocate_binary(binary: Path) -> Path:
    """运行时遇到 EACCES/noexec 时，把已编译二进制迁移到可执行目录后重试。

    这是第二道保险：即使缓存目录探针因特殊 FUSE/chroot 行为误判，
    正式运行阶段仍不会直接失败。
    """
    source_parent = binary.parent.resolve(strict=False)
    last_reason = "没有可用候选目录"

    for directory in cache_dir_candidates(script_path()):
        try:
            resolved = directory.expanduser().resolve(strict=False)
        except OSError:
            resolved = directory.expanduser().absolute()

        if resolved == source_parent:
            continue

        ok, reason = check_cache_dir(resolved)
        if not ok:
            last_reason = f"{resolved}: {reason}"
            continue

        target = resolved / binary.name
        temporary = resolved / f".{binary.name}.relocate_{os.getpid()}"
        try:
            shutil.copy2(binary, temporary)
            if os.name != "nt":
                temporary.chmod(0o755)
            os.replace(temporary, target)

            probe_ok, probe_reason = probe_native_binary(target)
            if probe_ok:
                print(f"🔁 运行目录禁止执行，已自动迁移二进制到：{target}")
                return target

            last_reason = f"{target}: {probe_reason}"
        except OSError as exc:
            last_reason = f"{resolved}: {exc}"
        finally:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass

    raise RuntimeError(
        "原生二进制所在目录禁止执行，且自动迁移失败。"
        f"\n最后原因：{last_reason}"
        "\n可手动设置 CPU_BENCH_CACHE_DIR=/data/local/tmp/cpu_native_bench"
    )


def ensure_native_binary(args: argparse.Namespace) -> tuple[Path, Path, str, list[str], bool]:
    py_file = script_path()

    compilers = compiler_candidates()
    if not compilers:
        raise RuntimeError(
            "未找到 g++/clang++。\n"
            "Windows：请安装 MinGW-w64，并将 g++.exe 加入 PATH。\n"
            "Debian/Ubuntu：sudo apt install build-essential"
        )

    cache_dirs = usable_cache_dirs(py_file)
    if not cache_dirs:
        raise RuntimeError(
            "没有找到同时满足“可写且可执行”的构建目录。\n"
            "可手动指定：CPU_BENCH_CACHE_DIR=/可执行目录"
        )

    last_error: Exception | None = None

    # 先尝试脚本附近目录；若遇到 noexec、SELinux 或其他执行失败，自动切换目录重编译。
    for cache_index, cache_dir in enumerate(cache_dirs):
        if cache_index == 0:
            print(f"📁 构建缓存目录：{cache_dir}")
        else:
            print(f"🔁 正在尝试回退目录：{cache_dir}")

        binary = cache_dir / ("cpu_native_bench.exe" if os.name == "nt" else "cpu_native_bench")
        source_file = cache_dir / "cpu_native_bench.cpp"
        metadata_file = cache_dir / "build_info.json"
        cache_execution_failed = False

        for compiler in compilers:
            requested_native = not args.no_native
            attempts = [requested_native]
            if requested_native:
                attempts.append(False)  # -march=native 不可用时自动降级。

            for use_native in attempts:
                flags = build_flags(compiler, use_native, args.opt_level)
                signature = build_signature(compiler, flags)
                rebuild, reason = needs_rebuild(
                    py_file, binary, metadata_file, signature, args.rebuild
                )

                if not rebuild:
                    probe_ok, probe_reason = probe_native_binary(binary)
                    if probe_ok:
                        print(f"✅ 跳过编译：{reason}")
                        return binary, source_file, compiler, flags, False

                    print(
                        f"⚠️ 已有二进制无法执行：{probe_reason}；"
                        "将在当前目录重新编译一次。"
                    )
                    rebuild = True
                    reason = f"已有二进制执行验证失败：{probe_reason}"

                print(f"ℹ️ 需要编译：{reason}")
                try:
                    compile_native(
                        compiler,
                        source_file,
                        binary,
                        metadata_file,
                        flags,
                        signature,
                    )

                    probe_ok, probe_reason = probe_native_binary(binary)
                    if not probe_ok:
                        cache_execution_failed = True
                        last_error = RuntimeError(
                            f"目录 {cache_dir} 中生成的二进制不可执行：{probe_reason}"
                        )
                        print(
                            f"⚠️ {last_error}\n"
                            "   自动切换到下一个可执行缓存目录。"
                        )
                        break

                    print("✅ C++ 原生程序编译完成，并通过执行验证")
                    return binary, source_file, compiler, flags, True
                except Exception as exc:
                    last_error = exc
                    if use_native:
                        print("⚠️ 当前编译器使用 -march=native 失败，尝试不使用该参数。")
                        continue
                    print(f"⚠️ 当前编译器失败：{exc}")
                    break

            if cache_execution_failed:
                break

        if cache_execution_failed:
            continue

        print(f"⚠️ 构建目录 {cache_dir} 未能生成可用二进制，继续尝试回退目录。")

    raise RuntimeError(
        "所有编译器与缓存回退目录均尝试失败。"
        f"\n最后错误：\n{last_error}"
    )


def validate_args(args: argparse.Namespace) -> None:
    if args.core < 1:
        raise ValueError("核心编号采用 1 开始编号，必须大于等于 1")
    if args.threads < 0:
        raise ValueError("--threads 不能小于 0")
    if not 0.2 <= args.seconds <= 30.0:
        raise ValueError("--seconds 必须位于 0.2~30.0 之间")
    if args.repeats < 1 or args.repeats > 9 or args.repeats % 2 == 0:
        raise ValueError("--repeats 必须是 1~9 之间的奇数")
    if args.memory_mib != 0 and not 128 <= args.memory_mib <= 16384:
        raise ValueError("--memory-mib 必须为 0（自动）或位于 128~16384 之间")
    if args.opt_level not in {"O0", "Og", "O1", "O2", "O3", "Ofast", "Os"}:
        raise ValueError("--opt-level 无效")


def run_native(binary: Path, args: argparse.Namespace) -> dict[str, Any]:
    command = [
        str(binary),
        "--core",
        str(args.core),
        "--threads",
        str(args.threads),
        "--seconds",
        str(args.seconds),
        "--repeats",
        str(args.repeats),
        "--memory-mib",
        str(args.memory_mib),
    ]

    print("🚀 开始运行原生 CPU 基准测试……")
    print(f"   单核阶段：第 {args.core} 个可用逻辑处理器")
    print(f"   多核线程：{'全部可用逻辑处理器' if args.threads == 0 else args.threads}")
    print(f"   单项重复：{args.repeats} 次，取中位数")
    mode = "自动选择" if getattr(args, "memory_auto", False) else "手动指定"
    print(f"   内存工作集：{args.memory_mib} MiB（两个数组合计，{mode}）")
    total_mib = getattr(args, "total_memory_mib", None)
    available_mib = getattr(args, "available_memory_mib", None)
    if total_mib is not None and available_mib is not None:
        print(f"   物理内存：总计约 {total_mib} MiB，可用约 {available_mib} MiB")
    elif available_mib is not None:
        print(f"   当前可用内存：约 {available_mib} MiB")

    def execute(executable: Path) -> subprocess.CompletedProcess[str]:
        command[0] = str(executable)
        return subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
        )

    started = time.perf_counter()
    try:
        completed = execute(binary)
    except PermissionError:
        binary = emergency_relocate_binary(binary)
        completed = execute(binary)
    except OSError as exc:
        if exc.errno in (errno.EACCES, errno.EPERM):
            binary = emergency_relocate_binary(binary)
            completed = execute(binary)
        else:
            raise
    wall_seconds = time.perf_counter() - started

    if completed.returncode != 0:
        raise RuntimeError(
            f"原生基准程序运行失败，退出码 {completed.returncode}\n"
            f"标准输出：\n{completed.stdout}\n"
            f"错误输出：\n{completed.stderr}"
        )

    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"原生程序输出不是有效JSON：{exc}\n原始输出：\n{completed.stdout}"
        ) from exc

    result["python_wall_seconds"] = wall_seconds
    return result


def score(throughput_miter_s: float, reference_miter_s: float) -> int:
    if throughput_miter_s <= 0 or not math.isfinite(throughput_miter_s):
        return 0
    return int(round(throughput_miter_s / reference_miter_s * 10000.0))


def geometric_score(a: int, b: int) -> int:
    if a <= 0 or b <= 0:
        return 0
    return int(round(math.sqrt(a * b)))


def geometric_mean_values(*values: float) -> float:
    if not values or any(value <= 0 or not math.isfinite(value) for value in values):
        return 0.0
    return math.exp(sum(math.log(value) for value in values) / len(values))


def print_results(result: dict[str, Any], compiler: str, flags: list[str]) -> None:
    metrics = result["metrics"]

    si = metrics["single_integer"]
    sf = metrics["single_float"]
    mi = metrics["multi_integer"]
    mf = metrics["multi_float"]
    smr = metrics["single_memory_read"]
    smw = metrics["single_memory_write"]
    smc = metrics["single_memory_copy"]
    mmr = metrics["multi_memory_read"]
    mmw = metrics["multi_memory_write"]
    mmc = metrics["multi_memory_copy"]

    si_score = score(float(si["miter_s"]), REFERENCE_INT_MITER_S)
    sf_score = score(float(sf["miter_s"]), REFERENCE_FLOAT_MITER_S)
    mi_score = score(float(mi["miter_s"]), REFERENCE_INT_MITER_S)
    mf_score = score(float(mf["miter_s"]), REFERENCE_FLOAT_MITER_S)

    single_score = geometric_score(si_score, sf_score)
    multi_score = geometric_score(mi_score, mf_score)
    cpu_total_score = geometric_score(single_score, multi_score)

    memory_multi_geomean = geometric_mean_values(
        float(mmr["gb_s"]), float(mmw["gb_s"]), float(mmc["gb_s"])
    )
    memory_score = int(round(memory_multi_geomean / REFERENCE_MEMORY_GB_S * 10000.0))
    platform_score = int(round(geometric_mean_values(cpu_total_score, memory_score)))

    multi_int_scale = float(mi["miter_s"]) / float(si["miter_s"])
    multi_float_scale = float(mf["miter_s"]) / float(sf["miter_s"])
    memory_read_scale = float(mmr["gb_s"]) / float(smr["gb_s"])
    memory_write_scale = float(mmw["gb_s"]) / float(smw["gb_s"])
    memory_copy_scale = float(mmc["gb_s"]) / float(smc["gb_s"])

    print()
    print("=" * 76)
    print("🧠 CPU 与内存原生基准测试结果")
    print("=" * 76)
    print(f"系统            : {platform.platform()}")
    print(f"架构            : {platform.machine()}")
    print(f"Python          : {platform.python_version()}")
    print(f"C++编译器       : {compiler_version(compiler)}")
    print(f"编译参数        : {' '.join(flags)}")
    print(f"可用逻辑处理器  : {result['available_cpus']}")
    print(
        f"单核绑定        : 第 {result['selected_core_one_based']} 个可用逻辑处理器"
        f"（OS CPU {result['selected_os_cpu']}）"
    )
    print(f"多核线程数      : {result['threads']}")
    print("多核 CPU 模式   : 固定时长总吞吐（快核心可完成更多迭代）")
    print(f"每项正式重复    : {result['repeats']} 次，按吞吐率取中位数")
    print("内存访问模式    : 双数组交替 + 多线程分片轮换")
    print(
        f"内存工作集      : 总计 {result['memory_mib']} MiB，"
        f"每个数组约 {result['memory_array_mib']:.1f} MiB"
    )
    print(
        f"每线程每数组    : 约 {result['memory_array_per_thread_mib']:.1f} MiB"
    )
    if float(result["memory_array_per_thread_mib"]) < 8.0:
        print("ℹ️ 每线程分片较小；测试会跨轮次轮换分片并交替两个数组，以抑制缓存驻留。")
    print("-" * 76)
    print(
        f"单核整数：{si['seconds']:8.4f} 秒 | "
        f"{si['miter_s']:10.3f} Miter/s | 得分 {si_score:8d}"
    )
    print(
        f"单核浮点：{sf['seconds']:8.4f} 秒 | "
        f"{sf['miter_s']:10.3f} Miter/s | 得分 {sf_score:8d}"
    )
    print(f"🌟 单核 CPU 综合得分：{single_score}")
    print("-" * 76)
    print(
        f"多核整数：{mi['seconds']:8.4f} 秒 | "
        f"{mi['miter_s']:10.3f} Miter/s | 得分 {mi_score:8d} | "
        f"相对单核 {multi_int_scale:6.2f}x"
    )
    print(
        f"多核浮点：{mf['seconds']:8.4f} 秒 | "
        f"{mf['miter_s']:10.3f} Miter/s | 得分 {mf_score:8d} | "
        f"相对单核 {multi_float_scale:6.2f}x"
    )
    print(f"🌟 多核 CPU 综合得分：{multi_score}")
    print("-" * 76)
    print("💾 内存顺序带宽（GB/s，复制按读取+写入两倍流量计算）")
    print(
        f"单线程读取：{smr['seconds']:8.4f} 秒 | {smr['gb_s']:10.3f} GB/s"
    )
    print(
        f"单线程写入：{smw['seconds']:8.4f} 秒 | {smw['gb_s']:10.3f} GB/s"
    )
    print(
        f"单线程复制：{smc['seconds']:8.4f} 秒 | {smc['gb_s']:10.3f} GB/s"
    )
    print(
        f"多线程读取：{mmr['seconds']:8.4f} 秒 | {mmr['gb_s']:10.3f} GB/s | "
        f"相对单线程 {memory_read_scale:6.2f}x"
    )
    print(
        f"多线程写入：{mmw['seconds']:8.4f} 秒 | {mmw['gb_s']:10.3f} GB/s | "
        f"相对单线程 {memory_write_scale:6.2f}x"
    )
    print(
        f"多线程复制：{mmc['seconds']:8.4f} 秒 | {mmc['gb_s']:10.3f} GB/s | "
        f"相对单线程 {memory_copy_scale:6.2f}x"
    )
    print(f"🌟 内存带宽综合得分：{memory_score}")
    print("=" * 76)
    print(f"🏆 CPU 总体综合得分：{cpu_total_score}")
    print(f"🏆 平台综合得分（CPU+内存）：{platform_score}")
    print(f"Python端总运行时间：{result['python_wall_seconds']:.2f} 秒")
    print("=" * 76)
    print(
        "说明：多核CPU采用固定时长吞吐；内存测试采用双数组交替和分片轮换。"
        "写入未计入写分配等隐藏总线流量。"
    )


def main() -> int:
    args = parse_args()
    try:
        print(f"🧩 完整版脚本版本：{BENCH_VERSION}（CPU + 内存带宽 + noexec 自动回退）")
        print(f"🛠️ 请求的编译器优化等级：-{args.opt_level}")
        print(f"📄 脚本路径：{script_path()}")
        validate_args(args)
        resolved_memory_mib, memory_auto, total_mib, available_mib = resolve_memory_mib(args)
        args.memory_mib = resolved_memory_mib
        args.memory_auto = memory_auto
        args.total_memory_mib = total_mib
        args.available_memory_mib = available_mib
        binary, source_file, compiler, flags, compiled = ensure_native_binary(args)
        result = run_native(binary, args)
        print_results(result, compiler, flags)

        if compiled and not args.keep_source:
            try:
                source_file.unlink(missing_ok=True)
            except OSError:
                pass
        elif args.keep_source:
            print(f"C++源码位置：{source_file}")

        return 0
    except KeyboardInterrupt:
        print("\n测试已由用户中止。", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"\n❌ {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
