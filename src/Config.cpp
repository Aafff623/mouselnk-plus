#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <system_error>

#include "resource.h"

namespace config {
namespace {

// ---------- 小工具 ----------

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                           nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          out.data(), need);
    return out;
}

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// 拒绝未知字段：这些结构体是我们自己的 schema，静默丢弃用户写的字段比报错更糟。
bool RejectUnknownKeys(const json& object, std::initializer_list<const char*> allowed,
                       const std::string& where, std::string* error) {
    if (!object.is_object()) {
        if (error) {
            *error = where + " 应该是一个对象。";
        }
        return false;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const char* key : allowed) {
            if (it.key() == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            if (error) {
                *error = where + " 里存在未知字段 \"" + it.key() + "\"；如果这是新版程序写入的，请升级程序。";
            }
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------- JSON 映射 ----------
//
// 这些 to_json 必须是 config 命名空间的成员，nlohmann::json 通过 ADL 查找它们；
// 放在匿名命名空间里会导致 json 无法从我们的类型隐式构造（编译期报 C2679）。

void FromJson(const json& j, Point& p) {
    if (!j.is_array() || j.size() != 2) {
        throw json::type_error::create(302, "坐标点必须是 [x, y] 两个数字", &j);
    }
    p.x = j.at(0).get<double>();
    p.y = j.at(1).get<double>();
}

void to_json(json& j, const Point& p) {
    j = json::array({p.x, p.y});
}

void FromJson(const json& j, GestureTemplate& g, std::string* error) {
    if (!RejectUnknownKeys(j, {"Id", "Points"}, "Gestures 的条目", error)) {
        throw json::type_error::create(302, error ? *error : "非法手势模板", &j);
    }
    g.id = j.value("Id", std::string());
    g.points.clear();
    const json points = j.value("Points", json::array());
    if (!points.is_array()) {
        throw json::type_error::create(302, "Gestures 的 Points 必须是数组", &j);
    }
    for (const auto& item : points) {
        Point p;
        FromJson(item, p);
        g.points.push_back(p);
    }
}

void to_json(json& j, const GestureTemplate& g) {
    j = json::object();
    j["Id"] = g.id;
    j["Points"] = g.points;
}

// 动作只强约束 Type；其余字段整体保留，新增参数不需要改结构体。
void FromJson(const json& j, Action& a, std::string* error) {
    if (!j.is_object()) {
        if (error) {
            *error = "动作必须是对象，例如 {\"Type\":\"SendKeys\",\"Keys\":\"Ctrl+C\"}";
        }
        throw json::type_error::create(302, error ? *error : "非法动作", &j);
    }
    a.raw = j;
    a.type = j.value("Type", std::string());
}

void to_json(json& j, const Action& a) {
    j = a.raw;
}

void FromJson(const json& j, Binding& b, const std::string& where, std::string* error) {
    if (!RejectUnknownKeys(j, {"GestureId", "Name", "Enabled", "Actions"}, where, error)) {
        throw json::type_error::create(302, error ? *error : "非法绑定", &j);
    }
    b.gestureId = j.value("GestureId", std::string());
    b.name = j.value("Name", std::string());
    b.enabled = j.value("Enabled", true);
    b.actions.clear();
    const json actions = j.value("Actions", json::array());
    if (!actions.is_array()) {
        throw json::type_error::create(302, where + " 的 Actions 必须是数组", &j);
    }
    for (const auto& item : actions) {
        Action a;
        FromJson(item, a, error);
        b.actions.push_back(a);
    }
}

void to_json(json& j, const Binding& b) {
    j = json::object();
    j["GestureId"] = b.gestureId;
    j["Name"] = b.name;
    j["Enabled"] = b.enabled;
    j["Actions"] = b.actions;
}

void FromJson(const json& j, AppRule& r, std::string* error) {
    if (!RejectUnknownKeys(j, {"Name", "Programs", "IgnoreGlobal", "Bindings"},
                           "MatchCustom 的条目", error)) {
        throw json::type_error::create(302, error ? *error : "非法应用规则", &j);
    }
    r.name = j.value("Name", std::string());
    r.ignoreGlobal = j.value("IgnoreGlobal", false);
    r.programs.clear();
    const json programs = j.value("Programs", json::array());
    if (!programs.is_array()) {
        throw json::type_error::create(302, "MatchCustom 的 Programs 必须是数组", &j);
    }
    for (const auto& item : programs) {
        r.programs.push_back(item.get<std::string>());
    }
    r.bindings.clear();
    const json bindings = j.value("Bindings", json::array());
    if (!bindings.is_array()) {
        throw json::type_error::create(302, "MatchCustom 的 Bindings 必须是数组", &j);
    }
    for (const auto& item : bindings) {
        Binding b;
        FromJson(item, b, "MatchCustom/" + r.name + " 的绑定", error);
        r.bindings.push_back(b);
    }
}

void to_json(json& j, const AppRule& r) {
    j = json::object();
    j["Name"] = r.name;
    j["Programs"] = r.programs;
    j["IgnoreGlobal"] = r.ignoreGlobal;
    j["Bindings"] = r.bindings;
}

void FromJson(const json& j, GeneralSettings& g, std::string* error) {
    if (!RejectUnknownKeys(j, {"Language", "StartOnBoot", "IgnoreFullScreen", "ShowTrayIcon"},
                           "General", error)) {
        throw json::type_error::create(302, error ? *error : "非法 General", &j);
    }
    g.language = j.value("Language", g.language);
    g.startOnBoot = j.value("StartOnBoot", g.startOnBoot);
    g.ignoreFullScreen = j.value("IgnoreFullScreen", g.ignoreFullScreen);
    g.showTrayIcon = j.value("ShowTrayIcon", g.showTrayIcon);
}

void to_json(json& j, const GeneralSettings& g) {
    j = json::object();
    j["Language"] = g.language;
    j["StartOnBoot"] = g.startOnBoot;
    j["IgnoreFullScreen"] = g.ignoreFullScreen;
    j["ShowTrayIcon"] = g.showTrayIcon;
}

void FromJson(const json& j, GestureSettings& g, std::string* error) {
    if (!RejectUnknownKeys(j,
                           {"Enabled", "StartDistance", "TimeoutMs", "Sensitivity", "DrawTrace",
                            "DrawResult", "TraceWidth", "TraceArrow", "DrawColor", "FailColor",
                            "RandColor", "FontSize", "RestoreOnFailure"},
                           "MouseGesture", error)) {
        throw json::type_error::create(302, error ? *error : "非法 MouseGesture", &j);
    }
    g.enabled = j.value("Enabled", g.enabled);
    g.startDistance = j.value("StartDistance", g.startDistance);
    g.timeoutMs = j.value("TimeoutMs", g.timeoutMs);
    g.sensitivity = j.value("Sensitivity", g.sensitivity);
    g.drawTrace = j.value("DrawTrace", g.drawTrace);
    g.drawResult = j.value("DrawResult", g.drawResult);
    g.traceWidth = j.value("TraceWidth", g.traceWidth);
    g.traceArrow = j.value("TraceArrow", g.traceArrow);
    g.drawColor = j.value("DrawColor", g.drawColor);
    g.failColor = j.value("FailColor", g.failColor);
    g.randColor = j.value("RandColor", g.randColor);
    g.fontSize = j.value("FontSize", g.fontSize);
    g.restoreOnFailure = j.value("RestoreOnFailure", g.restoreOnFailure);
}

void to_json(json& j, const GestureSettings& g) {
    j = json::object();
    j["Enabled"] = g.enabled;
    j["StartDistance"] = g.startDistance;
    j["TimeoutMs"] = g.timeoutMs;
    j["Sensitivity"] = g.sensitivity;
    j["DrawTrace"] = g.drawTrace;
    j["DrawResult"] = g.drawResult;
    j["TraceWidth"] = g.traceWidth;
    j["TraceArrow"] = g.traceArrow;
    j["DrawColor"] = g.drawColor;
    j["FailColor"] = g.failColor;
    j["RandColor"] = g.randColor;
    j["FontSize"] = g.fontSize;
    j["RestoreOnFailure"] = g.restoreOnFailure;
}

json ToJson(const Config& c) {
    json j = json::object();
    j["SchemaVersion"] = c.schemaVersion;
    j["General"] = c.general;
    j["MouseGesture"] = c.gesture;
    j["Gestures"] = c.gestures;
    j["MatchGlobal"] = c.matchGlobal;
    j["MatchCustom"] = c.matchCustom;
    j["Excludes"] = c.excludes;
    j["Hotkeys"] = c.hotkeys;
    j["WheelEdge"] = c.wheelEdge;
    j["HotCorner"] = c.hotCorner;
    j["ClipboardMenu"] = c.clipboardMenu;
    return j;
}

// 尚未实现行为的区块（Hotkeys/WheelEdge/HotCorner/ClipboardMenu）按不透明 JSON 保留，
// 但至少要求类型正确，避免写坏后无从判断。
bool ValidateOpaqueSections(const json& j, std::string* error) {
    for (const char* key : {"Hotkeys", "WheelEdge", "HotCorner", "ClipboardMenu"}) {
        auto it = j.find(key);
        if (it != j.end() && !it->is_object() && !it->is_null()) {
            if (error) {
                *error = std::string(key) + " 应该是一个对象。";
            }
            return false;
        }
    }
    return true;
}

bool FromJson(const json& j, Config& c, std::string* error) {
    if (!j.is_object()) {
        if (error) {
            *error = "配置文件的根应该是一个对象。";
        }
        return false;
    }
    if (!RejectUnknownKeys(j,
                           {"SchemaVersion", "General", "MouseGesture", "Gestures", "MatchGlobal",
                            "MatchCustom", "Excludes", "Hotkeys", "WheelEdge", "HotCorner",
                            "ClipboardMenu"},
                           "配置根", error)) {
        return false;
    }
    if (!ValidateOpaqueSections(j, error)) {
        return false;
    }

    c.schemaVersion = j.value("SchemaVersion", 0);
    if (c.schemaVersion == 0) {
        if (error) {
            *error = "配置缺少 SchemaVersion 字段。";
        }
        return false;
    }

    try {
        if (auto it = j.find("General"); it != j.end()) {
            FromJson(*it, c.general, error);
        }
        if (auto it = j.find("MouseGesture"); it != j.end()) {
            FromJson(*it, c.gesture, error);
        }
        c.gestures.clear();
        if (auto it = j.find("Gestures"); it != j.end()) {
            if (!it->is_array()) {
                if (error) {
                    *error = "Gestures 必须是数组。";
                }
                return false;
            }
            for (const auto& item : *it) {
                GestureTemplate g;
                FromJson(item, g, error);
                c.gestures.push_back(std::move(g));
            }
        }
        c.matchGlobal.clear();
        if (auto it = j.find("MatchGlobal"); it != j.end()) {
            if (!it->is_array()) {
                if (error) {
                    *error = "MatchGlobal 必须是数组。";
                }
                return false;
            }
            for (const auto& item : *it) {
                Binding b;
                FromJson(item, b, "MatchGlobal 的绑定", error);
                c.matchGlobal.push_back(std::move(b));
            }
        }
        c.matchCustom.clear();
        if (auto it = j.find("MatchCustom"); it != j.end()) {
            if (!it->is_array()) {
                if (error) {
                    *error = "MatchCustom 必须是数组。";
                }
                return false;
            }
            for (const auto& item : *it) {
                AppRule r;
                FromJson(item, r, error);
                c.matchCustom.push_back(std::move(r));
            }
        }
        c.excludes.clear();
        if (auto it = j.find("Excludes"); it != j.end()) {
            if (!it->is_array()) {
                if (error) {
                    *error = "Excludes 必须是数组。";
                }
                return false;
            }
            for (const auto& item : *it) {
                c.excludes.push_back(item.get<std::string>());
            }
        }
        if (auto it = j.find("Hotkeys"); it != j.end()) {
            c.hotkeys = *it;
        }
        if (auto it = j.find("WheelEdge"); it != j.end()) {
            c.wheelEdge = *it;
        }
        if (auto it = j.find("HotCorner"); it != j.end()) {
            c.hotCorner = *it;
        }
        if (auto it = j.find("ClipboardMenu"); it != j.end()) {
            c.clipboardMenu = *it;
        }
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("配置内容格式错误：") + e.what();
        }
        return false;
    }
    return true;
}

namespace {

// ---------- 文件 IO ----------

bool ReadFileBytes(const std::filesystem::path& path, std::string* out, std::string* error) {
    HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "无法读取 " + ToUtf8(path.wstring()) + "（win32 错误 " +
                     std::to_string(::GetLastError()) + "）。";
        }
        return false;
    }
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(handle, &size) || size.QuadPart > (64LL << 20)) {
        ::CloseHandle(handle);
        if (error) {
            *error = "配置文件过大或无法读取。";
        }
        return false;
    }
    std::string buffer(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = buffer.empty() ||
                    ::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    ::CloseHandle(handle);
    if (!ok) {
        if (error) {
            *error = "读取配置文件失败。";
        }
        return false;
    }
    buffer.resize(read);

    // 去掉 UTF-8 BOM（有些编辑器会加），否则解析会失败。
    if (buffer.size() >= 3 && static_cast<unsigned char>(buffer[0]) == 0xEF &&
        static_cast<unsigned char>(buffer[1]) == 0xBB &&
        static_cast<unsigned char>(buffer[2]) == 0xBF) {
        buffer.erase(0, 3);
    }
    *out = std::move(buffer);
    return true;
}

// 原子写：先写同目录临时文件，再整体替换。断电或崩溃不会留下半截配置。
bool WriteFileAtomic(const std::filesystem::path& path, const std::string& utf8, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);   // 失败留给后面的写入报错

    const std::filesystem::path temp = path.wstring() + L".tmp";
    HANDLE handle = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = "无法写入 " + ToUtf8(temp.wstring()) + "（win32 错误 " +
                     std::to_string(::GetLastError()) + "）。目录是否可写？";
        }
        return false;
    }
    DWORD written = 0;
    const BOOL ok = ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
    if (!ok || written != utf8.size()) {
        ::DeleteFileW(temp.c_str());
        if (error) {
            *error = "写入配置文件失败（磁盘空间不足？）。";
        }
        return false;
    }

    if (!::MoveFileExW(temp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = ::GetLastError();
        ::DeleteFileW(temp.c_str());
        if (error) {
            *error = "替换配置文件失败（win32 错误 " + std::to_string(err) + "）。";
        }
        return false;
    }
    return true;
}

