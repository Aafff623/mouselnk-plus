// 钩子存活看门狗判定逻辑的自动化测试（T-0033）。
//
// 覆盖纯函数 ShouldReinstallForSilence 的全部语义分支：
//   Raw Input 在动 + 钩子静默 3s 以上 => 判死重装
//   Raw Input 也静默               => 只是没人动鼠标，不重装
// 另验证边界值（2999/3000、2000/2001）与 GetTickCount 环绕。

#include <doctest/doctest.h>

#include <cstdint>

#include "Watchdog.h"

using input::Watchdog;

TEST_CASE("看门狗判定：双静默 = 没人动鼠标，不重装") {
    const std::uint32_t now = 100000;
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(now, now - 60000, now - 60000));
}

TEST_CASE("看门狗判定：钩子活着 + Raw Input 活跃，不重装") {
    const std::uint32_t now = 100000;
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(now, now - 50, now - 50));
}

TEST_CASE("看门狗判定：Raw Input 活跃 + 钩子静默达到 3s 才判死") {
    const std::uint32_t now = 100000;
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(now, now - 2999, now - 10));
    CHECK(Watchdog::ShouldReinstallForSilence(now, now - 3000, now - 10));
}

TEST_CASE("看门狗判定：Raw Input 也静默（超过 2s）时钩子静默不是判死依据") {
    const std::uint32_t now = 100000;
    // 用户一分钟没动鼠标：钩子当然也静默，但这不是钩子被摘。
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(now, now - 60000, now - 2001));
    CHECK(Watchdog::ShouldReinstallForSilence(now, now - 60000, now - 2000));
}

TEST_CASE("看门狗判定：GetTickCount 环绕（跨 0xFFFFFFFF）仍正确") {
    // now 刚绕过 0，事件时间戳在环绕前。无符号减法天然处理环绕。
    const std::uint32_t now = 0x1000;
    const std::uint32_t lastRaw = now - 1000;        // 1s 前（环绕前）：Raw 活跃
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(now, now - 272, lastRaw));   // 钩子 0.27s 前还在
    CHECK(Watchdog::ShouldReinstallForSilence(now, now - 4352, lastRaw));        // 钩子 4.35s 没响
}

TEST_CASE("看门狗判定：两个时间戳都为 0（尚未有任何事件）不判死") {
    // now - 0 是个巨大值，钩子一定「静默」；但 Raw Input 同样没有任何活动，
    // 语义上等于「没人动鼠标」，不能判死。
    CHECK_FALSE(Watchdog::ShouldReinstallForSilence(100000, 0, 0));
}
