// Where does the time go when a rewrite collides with a reader?
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <windows.h>

namespace fs = std::filesystem;
using clock_t_ = std::chrono::steady_clock;

static double ms_since(clock_t_::time_point t0)
{
  return std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();
}

int main()
{
  const fs::path dir = fs::temp_directory_path() / "shim_micro";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path target = dir / "1.graph";
  const fs::path temp = dir / "1.graph.tmp";
  { std::ofstream out(target, std::ios::binary); out << "#nav2shim-graph\t1\n#end\n"; }

  std::atomic<bool> stop{false};
  std::atomic<long> reads{0};
  std::thread reader([&] {
    while (!stop.load()) {
      HANDLE h = CreateFileW(target.wstring().c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h != INVALID_HANDLE_VALUE) {
        char b[4096]; DWORD got = 0;
        while (ReadFile(h, b, sizeof(b), &got, nullptr) && got > 0) {}
        CloseHandle(h);
      }
      ++reads;
    }
  });

  double move_ok = 0, move_fail = 0, replace_ok = 0, replace_fail = 0, write_temp = 0;
  long n_move_ok = 0, n_move_fail = 0, n_replace_ok = 0, n_replace_fail = 0;

  const int rounds = 2000;
  for (int i = 0; i < rounds; ++i) {
    auto t = clock_t_::now();
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      out << "#nav2shim-graph\t1\nservice\t/n" << i << "\tstd_srvs/srv/Trigger\tlocal\n#end\n"; }
    write_temp += ms_since(t);

    t = clock_t_::now();
    const bool moved = MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(),
                                   MOVEFILE_REPLACE_EXISTING) != 0;
    if (moved) { move_ok += ms_since(t); ++n_move_ok; continue; }
    move_fail += ms_since(t); ++n_move_fail;

    t = clock_t_::now();
    const bool replaced = ReplaceFileW(target.wstring().c_str(), temp.wstring().c_str(), nullptr,
                                       REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
    if (replaced) { replace_ok += ms_since(t); ++n_replace_ok; continue; }
    replace_fail += ms_since(t); ++n_replace_fail;
    MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(), 0);
  }
  stop.store(true);
  reader.join();

  std::printf("rounds %d, reader opens %ld\n", rounds, reads.load());
  std::printf("write temp    %8.2f ms total\n", write_temp);
  std::printf("MoveFileEx ok %8.2f ms over %ld   | fail %8.2f ms over %ld\n",
              move_ok, n_move_ok, move_fail, n_move_fail);
  std::printf("Replace ok    %8.2f ms over %ld   | fail %8.2f ms over %ld\n",
              replace_ok, n_replace_ok, replace_fail, n_replace_fail);
  return 0;
}