// 损坏配置不删除，改名留档，便于用户找回自己写过的内容。
bool BackupCorruptFile(const std::filesystem::path& path, std::string* backupName) {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t suffix[64]{};
    ::swprintf_s(suffix, L".corrupt-%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
    const std::filesystem::path target = path.wstring() + suffix;
    if (!::MoveFileExW(path.c_str(), target.c_str(), 0)) {
        return false;
    }
    if (backupName != nullptr) {
        *backupName = ToUtf8(target.filename().wstring());
    }
    return true;
}

}  // namespace

// ---------- 公开接口 ----------

bool IsPseudoGesture(const std::string& gestureId) {
    return EqualsIgnoreCase(gestureId, "WheelSwitchUp") ||
           EqualsIgnoreCase(gestureId, "WheelSwitchDown");
}

std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size() - 1) {
            buffer.resize(written);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path ExecutableDirectory() {
    const std::filesystem::path exe = ExecutablePath();
    return exe.empty() ? std::filesystem::path() : exe.parent_path();
}

std::filesystem::path ResolveConfigPath(bool* portableOut) {
    const std::filesystem::path exeDir = ExecutableDirectory();

    bool portable = false;
    if (!exeDir.empty()) {
        std::error_code ec;
        portable = std::filesystem::exists(exeDir / kPortableMarker, ec);
    }
    if (portableOut != nullptr) {
        *portableOut = portable;
    }
    if (portable) {
        return exeDir / L"mouselnk-plus.json";
    }

    wchar_t appData[MAX_PATH]{};
    const DWORD len = ::GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    const std::filesystem::path base = (len > 0 && len < MAX_PATH) ? std::filesystem::path(appData)
                                                                  : exeDir;
    return base / L"mouselnk-plus" / L"config.json";
}

bool Store::Validate(const Config& c, std::string* error) {
    auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    if (c.schemaVersion > kSchemaVersion) {
        return fail("配置的 SchemaVersion(" + std::to_string(c.schemaVersion) +
                    ") 高于本程序支持的版本(" + std::to_string(kSchemaVersion) +
                    ")，请升级程序后再用。");
    }
    if (c.schemaVersion < kSchemaVersion) {
        return fail("配置的 SchemaVersion(" + std::to_string(c.schemaVersion) +
                    ") 低于当前版本(" + std::to_string(kSchemaVersion) + ")，需要迁移。");
    }

    if (c.gesture.startDistance < 0) {
        return fail("MouseGesture.StartDistance 不能为负数（单位是物理像素）。");
    }
    if (c.gesture.timeoutMs <= 0) {
        return fail("MouseGesture.TimeoutMs 必须大于 0。");
    }
    if (c.gesture.sensitivity < 0 || c.gesture.sensitivity > 100) {
        return fail("MouseGesture.Sensitivity 必须在 0 到 100 之间。");
    }
    if (c.gesture.traceWidth < 1) {
        return fail("MouseGesture.TraceWidth 必须至少为 1。");
    }
    if (c.gesture.fontSize < 1) {
        return fail("MouseGesture.FontSize 必须至少为 1。");
    }

    std::set<std::string> gestureIds;
    for (const auto& g : c.gestures) {
        if (g.id.empty()) {
            return fail("Gestures 中存在没有 Id 的模板。");
        }
        if (g.points.size() < 2) {
            return fail("手势模板 \"" + g.id + "\" 至少需要 2 个点。");
        }
        for (const auto& p : g.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                return fail("手势模板 \"" + g.id + "\" 含有非法坐标。");
            }
        }
        if (!gestureIds.insert(g.id).second) {
            return fail("手势模板 \"" + g.id + "\" 重复定义。");
        }
    }

    auto checkBindings = [&](const std::vector<Binding>& list, const std::string& where) {
        for (const auto& b : list) {
            const std::string label = b.name.empty() ? std::string("(未命名)") : b.name;
            if (b.gestureId.empty()) {
                return fail(where + " 的绑定 \"" + label + "\" 缺少 GestureId。");
            }
            if (gestureIds.find(b.gestureId) == gestureIds.end() && !IsPseudoGesture(b.gestureId)) {
                return fail(where + " 的绑定 \"" + label + "\" 引用了不存在的模板 \"" +
                            b.gestureId + "\"。");
            }
            for (const auto& a : b.actions) {
                if (a.type.empty()) {
                    return fail(where + " 的绑定 \"" + label + "\" 中有动作缺少 Type。");
                }
            }
        }
        return true;
    };

    if (!checkBindings(c.matchGlobal, "MatchGlobal")) {
        return false;
    }
    for (const auto& rule : c.matchCustom) {
        const std::string label = rule.name.empty() ? std::string("(未命名)") : rule.name;
        if (rule.programs.empty()) {
            return fail("MatchCustom 的规则 \"" + label + "\" 没有指定 Programs。");
        }
        for (const auto& p : rule.programs) {
            if (p.empty()) {
                return fail("MatchCustom 的规则 \"" + label + "\" 里有空的程序名。");
            }
        }
        if (!checkBindings(rule.bindings, "MatchCustom/" + label)) {
            return false;
        }
    }
    for (const auto& e : c.excludes) {
        if (e.empty()) {
            return fail("Excludes 中存在空字符串。");
        }
    }
    return true;
}

