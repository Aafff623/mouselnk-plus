#pragma once

#include <filesystem>
#include <string>

// 最小诊断日志。
//
// 定位：只做「错误与诊断信息」这一件事——本地文件、带体积上限、线程安全。
// 按调研结论（docs/execution-plan.md 选择 D-4）不引入 spdlog：轮转上限与诊断包导出
// 本来就要自己写，spdlog 只省了「写文件 + 按大小轮转」这一小段，却要付出翻译单元与
// 必须进 CI 量化的体积增量，而本项目体积是硬约束。
//
// 重要：**钩子回调里不允许调用本模块**（内部有锁）。钩子回调把要记录的内容写进
// 预分配缓冲区，再由 App 线程取走并写日志。
namespace logger {

enum class Level { Info, Warn, Error };

// 初始化：file 为日志文件路径，maxBytes 为轮转阈值（超过则改名为 .1 后重开）。
void Init(const std::filesystem::path& file, unsigned long long maxBytes = 512 * 1024);

// 关闭（进程退出时调用，保证缓冲落盘）。
void Shutdown();

void Write(Level level, const std::string& utf8Message);

inline void Info(const std::string& m) { Write(Level::Info, m); }
inline void Warn(const std::string& m) { Write(Level::Warn, m); }
inline void Error(const std::string& m) { Write(Level::Error, m); }

// 当前日志文件路径（供「关于」与诊断导出显示）。
const std::string& Path();

}  // namespace logger
