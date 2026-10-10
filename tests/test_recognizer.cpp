// 轨迹模板识别的自动化测试（作者规格「五、轨迹模板识别」与「十七、验证与交付」）。
//
// 覆盖规格点名要求处理的情形：等距重采样、环形角度差、模板匹配边界，
// 以及空轨迹、单点、零长度线段、浮点误差、角度环绕、畸形模板、过长轨迹。
// 另验证「上下、左右不能互相混淆」。

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "Recognizer.h"

using gesture::PointD;
using gesture::Recognizer;

namespace {

constexpr double kPi = 3.14159265358979323846;

// 屏幕坐标：y 向下增大。所以「上」是 y 减小。
std::vector<PointD> Line(double x0, double y0, double x1, double y1, int n = 10) {
    std::vector<PointD> v;
    v.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(n - 1);
        v.push_back(PointD{x0 + (x1 - x0) * t, y0 + (y1 - y0) * t});
    }
    return v;
}

// 与默认配置一致的模板集（见 resources/default-config.json）。
std::vector<gesture::GestureTemplate> DefaultTemplates() {
    return {
        {"UP", {{0, 100}, {0, 0}}},
        {"DOWN", {{0, 0}, {0, 100}}},
        {"LEFT", {{100, 0}, {0, 0}}},
        {"RIGHT", {{0, 0}, {100, 0}}},
        {"/ UP", {{0, 100}, {100, 0}}},
        {"/ DOWN", {{0, 0}, {100, 100}}},
        {"DOWN-RIGHT", {{0, 0}, {0, 100}, {100, 100}}},
    };
}

Recognizer MakeRecognizer(int sensitivity = 50) {
    Recognizer r;
    r.SetTemplates(DefaultTemplates());
    r.SetSensitivity(sensitivity);
    return r;
}

}  // namespace

TEST_CASE("等距重采样把直线均分为 100 段 101 点") {
    std::vector<PointD> out;
    REQUIRE(Recognizer::Resample(Line(0, 0, 100, 0, 2), out));
    REQUIRE(out.size() == static_cast<size_t>(Recognizer::kSampleCount));
    CHECK(out.front().x == doctest::Approx(0.0));
    CHECK(out.back().x == doctest::Approx(100.0));
    CHECK(out.front().y == doctest::Approx(0.0));
    CHECK(out.back().y == doctest::Approx(0.0));
    // 等距：相邻点间距应约等于总长/100
    for (size_t i = 1; i < out.size(); ++i) {
        CHECK(std::hypot(out[i].x - out[i - 1].x, out[i].y - out[i - 1].y) ==
              doctest::Approx(1.0).epsilon(1e-6));
    }
}

TEST_CASE("重采样拒绝退化输入") {
    std::vector<PointD> out;

    SUBCASE("空轨迹") { CHECK_FALSE(Recognizer::Resample({}, out)); }
    SUBCASE("单点") { CHECK_FALSE(Recognizer::Resample({{5, 5}}, out)); }
    SUBCASE("零长度线段（两点重合）") {
        CHECK_FALSE(Recognizer::Resample({{5, 5}, {5, 5}}, out));
    }
    SUBCASE("连续重复点被去重后仍不足") {
        CHECK_FALSE(Recognizer::Resample({{5, 5}, {5, 5}, {5, 5}}, out));
    }
    SUBCASE("连续重复点被去重后正常重采样") {
        // 中间夹重复点不应影响结果
        std::vector<PointD> in = {{0, 0}, {0, 0}, {50, 0}, {100, 0}, {100, 0}};
        REQUIRE(Recognizer::Resample(in, out));
        CHECK(out.back().x == doctest::Approx(100.0));
    }
}

TEST_CASE("最短环形角差") {
    CHECK(Recognizer::CircularDiff(0.0, 0.0) == doctest::Approx(0.0));
    CHECK(Recognizer::CircularDiff(0.0, kPi) == doctest::Approx(kPi));
    CHECK(Recognizer::CircularDiff(kPi, 0.0) == doctest::Approx(kPi));   // 对称
    // 角度环绕：350° 与 10° 相差 20°，不是 340°
    const double a = 350.0 * kPi / 180.0;
    const double b = 10.0 * kPi / 180.0;
    CHECK(Recognizer::CircularDiff(a, b) == doctest::Approx(20.0 * kPi / 180.0));
    // -π 与 +π 是同一个方向
    CHECK(Recognizer::CircularDiff(-kPi, kPi) == doctest::Approx(0.0).scale(1.0));
}