bool Store::ReadDefaultFromResource(json* out, std::string* error) {
    const HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC resource = ::FindResourceW(module, MAKEINTRESOURCEW(IDR_DEFAULT_CONFIG),
                                     MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (resource == nullptr) {
        if (error) {
            *error = "程序内没有找到默认配置资源（构建时漏掉 resources/default-config.json？）。";
        }
        return false;
    }
    const DWORD size = ::SizeofResource(module, resource);
    HGLOBAL loaded = ::LoadResource(module, resource);
    if (loaded == nullptr || size == 0) {
        if (error) {
            *error = "读取默认配置资源失败。";
        }
        return false;
    }
    const void* data = ::LockResource(loaded);
    if (data == nullptr) {
        if (error) {
            *error = "锁定默认配置资源失败。";
        }
        return false;
    }
    const std::string text(static_cast<const char*>(data), size);
    try {
        *out = json::parse(text);
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("内嵌默认配置不是合法 JSON：") + e.what();
        }
        return false;
    }
    return true;
}

Store& Store::Instance() {
    static Store instance;
    return instance;
}

LoadResult Store::Load() {
    const std::filesystem::path path = ResolveConfigPath(&m_portable);
    m_path = path;
    m_pathString = ToUtf8(path.wstring());
    LoadResult result = LoadInto(path, true);
    m_initialized = true;
    return result;
}

