#pragma once

#include <string>
#include <vector>

#include "GestureState.h"

// 轨迹模板识别（作者规格「五、轨迹模板识别」）。
//
// 算法是**整体路径的模板匹配**，不是把轨迹压成上下左右方向字符串：
//   去连续重复点 → 累计路径长度 → 按长度等距重采样为 100 段（101 点）
//   → 每段 atan2 方向角（100 维特征）→ 与模板逐段取最短环形角差
//   → score = 100 × (1 − 平均角差 / π) → 取达到阈值的最高分
//
// 因为比较的是**方向角**，位置与整体大小天然不影响结果（方向与平移、缩放无关），
// 但绘制方向差异被保留 —— 上下、左右不会互相混淆。
//
// 本模块是纯逻辑：不碰 Win32、不分配在热路径之外，因此可以在测试工程里独立编译。
namespace gesture {

struct PointD {
    double x = 0.0;
    double y = 0.0;
};

// 一条模板：归一化坐标（0..100 量级，允许 -5 / 105 这类越界点作为出入笔信号）。
struct GestureTemplate {
    std::string id;
    std::vector<PointD> points;
};

struct Candidate {
    std::string id;
    double score = 0.0;
};

struct MatchResult {
    bool matched = false;
    std::string templateId;
    double score = 0.0;
    std::vector<Candidate> candidates;   // 按分数降序，供诊断查看
};

class Recognizer {
public:
    static constexpr int kSegments = 100;               // 100 个线段
    static constexpr int kSampleCount = kSegments + 1;  // 101 个点

    // 等距重采样为 101 点。去重后点数不足、或总长接近零时返回 false（拒绝识别）。
    static bool Resample(const std::vector<PointD>& in, std::vector<PointD>& out);

    // 101 点 → 100 维方向角（弧度，-π..π）
    static void Directions(const std::vector<PointD>& pts, double out[kSegments]);

    // 两个角度的最短环形差，范围 0..π
    static double CircularDiff(double a, double b);

    // 100 维方向特征的相似度评分（0..100）
    static double Score(const double a[kSegments], const double b[kSegments]);

    void SetTemplates(std::vector<GestureTemplate> templates);
    void SetSensitivity(int sensitivity) { m_sensitivity = sensitivity; }
    int  sensitivity() const { return m_sensitivity; }

    // 通过阈值：threshold = 60 + 30 × sensitivity / 100
    double threshold() const;

    size_t templateCount() const { return m_templates.size(); }

    // 对一条轨迹做识别。返回 matched=false 表示无匹配。
    MatchResult Recognize(const std::vector<PointD>& trace) const;

    // 便捷：把状态机产出的轨迹点转成识别用的点序列。
    static std::vector<PointD> FromTrace(const Trace& t);

private:
    struct Prepared {
        std::string id;
        double      dirs[kSegments]{};
        bool        valid = false;
    };

    std::vector<Prepared> m_templates;
    int m_sensitivity = 50;
};

}  // namespace gesture