TEST_CASE("评分：完全一致为 100，方向相反接近 0") {
    double same[Recognizer::kSegments];
    double opposite[Recognizer::kSegments];
    for (int i = 0; i < Recognizer::kSegments; ++i) {
        same[i] = 0.3;
        opposite[i] = 0.3 + kPi;
    }
    CHECK(Recognizer::Score(same, same) == doctest::Approx(100.0));
    CHECK(Recognizer::Score(same, opposite) == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("阈值随灵敏度变化") {
    Recognizer r;
    r.SetSensitivity(0);
    CHECK(r.threshold() == doctest::Approx(60.0));
    r.SetSensitivity(50);
    CHECK(r.threshold() == doctest::Approx(75.0));
    r.SetSensitivity(100);
    CHECK(r.threshold() == doctest::Approx(90.0));
}

TEST_CASE("识别四方向且不互相混淆") {
    const Recognizer r = MakeRecognizer();

    SUBCASE("上划匹配 UP") {
        const auto res = r.Recognize(Line(500, 500, 500, 300));
        REQUIRE(res.matched);
        CHECK(res.templateId == "UP");
        CHECK(res.score > 95.0);
    }
    SUBCASE("下划匹配 DOWN") {
        const auto res = r.Recognize(Line(500, 300, 500, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "DOWN");
    }
    SUBCASE("左划匹配 LEFT") {
        const auto res = r.Recognize(Line(500, 500, 300, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "LEFT");
    }
    SUBCASE("右划匹配 RIGHT") {
        const auto res = r.Recognize(Line(300, 500, 500, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "RIGHT");
    }
}

TEST_CASE("上下不互混、左右不互混：方向相反分数接近 0") {
    const Recognizer r = MakeRecognizer(0);   // 阈值降到 60，仍不应误匹配反向

    SUBCASE("上划不会匹配 DOWN") {
        const auto res = r.Recognize(Line(500, 500, 500, 300));
        REQUIRE(res.matched);
        CHECK(res.templateId == "UP");
        for (const auto& c : res.candidates) {
            if (c.id == "DOWN") {
                CHECK(c.score < 5.0);
            }
        }
    }
    SUBCASE("左划不会匹配 RIGHT") {
        const auto res = r.Recognize(Line(500, 500, 300, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "LEFT");
        for (const auto& c : res.candidates) {
            if (c.id == "RIGHT") {
                CHECK(c.score < 5.0);
            }
        }
    }
}

TEST_CASE("垂直与水平相差 90 度，在默认阈值下互不匹配") {
    // 只放 UP 一个模板：左划应当因分数 50 < 75 而判为无匹配
    Recognizer r;
    r.SetTemplates({{"UP", {{0, 100}, {0, 0}}}});
    r.SetSensitivity(50);
    const auto res = r.Recognize(Line(500, 500, 300, 500));
    CHECK_FALSE(res.matched);
}

TEST_CASE("斜线优先匹配斜线模板而非正交模板") {
    const Recognizer r = MakeRecognizer();

    SUBCASE("右上斜线匹配 / UP") {
        const auto res = r.Recognize(Line(300, 500, 500, 300));
        REQUIRE(res.matched);
        CHECK(res.templateId == "/ UP");
    }
    SUBCASE("右下斜线匹配 / DOWN") {
        const auto res = r.Recognize(Line(300, 300, 500, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "/ DOWN");
    }
}

TEST_CASE("折线匹配：下→右 匹配 DOWN-RIGHT") {
    const Recognizer r = MakeRecognizer();
    std::vector<PointD> trace = {{0, 0}, {0, 50}, {0, 100}, {50, 100}, {100, 100}};
    const auto res = r.Recognize(trace);
    REQUIRE(res.matched);
    CHECK(res.templateId == "DOWN-RIGHT");
}

TEST_CASE("畸形模板被跳过，不影响其它模板") {
    Recognizer r;
    r.SetTemplates({
        {"BAD_EMPTY", {}},
        {"BAD_SINGLE", {{0, 0}}},
        {"BAD_ZERO", {{0, 0}, {0, 0}}},
        {"UP", {{0, 100}, {0, 0}}},
    });
    r.SetSensitivity(50);
    CHECK(r.templateCount() == 4);   // 畸形模板保留在列表里（供诊断计数）

    const auto res = r.Recognize(Line(500, 500, 500, 300));
    REQUIRE(res.matched);
    CHECK(res.templateId == "UP");
    // 畸形模板不产生候选
    for (const auto& c : res.candidates) {
        CHECK(c.id == "UP");
    }
}

TEST_CASE("过长轨迹（点数远超采样数）仍可识别") {
    const Recognizer r = MakeRecognizer();
    const auto res = r.Recognize(Line(0, 500, 0, 0, 5000));
    REQUIRE(res.matched);
    CHECK(res.templateId == "UP");
    CHECK(res.score > 95.0);
}

TEST_CASE("退化轨迹不产生匹配") {
    const Recognizer r = MakeRecognizer();
    CHECK_FALSE(r.Recognize({}).matched);
    CHECK_FALSE(r.Recognize({{10, 10}}).matched);
    CHECK_FALSE(r.Recognize({{10, 10}, {10, 10}}).matched);
}

TEST_CASE("浮点误差：轻微抖动的直线仍匹配原方向") {
    const Recognizer r = MakeRecognizer();
    std::vector<PointD> trace;
    for (int i = 0; i <= 20; ++i) {
        trace.push_back(PointD{500.0 + 0.5 * ((i % 2) ? 1.0 : -1.0), 500.0 - i * 10.0});
    }
    const auto res = r.Recognize(trace);
    REQUIRE(res.matched);
    CHECK(res.templateId == "UP");
}

TEST_CASE("从状态机轨迹转换") {
    gesture::Trace t;
    t.pointCount = 3;
    t.points[0].p = {10, 20};
    t.points[1].p = {30, 40};
    t.points[2].p = {50, 60};
    const auto pts = Recognizer::FromTrace(t);
    REQUIRE(pts.size() == 3);
    CHECK(pts[0].x == doctest::Approx(10.0));
    CHECK(pts[2].y == doctest::Approx(60.0));
}

// ---------------------------------------------------------------------------
// MouseInc 2.13.4 内置的全部 36 条模板（实测自本机 MouseInc.json 的 Gestures 数组）。
// 用来验证「36 条模板均可识别」这条验收断言，而不是只测默认配置里的那几条。
// ---------------------------------------------------------------------------

namespace {

gesture::GestureTemplate T(const char* id, std::initializer_list<double> flat) {
    gesture::GestureTemplate t;
    t.id = id;
    auto it = flat.begin();
    while (it != flat.end()) {
        const double x = *it++;
        const double y = *it++;
        t.points.push_back(PointD{x, y});
    }
    return t;
}

std::vector<gesture::GestureTemplate> AllMouseIncTemplates() {
    return {
        T("UP", {0, 100, 0, 0}),
        T("DOWN", {0, 0, 0, 100}),
        T("LEFT", {100, 0, 0, 0}),
        T("RIGHT", {0, 0, 100, 0}),
        T("\\ UP", {100, 100, 0, 0}),
        T("\\ DOWN", {0, 0, 100, 100}),
        T("/ UP", {0, 100, 100, 0}),
        T("/ DOWN", {100, 0, 0, 100}),
        T("UP-DOWN", {40, 100, 50, 0, 60, 105}),
        T("DOWN-UP", {40, 0, 50, 100, 60, -5}),
        T("LEFT-RIGHT", {100, 40, 0, 50, 105, 60}),
        T("RIGHT-LEFT", {0, 40, 100, 50, -5, 60}),
        T("UP-LEFT", {100, 100, 100, 0, 0, 0}),
        T("UP-RIGHT", {0, 100, 0, 0, 100, 0}),
        T("DOWN-LEFT", {100, 0, 100, 100, 0, 100}),
        T("DOWN-RIGHT", {0, 0, 0, 100, 100, 100}),
        T("LEFT-UP", {100, 100, 0, 100, 0, 0}),
        T("LEFT-DOWN", {100, 0, 0, 0, 0, 100}),
        T("RIGHT-UP", {0, 100, 100, 100, 100, 0}),
        T("RIGHT-DOWN", {0, 0, 100, 0, 100, 100}),
        T("UP-RIGHT-UP", {0, 100, 0, 50, 50, 50, 50, -5}),
        T("DOWN-RIGHT-DOWN", {0, 0, 0, 50, 50, 50, 50, 105}),
        T("SQUARE", {0, 0, 0, 100, 100, 100, 100, 0, 5, 0}),
        T("SQUARE 2", {100, 0, 100, 100, 0, 100, 0, 0, 95, 0}),
        T("M", {0, 100, 25, 0, 50, 100, 75, 0, 100, 105}),
        T("W", {0, 0, 25, 100, 50, 0, 75, 100, 100, -5}),
        T("C", {50, 0, 30, 4, 16, 13, 6, 25, 2, 34, 0, 50, 2, 66, 6, 75, 16, 87, 30, 96, 55, 100}),
        T("O", {50, 0, 30, 4, 16, 13, 6, 25, 2, 34, 0, 50, 2, 66, 6, 75, 16, 87, 30, 96,
                50, 100, 70, 96, 84, 87, 94, 75, 98, 66, 100, 50, 98, 34, 94, 25, 84, 13,
                70, 4, 55, 0}),
        T("P", {30, 100, 30, 0, 70, 0, 70, 50, 40, 50}),
        T("R", {30, 100, 30, 0, 70, 0, 70, 50, 40, 50, 70, 105}),
        T("B", {30, 100, 30, 0, 70, 0, 70, 45, 40, 50, 70, 55, 70, 100, 35, 100}),
        T("h", {30, 0, 30, 100, 30, 50, 50, 50, 70, 50, 70, 105}),
        T("N", {0, 100, 0, 0, 70, 100, 70, -5}),
        T("S", {70, 0, 0, 0, 0, 50, 70, 50, 70, 100, -5, 100}),
        T("Z", {0, 0, 70, 0, 0, 100, 75, 100}),
        T("3", {0, 0, 50, 20, 0, 40, 50, 60, -5, 80}),
    };
}

}  // namespace

TEST_CASE("MouseInc 内置 36 条模板全部有效且可自我识别") {
    const auto templates = AllMouseIncTemplates();
    REQUIRE(templates.size() == 36);

    Recognizer r;
    r.SetTemplates(templates);
    r.SetSensitivity(50);
    REQUIRE(r.templateCount() == 36);

    for (const auto& t : templates) {
        // 把模板放大 4 倍并平移到屏幕另一处，模拟「用户按同样形状在别处画」
        std::vector<PointD> trace;
        trace.reserve(t.points.size());
        for (const PointD& p : t.points) {
            trace.push_back(PointD{300.0 + p.x * 4.0, 400.0 + p.y * 4.0});
        }

        INFO("template=", t.id);
        const auto res = r.Recognize(trace);
        REQUIRE(res.matched);
        CHECK(res.templateId == t.id);
        CHECK(res.score == doctest::Approx(100.0).epsilon(1e-6));
    }
}

TEST_CASE("36 条模板下，四方向与其反向仍不互相混淆") {
    Recognizer r;
    r.SetTemplates(AllMouseIncTemplates());
    r.SetSensitivity(50);

    // 上划 → UP，且 DOWN 分数接近 0
    {
        const auto res = r.Recognize(Line(500, 500, 500, 300));
        REQUIRE(res.matched);
        CHECK(res.templateId == "UP");
        for (const auto& c : res.candidates) {
            if (c.id == "DOWN") {
                CHECK(c.score < 5.0);
            }
        }
    }
    // 左划 → LEFT，且 RIGHT 分数接近 0
    {
        const auto res = r.Recognize(Line(500, 500, 300, 500));
        REQUIRE(res.matched);
        CHECK(res.templateId == "LEFT");
        for (const auto& c : res.candidates) {
            if (c.id == "RIGHT") {
                CHECK(c.score < 5.0);
            }
        }
    }
}

