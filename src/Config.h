#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// 配置系统（作者规格「十四、设置与配置」）。
//
// 职责：定位配置文件 → 读取并做语义验证 → 验证通过才应用 → 原子写回 →
// 损坏时备份并回落默认。所有读取都通过 Store::Snapshot() 取一份完整副本，
// 因此重载过程中调用方看到的要么是旧的一份、要么是新的一份，不会是半成品。
namespace config {

// 用有序 JSON：写回的配置文件保持稳定的字段顺序，便于用户手工编辑与看 diff。
using json = nlohmann::ordered_json;

// 当前 schema 版本。改动不兼容的字段时必须递增，并在 Migrate() 里补上升级路径。
inline constexpr int kSchemaVersion = 1;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

// 手势模板：作者规格用点对数组表达归一化坐标（0..100 量级，允许略微越界作为出入笔信号）。
struct GestureTemplate {
    std::string id;                 // 如 "UP"、"/ DOWN"、"SQUARE"
    std::vector<Point> points;
};

// 动作：只强约束 Type，其余字段原样保留。
// 动作的参数集是随任务扩展的（Execute 的提权/等待、Screenshot 的输出方式等），
// 因此这里不做字段建模，避免每次加参数都要改结构体；但 Type 必须存在。
struct Action {
    std::string type;
    json raw;
};

struct Binding {
    std::string gestureId;
    std::string name;
    bool enabled = true;
    std::vector<Action> actions;
};

struct AppRule {
    std::string name;
    std::vector<std::string> programs;   // 程序名或完整路径，匹配时忽略大小写
    bool ignoreGlobal = false;
    std::vector<Binding> bindings;
};

struct GeneralSettings {
    std::string language = "zh-CN";
    bool startOnBoot = false;
    bool ignoreFullScreen = true;
    bool showTrayIcon = true;
};

struct GestureSettings {
    bool enabled = true;
    int startDistance = 5;      // 单位：物理像素
    int timeoutMs = 1000;
    int sensitivity = 50;       // 0..100
    bool drawTrace = true;
    bool drawResult = true;
    int traceWidth = 3;
    bool traceArrow = true;
    std::string drawColor = "#E47542";
    std::string failColor = "#CAD0D3";
    bool randColor = false;
    int fontSize = 26;
    bool restoreOnFailure = false;
};

struct Config {
    int schemaVersion = kSchemaVersion;
    GeneralSettings general;
    GestureSettings gesture;
    std::vector<GestureTemplate> gestures;
    std::vector<Binding> matchGlobal;
    std::vector<AppRule> matchCustom;
    std::vector<std::string> excludes;

    // 以下区块的「行为」尚未实现（对应的 TODO 任务在后几个阶段）。
    // 它们先按不透明 JSON 原样保留：字段已在文件里、读写往返不丢数据，
    // 等各自的任务落地时再把结构体建起来。
    json hotkeys = json::object();
    json wheelEdge = json::object();
    json hotCorner = json::object();
    json clipboardMenu = json::object();
};

enum class LoadOutcome {
    Loaded,            // 成功读入用户配置
    CreatedDefault,    // 首次运行，已生成默认配置
    RecoveredCorrupt,  // 原配置损坏，已备份并改用默认配置
    Failed,            // 无法建立可用配置（例如目录不可写）
};

struct LoadResult {
    LoadOutcome outcome = LoadOutcome::Failed;
    std::string message;   // 面向用户的中文说明
    std::string path;      // 实际使用的配置文件路径
};

// 配置文件的定位与读写。
class Store {
public:
    static Store& Instance();

    LoadResult Load();     // 启动时调用：不存在则生成默认，损坏则备份后回落
    LoadResult Reload();   // 托盘「重载配置」：读入候选并验证，通过才替换当前快照
    bool Save(const Config& candidate, std::string* error);   // 原子写回

    // 取一份完整副本。配置很小，复制比加引用计数更简单也更难出错。
    Config Snapshot() const;

    const std::string& Path() const { return m_pathString; }
    bool IsPortable() const { return m_portable; }

    // 解析 + 语义验证。只有返回 true 时才可以应用该配置。
    static bool Validate(const Config& candidate, std::string* error);

private:
    Store() = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // 从内嵌资源读取默认配置（首次运行用）。
    static bool ReadDefaultFromResource(json* out, std::string* error);

    LoadResult LoadInto(const std::filesystem::path& path, bool createIfMissing);

    mutable std::mutex m_mutex;
    Config m_config;
    std::filesystem::path m_path;
    std::string m_pathString;
    bool m_portable = false;
    bool m_initialized = false;
};

// 决定配置文件位置：exe 同目录存在标记文件则用便携模式，否则用 %APPDATA%。
std::filesystem::path ResolveConfigPath(bool* portableOut);

// 当前进程的可执行文件路径与其所在目录（App 与 Config 共用，避免各自实现一份）。
std::filesystem::path ExecutablePath();
std::filesystem::path ExecutableDirectory();

// 便携模式的标记文件名（与 exe 同目录）。
inline constexpr const wchar_t* kPortableMarker = L"mouselnk-plus.portable";

// 伪手势：不是画出来的形状，而是按住右键滚轮时产生的标识。
bool IsPseudoGesture(const std::string& gestureId);

}  // namespace config