LoadResult Store::Reload() {
    LoadResult result = LoadInto(m_path, true);
    return result;
}

LoadResult Store::LoadInto(const std::filesystem::path& path, bool createIfMissing) {
    LoadResult result;
    result.path = ToUtf8(path.wstring());

    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);

    if (!exists) {
        if (!createIfMissing) {
            result.message = "配置文件不存在：" + result.path;
            return result;
        }
        json defaultJson;
        std::string error;
        if (!ReadDefaultFromResource(&defaultJson, &error)) {
            result.message = error;
            return result;
        }
        Config candidate;
        if (!FromJson(defaultJson, candidate, &error) || !Validate(candidate, &error)) {
            // 内嵌默认值自身不合法属于构建缺陷，必须报出来而不是带病运行。
            result.message = "内嵌默认配置未通过验证：" + error;
            return result;
        }
        if (!WriteFileAtomic(path, defaultJson.dump(2), &error)) {
            result.message = "无法生成默认配置：" + error;
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config = candidate;
        }
        result.outcome = LoadOutcome::CreatedDefault;
        result.message = "已生成默认配置：" + result.path;
        return result;
    }

    std::string text;
    std::string error;
    if (!ReadFileBytes(path, &text, &error)) {
        result.message = error;
        return result;
    }

    Config candidate;
    bool usable = false;
    try {
        const json parsed = json::parse(text);
        usable = FromJson(parsed, candidate, &error) && Validate(candidate, &error);
    } catch (const std::exception& e) {
        error = std::string("配置不是合法 JSON：") + e.what();
    }

    if (usable) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_config = candidate;
        }
        result.outcome = LoadOutcome::Loaded;
        result.message = "配置已载入。";
        if (createIfMissing) {
            // Load() 首次成功读入时不需要额外动作
        }
        return result;
    }

    // 损坏：备份原文件，再回落默认值，不静默丢用户内容。
    std::string backupName;
    const bool backedUp = BackupCorruptFile(path, &backupName);

    json defaultJson;
    std::string defaultError;
    if (!ReadDefaultFromResource(&defaultJson, &defaultError)) {
        result.message = "配置损坏（" + error + "），且无法读取内嵌默认配置：" + defaultError;
        return result;
    }
    Config fallback;
    if (!FromJson(defaultJson, fallback, &defaultError) || !Validate(fallback, &defaultError)) {
        result.message = "配置损坏（" + error + "），且内嵌默认配置未通过验证：" + defaultError;
        return result;
    }
    std::string writeError;
    if (!WriteFileAtomic(path, defaultJson.dump(2), &writeError)) {
        result.message = "配置损坏（" + error + "），且无法写回默认配置：" + writeError;
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = fallback;
    }
    result.outcome = LoadOutcome::RecoveredCorrupt;
    result.message = "配置损坏：" + error + "。已改用默认配置" +
                     (backedUp ? ("，原文件备份为 " + backupName) : std::string("（备份失败）")) + "。";
    return result;
}

bool Store::Save(const Config& candidate, std::string* error) {
    if (!Validate(candidate, error)) {
        return false;
    }
    const std::filesystem::path path = m_path.empty() ? ResolveConfigPath(nullptr) : m_path;
    if (!WriteFileAtomic(path, ToJson(candidate).dump(2), error)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = candidate;
    return true;
}

Config Store::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

}  // namespace config
