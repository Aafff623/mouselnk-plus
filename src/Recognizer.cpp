#include "Recognizer.h"

#include <algorithm>
#include <cmath>

namespace gesture {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// 「总长度接近零」的判据（物理像素）。手势起点阈值是 5px，所以正常轨迹远大于它；
// 这里只用来挡住退化的输入，不承担「手势太小」的语义。
constexpr double kMinLength = 1e-6;

// 去连续重复点。浮点比较用绝对容差：重采样后的点不会完全相等，但原始输入可能重复。
std::vector<PointD> Dedup(const std::vector<PointD>& in) {
    std::vector<PointD> out;
    out.reserve(in.size());
    for (const PointD& p : in) {
        if (out.empty() || std::fabs(p.x - out.back().x) > 1e-9 ||
            std::fabs(p.y - out.back().y) > 1e-9) {
            out.push_back(p);
        }
    }
    return out;
}

}  // namespace

bool Recognizer::Resample(const std::vector<PointD>& in, std::vector<PointD>& out) {
    out.clear();
    const std::vector<PointD> pts = Dedup(in);
    if (pts.size() < 2) {
        return false;   // 点数不足
    }

    // 累计路径长度
    std::vector<double> cum(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        cum[i] = cum[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    const double total = cum.back();
    if (!(total > kMinLength)) {
        return false;   // 总长度接近零
    }

    out.resize(kSampleCount);
    size_t seg = 1;
    for (int i = 0; i < kSampleCount; ++i) {
        const double target = total * static_cast<double>(i) / kSegments;
        while (seg < pts.size() - 1 && cum[seg] < target) {
            ++seg;
        }
        const double segLen = cum[seg] - cum[seg - 1];
        double t = 0.0;
        if (segLen > 1e-12) {
            t = (target - cum[seg - 1]) / segLen;
        }
        t = std::clamp(t, 0.0, 1.0);
        out[i].x = pts[seg - 1].x + (pts[seg].x - pts[seg - 1].x) * t;
        out[i].y = pts[seg - 1].y + (pts[seg].y - pts[seg - 1].y) * t;
    }
    return true;
}

void Recognizer::Directions(const std::vector<PointD>& pts, double out[kSegments]) {
    for (int i = 0; i < kSegments; ++i) {
        out[i] = std::atan2(pts[i + 1].y - pts[i].y, pts[i + 1].x - pts[i].x);
    }
}

double Recognizer::CircularDiff(double a, double b) {
    double d = std::fmod(std::fabs(a - b), kTwoPi);
    if (d > kPi) {
        d = kTwoPi - d;
    }
    return d;   // 0..π
}

double Recognizer::Score(const double a[kSegments], const double b[kSegments]) {
    double sum = 0.0;
    for (int i = 0; i < kSegments; ++i) {
        sum += CircularDiff(a[i], b[i]);
    }
    const double mean = sum / kSegments;
    // 注意：这是**评分**，不是统计概率，日志与界面都不得称其为概率。
    return 100.0 * (1.0 - mean / kPi);
}

void Recognizer::SetTemplates(std::vector<GestureTemplate> templates) {
    m_templates.clear();
    m_templates.reserve(templates.size());
    for (GestureTemplate& t : templates) {
        Prepared p;
        p.id = std::move(t.id);
        std::vector<PointD> resampled;
        if (Resample(t.points, resampled)) {
            Directions(resampled, p.dirs);
            p.valid = true;
        }
        // 畸形模板（点不足 / 零长度）不参与匹配，但保留在列表里以便诊断计数。
        m_templates.push_back(std::move(p));
    }
}

double Recognizer::threshold() const {
    return 60.0 + 30.0 * static_cast<double>(m_sensitivity) / 100.0;
}

MatchResult Recognizer::Recognize(const std::vector<PointD>& trace) const {
    MatchResult result;

    std::vector<PointD> pts;
    if (!Resample(trace, pts)) {
        return result;   // 无匹配
    }

    double inDirs[kSegments]{};
    Directions(pts, inDirs);

    const double thr = threshold();
    double best = -1.0;
    std::string bestId;

    result.candidates.reserve(m_templates.size());
    for (const Prepared& t : m_templates) {
        if (!t.valid) {
            continue;
        }
        const double s = Score(inDirs, t.dirs);
        result.candidates.push_back(Candidate{t.id, s});
        // 严格大于：同分时保留先出现的模板（配置顺序），保证结果确定。
        if (s >= thr && s > best) {
            best = s;
            bestId = t.id;
        }
    }

    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    if (best >= 0.0) {
        result.matched = true;
        result.templateId = std::move(bestId);
        result.score = best;
    }
    return result;
}

std::vector<PointD> Recognizer::FromTrace(const Trace& t) {
    std::vector<PointD> out;
    out.reserve(static_cast<size_t>(t.pointCount));
    for (int i = 0; i < t.pointCount; ++i) {
        out.push_back(PointD{static_cast<double>(t.points[i].p.x),
                             static_cast<double>(t.points[i].p.y)});
    }
    return out;
}

}  // namespace gesture
