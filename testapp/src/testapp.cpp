#define NOMINMAX

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <iostream>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>
#include <iomanip>
#include <string>
#include <functional>
#include <set>
#include <ctime>
#include <cstdio>
#include <cctype>
#include <cmath>

#include "JoyConDecoder.h"
#include "DsuServer.h"
#include <Windows.h>
#include <ViGEm/Client.h>
#include <ViGEm/Common.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

constexpr uint16_t JOYCON_MANUFACTURER_ID = 1363;
const std::vector<uint8_t> JOYCON_MANUFACTURER_PREFIX = { 0x01, 0x00, 0x03, 0x7E };



const wchar_t* SESSION_CHAR_UUID       = L"00c5af5d-1964-4e30-8f51-1956f96bd282";
const wchar_t* NATIVE_INPUT_UUID_R     = L"d5a9e01e-2ffc-4cca-b20c-8b67142bf442";
const wchar_t* NATIVE_INPUT_UUID_L     = L"cc1bbbb5-7354-4d32-a716-a81cb241a32a";
const wchar_t* NATIVE_ENABLE_DESC_UUID = L"679d5510-5a24-4dee-9557-95df80486ecb";
const wchar_t* INPUT_REPORT_UUID  = L"ab7de9be-89fe-49ad-828f-118f09df7fd2";
const wchar_t* WRITE_COMMAND_UUID = L"649d4ac9-8eb7-4e6c-af44-1ea54fe5f005";


const wchar_t* CMD_RESPONSE_UUID_BASIC = L"c765a961-d9d8-4d36-a20a-5315b111836a";
const wchar_t* CMD_RESPONSE_UUID_L     = L"63a3810f-aec7-474b-9010-3d52403cb996";
const wchar_t* CMD_RESPONSE_UUID_R     = L"640ca58e-0e88-410c-a7f3-426faf2b690b";
const wchar_t* CMD_RESPONSE_UUID_PRO   = L"506d9f7d-4278-4e95-a549-326ba77657e0";
const wchar_t* CMD_RESPONSE_UUID_GC    = L"46f6ad29-cdaf-4569-a2fe-339020b94604";

const wchar_t* RUMBLE_CHAR_UUID_L   = L"ce49a830-dced-48ae-931e-c8cf88aadbea";
const wchar_t* RUMBLE_CHAR_UUID_R   = L"65a724b3-f1e7-4a61-8078-a342376b27ff";
const wchar_t* RUMBLE_CHAR_UUID_PRO = L"3dacbc7e-6955-40b5-8eaf-6f9809e8b379";
const wchar_t* RUMBLE_CHAR_UUID_GC  = L"af95885e-44b3-4a24-9cf0-483cc129469a";

const wchar_t* VIBRATION_CHAR_UUID_L = L"289326cb-a471-485d-a8f4-240c14f18241";
const wchar_t* VIBRATION_CHAR_UUID_R = L"fa19b0fb-cd1f-46a7-84a1-bbb09e00c149";
const wchar_t* VIBRATION_CHAR_UUID_PRO = L"cc483f51-9258-427d-a939-630c31f72b05";
const wchar_t* VIBRATION_CHAR_UUID_GC  = L"3f8fb670-ab25-45bf-b540-38c72834d064";

const std::string CONFIG_FILE = "joycon2cpp_config.json";



enum class DeviceKind { Any, JoyConL, JoyConR, Pro, GC };



constexpr uint16_t PID_JOYCON_R = 0x2066;
constexpr uint16_t PID_JOYCON_L = 0x2067;
constexpr uint16_t PID_PRO      = 0x2069;
constexpr uint16_t PID_GC       = 0x2073;


constexpr size_t FULL_REPORT_LEN = 0x3C;

enum class UpdatePolicy { LowLatency, Balanced120Hz, Legacy60Hz };
enum ControllerType { SingleJoyCon = 1, DualJoyCon = 2, ProController = 3, NSOGCController = 4 };

enum class ButtonMapping {
    NONE, L3, R3, L1, R1, L2, R2,
    CROSS, CIRCLE, SQUARE, TRIANGLE,
    SHARE, OPTIONS,
    DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT
};

enum class AppScreen { Setup, Connecting, Running };

struct GLGRLayout {
    char name[64] = "Layout 1";
    ButtonMapping glMapping = ButtonMapping::NONE;
    ButtonMapping grMapping = ButtonMapping::NONE;
};

struct ProControllerConfig {
    std::vector<GLGRLayout> layouts;
    int activeLayoutIndex = 0;
};

struct RuntimeOptions {
    bool latencyMetrics = false;
    UpdatePolicy updatePolicy = UpdatePolicy::LowLatency;
    char latencyCsvPath[256] = "latency_benchmark.csv";
    bool swapABXY = false;
};

struct PlayerConfig {
    ControllerType controllerType = SingleJoyCon;
    JoyConSide     joyconSide        = JoyConSide::Left;
    JoyConOrientation joyconOrientation = JoyConOrientation::Upright;
    GyroSource     gyroSource        = GyroSource::Both;
    GyroMode       gyroMode          = GyroMode::Raw;
    bool           mouseEnabled      = true;  
    DpadMode       dpadMode          = DpadMode::On; 
    RemapSideways  remapSideways     = RemapSideways::No; 
};


struct RxStats {
    std::string           label;
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> fullCount{0};
    std::atomic<uint32_t> lastLen{0};
    std::mutex            mtx;
    std::vector<uint8_t>  lastPacket;
    std::vector<uint8_t>  lastButtons;
    int                   changeLogs = 0;
    std::vector<uint8_t>  lastLoggedPkt;
    std::chrono::steady_clock::time_point lastLogTime{};
    int                   logLines = 0;
    bool                  captureFlag = false;
    std::atomic<uint64_t> nonZeroMask{0};
};


struct FauxGyroSettings {
    std::atomic<bool> enabled{true};
    std::atomic<bool> invX{false}, invY{false}, invZ{false};
    std::atomic<int>  gainPercent{100};
    std::atomic<int>  dualSource{1};
};
static FauxGyroSettings g_fg;

struct FauxGyroState {
    std::mutex mtx;
    bool       hasQuat = false;
    uint16_t   prevTs = 0;
    float      prevQ[4] = { 1.f, 0.f, 0.f, 0.f };
    float      out[3] = { 0.f, 0.f, 0.f };
};

struct ConnectedJoyCon {
    BluetoothLEDevice      device    = nullptr;
    GattCharacteristic     inputChar = nullptr;
    GattCharacteristic     writeChar = nullptr;
    GattCharacteristic     rumbleChar = nullptr;
    GattCharacteristic     vibrationChar = nullptr;
    GattCharacteristic     vibrationCharLeft = nullptr;
    GattCharacteristic     vibrationCharRight = nullptr;
    GattCharacteristic     cmdRespBasicChar = nullptr;
    GattCharacteristic     cmdRespExtChar = nullptr;
    GattCharacteristic     sessionChar = nullptr;
    GattCharacteristic     nativeInputChar = nullptr;
    std::shared_ptr<RxStats> nativeRx;
    bool                   consoleMode = false;
    std::shared_ptr<FauxGyroState> fg;
    std::shared_ptr<RxStats> rx;
    std::vector<std::pair<GattCharacteristic, std::shared_ptr<RxStats>>> extraInputs;
    uint64_t               address   = 0;
    uint16_t               productId = 0;
    std::string            initStrategy;
};

using SteadyClock = std::chrono::steady_clock;
using TimePoint   = SteadyClock::time_point;

struct LatencyTracker {
    TimePoint lastBleTime{};
    TimePoint lastEmitTime{};
    uint64_t  eventIndex = 0;
};

struct TimedInputBuffer {
    std::vector<uint8_t> buffer;
    TimePoint  receivedAt{};
    double     bleDeltaMs = -1.0;
    uint64_t   sequence   = 0;
};

struct DualJoyConSharedState {
    std::mutex              mutex;
    std::condition_variable cv;
    TimedInputBuffer        left, right;
    TimePoint               lastLeftBleTime{}, lastRightBleTime{}, lastEmitTime{};
    uint64_t                sequence = 0, eventIndex = 0;
};

struct SingleJoyConPlayer {
    ConnectedJoyCon joycon;
    PVIGEM_TARGET   ds4Controller = nullptr;
    JoyConSide      side;
    JoyConOrientation orientation;
    bool            mouseEnabled   = true;
    DpadMode        dpadMode       = DpadMode::On;
    RemapSideways   remapSideways  = RemapSideways::No;
    int             mouseMode      = 0;
    bool            wasChatPressed = false;
    int16_t         lastOpticalX   = 0, lastOpticalY = 0;
    bool            firstOpticalRead = true;
    float           scrollAccumulator = 0.f;
    bool            mb4Pressed = false, mb5Pressed = false;
    bool            leftBtnPressed = false, rightBtnPressed = false, middleBtnPressed = false;
    LatencyTracker  latency;
};

struct DualJoyConPlayer {
    ConnectedJoyCon leftJoyCon, rightJoyCon;
    GyroSource      gyroSource;
    GyroMode        gyroMode;
    DpadMode        leftDpadMode = DpadMode::On;
    uint8_t         dsuSlot = 0;
    PVIGEM_TARGET   ds4Controller = nullptr;
    std::atomic<bool> running{false};
    std::thread     updateThread;
    std::shared_ptr<DualJoyConSharedState> sharedState;
};

struct ProControllerPlayer {
    ConnectedJoyCon controller;
    PVIGEM_TARGET   ds4Controller = nullptr;
    LatencyTracker  latency;
};

struct ConnectionTask {
    std::string        label;
    bool               done    = false;
    bool               success = false;
    std::string        statusMsg;
    ConnectedJoyCon    result;
    std::thread        worker;
};


enum class RumbleMode { JoyCon, Pro, GC };

struct SingleRumbleCtx {
    GattCharacteristic  vibrationChar;
    PVIGEM_TARGET       target    = nullptr;
    GattCharacteristic  secondaryVibrationChar = nullptr;
    RumbleMode          mode      = RumbleMode::JoyCon;
    std::atomic<bool>   running{true};
    std::thread         thread;
};
struct DualRumbleCtx {
    GattCharacteristic  leftVibration;
    GattCharacteristic  rightVibration;
    PVIGEM_TARGET       target    = nullptr;
    std::atomic<bool>   running{true};
    std::thread         thread;
};

static AppScreen              g_screen        = AppScreen::Setup;
static RuntimeOptions         g_opts;
static ProControllerConfig    g_proConfig;
static PVIGEM_CLIENT          g_vigem         = nullptr;
static DsuServer              g_dsuServer;

static std::vector<PlayerConfig>                    g_playerConfigs;
static std::vector<std::unique_ptr<SingleJoyConPlayer>> g_singlePlayers;
static std::vector<std::unique_ptr<DualJoyConPlayer>> g_dualPlayers;
static std::vector<ProControllerPlayer>             g_proPlayers;

static std::vector<SingleRumbleCtx*> g_singleRumbleCtxs;
static std::vector<DualRumbleCtx*>   g_dualRumbleCtxs;
static std::vector<SingleRumbleCtx*> g_proRumbleCtxs;

static std::vector<ConnectionTask>  g_connectionTasks;
static std::mutex                   g_connMutex;
static std::vector<std::shared_ptr<RxStats>> g_rxStats;
static std::mutex                   g_rxStatsMutex;
static int                          g_connectionTaskIndex = 0;
static bool                         g_connectionDone      = false;
static std::string                  g_connectionError;

static bool g_showLayoutManager = false;
static bool g_screenshotButtonPressed = false;
static bool g_cButtonPressed          = false;
static bool g_comboPressed            = false;
static std::atomic<bool> g_openLayoutManager{false};
static std::atomic<bool> g_shuttingDown{false};

static std::mutex         g_logMutex;
static std::vector<std::string> g_logLines;
static std::atomic<uint64_t> g_logVersion{0};
static void AppLog(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_logLines.push_back(s);
    if (g_logLines.size() > 200) g_logLines.erase(g_logLines.begin());
    ++g_logVersion;
}



static std::mutex    g_diagMutex;
static std::ofstream g_diagFile;
static bool          g_diagOpened = false;
static void DiagLog(const std::string& s) {
    {
        std::lock_guard<std::mutex> lk(g_diagMutex);
        if (!g_diagOpened) {
            g_diagOpened = true;
            g_diagFile.open("joycon2cpp_debug.log", std::ios::out | std::ios::trunc);
        }
        if (g_diagFile.is_open()) {
            std::time_t t = std::time(nullptr);
            std::tm tmv{};
            localtime_s(&tmv, &t);
            char ts[16] = {};
            std::strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
            g_diagFile << ts << "  " << s << "\n";
            g_diagFile.flush();
        }
    }
    AppLog(s);
}


static std::atomic<bool> g_capActive{false};
static std::mutex        g_capMutex;
static std::ofstream     g_capFile;
static std::chrono::steady_clock::time_point g_capStart;
static std::atomic<int>  g_capSecondsLeft{0};

static void CapWrite(const std::string& label, const std::vector<uint8_t>& buf) {
    std::lock_guard<std::mutex> lk(g_capMutex);
    if (!g_capFile.is_open()) return;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_capStart).count();
    char head[64];
    sprintf_s(head, "%7lld ms  len=%3u  ", ms, (unsigned)buf.size());
    g_capFile << head << label << " :";
    for (uint8_t b : buf) { char t[8]; sprintf_s(t, " %02X", b); g_capFile << t; }
    g_capFile << "\n";
}

static void CapStart(int seconds) {
    if (g_capActive.load()) return;
    {
        std::lock_guard<std::mutex> lk(g_capMutex);
        if (g_capFile.is_open()) g_capFile.close();
        g_capFile.open("joycon2cpp_motion_capture.txt", std::ios::out | std::ios::trunc);
        g_capStart = std::chrono::steady_clock::now();
    }
    g_capSecondsLeft.store(seconds);
    g_capActive.store(true);
    DiagLog("Motion capture started (" + std::to_string(seconds) + " s) -> joycon2cpp_motion_capture.txt");
    std::thread([seconds]() {
        for (int i = seconds; i > 0 && !g_shuttingDown.load(); --i) {
            g_capSecondsLeft.store(i);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        g_capActive.store(false);
        g_capSecondsLeft.store(0);
        { std::lock_guard<std::mutex> lk(g_capMutex); if (g_capFile.is_open()) g_capFile.close(); }
        DiagLog("Motion capture finished: joycon2cpp_motion_capture.txt");
    }).detach();
}

class LatencyCsvLogger {
public:
    bool Start(const std::string& path) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_file.open(path, std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) return false;
        m_file << "mode,controller_type,event_index,ble_delta_ms,buffer_age_left_ms,buffer_age_right_ms,decode_to_vigem_us,total_pipeline_us\n";
        m_enabled = true;
        return true;
    }
    bool Enabled() const { return m_enabled.load(std::memory_order_acquire); }
    void Record(UpdatePolicy pol, const char* ct, uint64_t idx,
                double bleDelta, double ageL, double ageR, double decUs, double totUs) {
        if (!Enabled()) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_file.is_open()) return;
        m_file << PolicyName(pol) << ',' << ct << ',' << idx << ','
               << std::fixed << std::setprecision(3)
               << bleDelta << ',' << ageL << ',' << ageR << ','
               << std::setprecision(1) << decUs << ',' << totUs << '\n';
    }
    static const char* PolicyName(UpdatePolicy p) {
        switch(p) {
            case UpdatePolicy::LowLatency:    return "LowLatency";
            case UpdatePolicy::Balanced120Hz: return "Balanced120Hz";
            case UpdatePolicy::Legacy60Hz:    return "Legacy60Hz";
            default: return "Unknown";
        }
    }
private:
    std::atomic<bool> m_enabled{false};
    std::mutex        m_mutex;
    std::ofstream     m_file;
};
static LatencyCsvLogger g_latencyLogger;

static double MsBetween(TimePoint a, TimePoint b) {
    if (a == TimePoint{}) return -1.0;
    return std::chrono::duration<double, std::milli>(b - a).count();
}
static double UsBetween(TimePoint a, TimePoint b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}
static std::chrono::microseconds PolicyInterval(UpdatePolicy p) {
    switch(p) {
        case UpdatePolicy::Balanced120Hz: return std::chrono::microseconds(8333);
        case UpdatePolicy::Legacy60Hz:    return std::chrono::microseconds(16667);
        default:                          return std::chrono::microseconds(0);
    }
}
static bool ShouldEmit(UpdatePolicy p, TimePoint& last, TimePoint now) {
    auto iv = PolicyInterval(p);
    if (iv.count() == 0 || last == TimePoint{} || now - last >= iv) { last = now; return true; }
    return false;
}
static const char* CtrlTypeName(int t) {
    switch(t) {
        case 1: return "SingleJoyCon";
        case 2: return "DualJoyCon";
        case 3: return "ProController";
        case 4: return "NSOGCController";
        default: return "Unknown";
    }
}

static const char* BtnMapNames[] = {
    "None","L3","R3","L1","R1","L2","R2",
    "Cross","Circle","Square","Triangle",
    "Share","Options",
    "DPad Up","DPad Down","DPad Left","DPad Right"
};
static const char* BtnMapStr(ButtonMapping m) { return BtnMapNames[(int)m]; }
static ButtonMapping BtnMapFromStr(const std::string& s) {
    for (int i = 0; i < 17; i++)
        if (s == BtnMapNames[i]) return (ButtonMapping)i;
    return ButtonMapping::NONE;
}

static void SaveProConfig() {
    std::ofstream f(CONFIG_FILE);
    if (!f.is_open()) return;
    f << "{\n  \"activeLayoutIndex\": " << g_proConfig.activeLayoutIndex << ",\n  \"layouts\": [\n";
    for (size_t i = 0; i < g_proConfig.layouts.size(); ++i) {
        auto& l = g_proConfig.layouts[i];
        f << "    {\"name\":\"" << l.name << "\","
          << "\"glMapping\":\"" << BtnMapStr(l.glMapping) << "\","
          << "\"grMapping\":\"" << BtnMapStr(l.grMapping) << "\"}";
        if (i+1 < g_proConfig.layouts.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
}
static void LoadProConfig() {
    std::ifstream f(CONFIG_FILE);
    if (!f.is_open()) {
        GLGRLayout def; def.glMapping = ButtonMapping::NONE; def.grMapping = ButtonMapping::NONE;
        g_proConfig.layouts.push_back(def);
        g_proConfig.activeLayoutIndex = 0;
        SaveProConfig();
        return;
    }
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    g_proConfig.layouts.clear(); g_proConfig.activeLayoutIndex = 0;
    auto readField = [&](const std::string& key) -> std::string {
        auto p = json.find("\"" + key + "\"");
        if (p == std::string::npos) return "";
        auto c = json.find(':', p); auto e = json.find_first_of(",}\n", c);
        auto v = json.substr(c+1, e-c-1);
        v.erase(0, v.find_first_not_of(" \t\n\r\""));
        v.erase(v.find_last_not_of(" \t\n\r\"")+1);
        return v;
    };
    try { g_proConfig.activeLayoutIndex = std::stoi(readField("activeLayoutIndex")); } catch(...) {}
    size_t pos = json.find('[');
    while (pos != std::string::npos) {
        auto ob = json.find('{', pos);
        if (ob == std::string::npos) break;
        auto oe = json.find('}', ob);
        if (oe == std::string::npos) break;
        auto obj = json.substr(ob, oe-ob+1);
        GLGRLayout lay;
        auto getStr = [&](const std::string& key) -> std::string {
            auto kp = obj.find("\""+key+"\"");
            if (kp == std::string::npos) return "";
            auto q1 = obj.find('"', kp+key.size()+2);
            auto q2 = obj.find('"', q1+1);
            return obj.substr(q1+1, q2-q1-1);
        };
        auto nm = getStr("name");
        if (!nm.empty()) strncpy_s(lay.name, nm.c_str(), 63);
        lay.glMapping = BtnMapFromStr(getStr("glMapping"));
        lay.grMapping = BtnMapFromStr(getStr("grMapping"));
        g_proConfig.layouts.push_back(lay);
        pos = oe+1;
    }
    if (g_proConfig.layouts.empty()) {
        GLGRLayout def; g_proConfig.layouts.push_back(def); g_proConfig.activeLayoutIndex = 0;
    }
    if (g_proConfig.activeLayoutIndex < 0 || g_proConfig.activeLayoutIndex >= (int)g_proConfig.layouts.size())
        g_proConfig.activeLayoutIndex = 0;
}


static std::string HexBytes(const std::vector<uint8_t>& bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 3);
    for (auto b : bytes) {
        char buf[4];
        sprintf_s(buf, "%02X ", b);
        hex += buf;
    }
    return hex;
}

static std::string GuidStr(guid const& g) {
    std::string out = winrt::to_string(winrt::to_hstring(g));
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static std::string PropsStr(uint32_t p) {
    std::string s;
    if (p & 0x02) s += "Read ";
    if (p & 0x04) s += "WriteNoResp ";
    if (p & 0x08) s += "Write ";
    if (p & 0x10) s += "Notify ";
    if (p & 0x20) s += "Indicate ";
    if (s.empty()) s = "-";
    return s;
}

static const char* GattStatusName(GattCommunicationStatus st) {
    switch (st) {
        case GattCommunicationStatus::Success:       return "Success";
        case GattCommunicationStatus::Unreachable:   return "Unreachable";
        case GattCommunicationStatus::ProtocolError: return "ProtocolError";
        case GattCommunicationStatus::AccessDenied:  return "AccessDenied";
        default:                                     return "Unknown";
    }
}

static void AttachPacketTap(GattCharacteristic const& inputChar, std::shared_ptr<RxStats> rx);



static std::shared_ptr<RxStats> AttachCommandResponseLogger(
    GattCharacteristic const& ch,
    const std::string& name,
    GattClientCharacteristicConfigurationDescriptorValue cccValue =
        GattClientCharacteristicConfigurationDescriptorValue::Notify)
{
    if (!ch) {
        return nullptr;
    }

    auto rx = std::make_shared<RxStats>();
    rx->label = name;
    rx->captureFlag = (name.find("cmd-resp(basic)") != std::string::npos);
    { std::lock_guard<std::mutex> lk(g_rxStatsMutex); g_rxStats.push_back(rx); }
    AttachPacketTap(ch, rx);

    try {
        auto status = ch.WriteClientCharacteristicConfigurationDescriptorAsync(cccValue).get();
        if (status != GattCommunicationStatus::Success)
            DiagLog("[WARN] could not subscribe to " + name + ": " + GattStatusName(status));
    } catch (...) { }
    return rx;
}

static void SendGenericCommand(GattCharacteristic const& ch, uint8_t cmdId, uint8_t subId, const std::vector<uint8_t>& data) {
    if (g_shuttingDown.load()) return;
    if (!ch) {
        return;
    }

    std::vector<uint8_t> pkt;
    pkt.reserve(8 + data.size());
    pkt.push_back(cmdId);
    pkt.push_back(0x91);
    pkt.push_back(0x01); 
    pkt.push_back(subId);
    pkt.push_back(0x00);
    pkt.push_back((uint8_t)data.size());
    pkt.push_back(0x00);
    pkt.push_back(0x00);
    pkt.insert(pkt.end(), data.begin(), data.end());

    DataWriter w;
    w.WriteBytes(pkt);
    try {
        auto status = ch.WriteValueAsync(
            w.DetachBuffer(),
            GattWriteOption::WriteWithoutResponse
        ).get();
    } catch (...) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
static void SetPlayerLEDs(GattCharacteristic const& ch, uint8_t pat) {
    std::vector<uint8_t> d(8, 0); d[0] = pat;
    SendGenericCommand(ch, 0x09, 0x07, d);
}

static void SendVibrationSample(GattCharacteristic const& ch, uint8_t sampleId);

static void EmitSound(GattCharacteristic const& ch) {
    SendVibrationSample(ch, 0x04);
}
static void SendCustomCommands(GattCharacteristic const& ch) {
    if (!ch || g_shuttingDown.load()) return;
    std::vector<std::vector<uint8_t>> cmds = {
        {0x0c,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},

        {0x0c,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},
    };

    for (auto& cmd : cmds) {
        DataWriter w;
        w.WriteBytes(cmd);

        try {
            auto status = ch.WriteValueAsync(
                w.DetachBuffer(),
                GattWriteOption::WriteWithoutResponse
            ).get();
        } catch (...) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


static const char* KindNameForLog(DeviceKind k)
{
    switch (k) {
        case DeviceKind::JoyConL: return "Left Joy-Con";
        case DeviceKind::JoyConR: return "Right Joy-Con";
        case DeviceKind::Pro:     return "Pro Controller";
        case DeviceKind::GC:      return "NSO GC Controller";
        default:                  return "Controller";
    }
}

using Cmd = std::vector<uint8_t>;


static std::vector<Cmd> JoyCon2InitCmds()
{
    return {
        {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
        {0x10,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
        {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
        {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
        {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
         0x01,0x59,0x09,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,
         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x11,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x37,0x00,0x00,0x00},
    };
}

static std::vector<Cmd> ProCon2InitCmds()
{
    return {
        {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
        {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
        {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x2F,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0xC0,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
        {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
        {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
         0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,
         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x2F,0x00,0x00,0x00},
    };
}

static std::vector<Cmd> NSOGCInitCmds()
{
    return {
        {0x07,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x00,0x30,0x01,0x00},
        {0x10,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x16,0x91,0x01,0x01,0x00,0x00,0x00,0x00},
        {0x0A,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x03,0x00,0x00,0x00},
        {0x09,0x91,0x01,0x07,0x00,0x08,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x02,0x00,0x04,0x00,0x00,0x27,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x80,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0xC0,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x40,0x7E,0x00,0x00,0x40,0xC0,0x1F,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x10,0x7E,0x00,0x00,0x40,0x30,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x18,0x7E,0x00,0x00,0x00,0x31,0x01,0x00},
        {0x11,0x91,0x01,0x03,0x00,0x00,0x00,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x02,0x7E,0x00,0x00,0x40,0x31,0x01,0x00},
        {0x02,0x91,0x01,0x04,0x00,0x08,0x00,0x00,0x20,0x7E,0x00,0x00,0x60,0x30,0x01,0x00},
        {0x0A,0x91,0x01,0x08,0x00,0x14,0x00,0x00,
         0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x35,0x00,0x46,
         0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x0C,0x91,0x01,0x04,0x00,0x04,0x00,0x00,0x27,0x00,0x00,0x00},
    };
}


static void ApplyFeatureMask(std::vector<Cmd>& cmds, uint8_t mask)
{
    for (auto& c : cmds)
        if (c.size() >= 9 && c[0] == 0x0C && c[1] == 0x91) c[8] = mask;
}



static bool GattWrite(GattCharacteristic const& ch, const std::vector<uint8_t>& pkt,
                      GattCommunicationStatus* outStatus = nullptr)
{
    if (!ch) return false;
    const uint32_t props = static_cast<uint32_t>(ch.CharacteristicProperties());
    const GattWriteOption opt = (props & 0x04) ? GattWriteOption::WriteWithoutResponse
                                               : GattWriteOption::WriteWithResponse;
    try {
        DataWriter w;
        w.WriteBytes(pkt);
        GattCommunicationStatus st = ch.WriteValueAsync(w.DetachBuffer(), opt).get();
        if (outStatus) *outStatus = st;
        return st == GattCommunicationStatus::Success;
    } catch (...) {
        if (outStatus) *outStatus = GattCommunicationStatus::Unreachable;
        return false;
    }
}




static int RunInitCommands(GattCharacteristic const& ch, const std::vector<Cmd>& cmds,
                           size_t prefixSize, const std::string& tag)
{
    int okCount = 0, failCount = 0;
    for (const auto& cmd : cmds) {
        if (g_shuttingDown.load()) break;
        std::vector<uint8_t> pkt(prefixSize, 0x00);
        pkt.insert(pkt.end(), cmd.begin(), cmd.end());
        GattCommunicationStatus st = GattCommunicationStatus::Unreachable;
        if (GattWrite(ch, pkt, &st)) {
            ++okCount;
        } else {
            ++failCount;
            if (failCount <= 2) DiagLog(tag + ": write failed (" + GattStatusName(st) + ")");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    DiagLog(tag + ": " + std::to_string(okCount) + " commands written, " + std::to_string(failCount) + " failed");
    return okCount;
}

static bool WaitForFullStream(const std::shared_ptr<RxStats>& rx, uint64_t fullBefore, int waitMs)
{
    const auto end = SteadyClock::now() + std::chrono::milliseconds(waitMs);
    while (SteadyClock::now() < end && !g_shuttingDown.load()) {
        if (rx->fullCount.load() >= fullBefore + 3) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return rx->fullCount.load() >= fullBefore + 3;
}




static bool TryConsoleSession(ConnectedJoyCon& cj, DeviceKind kind,
                              const std::function<void(const std::string&)>& statusCb)
{
    if (!cj.nativeInputChar || !cj.nativeRx) return false;
    const std::string tag = std::string("[CONSOLE ") + KindNameForLog(kind) + "]";
    statusCb("Switching to console protocol (clone pad)...");
    DiagLog(tag + " standard input is silent - starting console-protocol session on the native input characteristic");


    if (cj.sessionChar) {
        std::vector<uint8_t> en = { 0x01, 0x00 };
        GattCommunicationStatus st = GattCommunicationStatus::Unreachable;
        const bool ok = GattWrite(cj.sessionChar, en, &st);
        DiagLog(tag + " session enable (01 00): " + (ok ? "ok" : GattStatusName(st)));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
        DiagLog(tag + " [WARN] session characteristic (...bd282) not found, skipping");
    }


    {
        std::vector<Cmd> cmds = JoyCon2InitCmds();
        if (cj.rumbleChar)      RunInitCommands(cj.rumbleChar, cmds, 17, tag + " commands");
        else if (cj.writeChar)  RunInitCommands(cj.writeChar,  cmds, 0,  tag + " commands");
    }


    try {
        auto dr = cj.nativeInputChar.GetDescriptorsForUuidAsync(guid(NATIVE_ENABLE_DESC_UUID), BluetoothCacheMode::Uncached).get();
        if (dr.Status() == GattCommunicationStatus::Success && dr.Descriptors().Size() > 0) {
            std::vector<uint8_t> val = { 0x85, 0x00 };
            DataWriter w;
            w.WriteBytes(val);
            GattCommunicationStatus st = dr.Descriptors().GetAt(0).WriteValueAsync(w.DetachBuffer()).get();
            DiagLog(tag + " native enable descriptor (85 00): " + GattStatusName(st));
        } else {
            DiagLog(tag + " [WARN] native enable descriptor not found (status " + GattStatusName(dr.Status()) + ")");
        }
    } catch (...) {
        DiagLog(tag + " [WARN] writing the native enable descriptor threw an exception");
    }


    GattCommunicationStatus cst = GattCommunicationStatus::Unreachable;
    for (int i = 0; i < 3; ++i) {
        try {
            cst = cj.nativeInputChar.WriteClientCharacteristicConfigurationDescriptorAsync(
                      GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
        } catch (...) {
            cst = GattCommunicationStatus::Unreachable;
        }
        if (cst == GattCommunicationStatus::Success) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    DiagLog(tag + " native input notifications: " + GattStatusName(cst));


    const uint64_t before = cj.nativeRx->count.load();
    const auto end = SteadyClock::now() + std::chrono::milliseconds(2500);
    while (SteadyClock::now() < end && !g_shuttingDown.load()) {
        if (cj.nativeRx->count.load() >= before + 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (cj.nativeRx->count.load() >= before + 3) {
        cj.inputChar    = cj.nativeInputChar;
        cj.rx           = cj.nativeRx;
        cj.consoleMode  = true;
        cj.initStrategy = "console protocol (native input)";
        DiagLog(tag + " OK - native input is streaming (" + std::to_string(cj.nativeRx->count.load()) + " packets)");
        cj.fg = std::make_shared<FauxGyroState>();
        return true;
    }
    DiagLog(tag + " no packets on the native input characteristic");
    return false;
}

struct InitAttempt {
    const char* name;
    int         channel;
    size_t      prefix;
    uint8_t     mask;
};








static bool ActivateInputStream(ConnectedJoyCon& cj, DeviceKind kind,
                                const std::function<void(const std::string&)>& statusCb)
{
    if (!cj.rx) return false;
    const bool joyLike = (kind == DeviceKind::JoyConL || kind == DeviceKind::JoyConR || kind == DeviceKind::Any);
    const size_t combinedPrefix = (kind == DeviceKind::Pro) ? 33 : (kind == DeviceKind::GC) ? 13 : 17;

    std::vector<InitAttempt> attempts;
    if (cj.rumbleChar) attempts.push_back({ "original (0x0016 combined)", 1, combinedPrefix, 0 });
    if (cj.writeChar)  attempts.push_back({ "direct (0x0014 command)",    0, 0, 0 });
    if (cj.writeChar) {
        if (joyLike) {
            attempts.push_back({ "direct, features 0xFF", 0, 0, 0xFF });
            attempts.push_back({ "direct, features 0x2F", 0, 0, 0x2F });
        } else {
            attempts.push_back({ "direct, features 0x2F", 0, 0, 0x2F });
            attempts.push_back({ "direct, features 0xFF", 0, 0, 0xFF });
        }
    }
    if (attempts.empty()) {
        DiagLog("[ERROR] no writable command characteristic found - cannot initialise controller");
        return false;
    }

    bool consoleTried = false;
    for (const auto& a : attempts) {
        if (g_shuttingDown.load()) return false;
        std::vector<Cmd> cmds = (kind == DeviceKind::Pro) ? ProCon2InitCmds()
                              : (kind == DeviceKind::GC)  ? NSOGCInitCmds()
                                                          : JoyCon2InitCmds();
        if (a.mask) ApplyFeatureMask(cmds, a.mask);

        const std::string tag = std::string("[INIT ") + KindNameForLog(kind) + " / " + a.name + "]";
        statusCb(std::string("Initialising (") + a.name + ")...");
        DiagLog(tag + " sending " + std::to_string(cmds.size()) + " commands");

        const uint64_t fullBefore = cj.rx->fullCount.load();
        RunInitCommands(a.channel == 1 ? cj.rumbleChar : cj.writeChar, cmds, a.prefix, tag);

        if (WaitForFullStream(cj.rx, fullBefore, 1500)) {
            cj.initStrategy = a.name;
            DiagLog(tag + " OK - controller is streaming input reports");
            return true;
        }
        DiagLog(tag + " no full-size input stream yet (packets so far: " +
                std::to_string(cj.rx->count.load()) + ", last length: " +
                std::to_string(cj.rx->lastLen.load()) + ")");


        if (!consoleTried && joyLike && cj.nativeInputChar) {
            consoleTried = true;
            if (TryConsoleSession(cj, kind, statusCb)) return true;
        }
    }

    if (cj.rx->count.load() > 0) {
        DiagLog("[WARN] " + std::string(KindNameForLog(kind)) + " sends packets but none are 60+ bytes (last length " +
                std::to_string(cj.rx->lastLen.load()) + "). Buttons may still work; motion data will not.");
        return true;
    }
    DiagLog("[WARN] " + std::string(KindNameForLog(kind)) + " connected but sent NO input packets after every init strategy. "
            "See joycon2cpp_debug.log (GATT dump + command responses).");
    return false;
}

static void SendRumble(GattCharacteristic const& ch, float amp)
{
    if (!ch || g_shuttingDown.load()) return;

    static uint8_t c = 0;

    std::vector<uint8_t> pkt(42, 0x00);
    pkt[0] = 0x00;

    if (amp >= 0.01f) {
        pkt[1] = 0x50 | (c & 0x0F);

        const uint8_t strong[5] = {0x93, 0x35, 0x36, 0x1C, 0x0D};
        const uint8_t weak[5]   = {0x4B, 0x7D, 0x80, 0x5A, 0x02};

        const uint8_t* p = amp > 0.5f ? strong : weak;
        memcpy(pkt.data() + 2, p, 5);

        c = (c + 1) & 0x0F;
    }

    DataWriter w;
    w.WriteBytes(pkt);
    ch.WriteValueAsync(w.DetachBuffer(), GattWriteOption::WriteWithoutResponse);
}


static void SendGCRumble(GattCharacteristic const& ch, float amp)
{
    if (!ch || g_shuttingDown.load()) return;
    std::vector<uint8_t> pkt(5, 0x00);
    if (amp >= 0.01f) {
        pkt[1] = 0x01;
        pkt[2] = static_cast<uint8_t>(std::min(amp * 255.f, 255.f));
    }
    DataWriter w;
    w.WriteBytes(pkt);
    ch.WriteValueAsync(w.DetachBuffer(), GattWriteOption::WriteWithoutResponse);
}

static void SendProRumble(GattCharacteristic const& ch, float amp)
{
    if (!ch || g_shuttingDown.load()) return;
    static uint8_t c = 0;
    std::vector<uint8_t> pkt(58, 0x00);
    if (amp >= 0.01f) {
        const uint8_t strong[5] = {0x93, 0x35, 0x36, 0x1C, 0x0D};
        const uint8_t weak[5]   = {0x4B, 0x7D, 0x80, 0x5A, 0x02};
        const uint8_t* p = amp > 0.5f ? strong : weak;
        pkt[1] = 0x50 | (c & 0x0F);
        memcpy(pkt.data() + 2, p, 5); 
        pkt[17] = 0x50 | (c & 0x0F);
        memcpy(pkt.data() + 18, p, 5); 
        c = (c + 1) & 0x0F;
    }
    DataWriter w;
    w.WriteBytes(pkt);
    ch.WriteValueAsync(w.DetachBuffer(), GattWriteOption::WriteWithoutResponse);
}

static void SendVibrationSample(GattCharacteristic const& ch, uint8_t sampleId)
{
    std::vector<uint8_t> data(4, 0);
    data[0] = sampleId;
    SendGenericCommand(ch, 0x0A, 0x02, data);
}

static void StartSingleRumbleThread(SingleRumbleCtx* ctx) {
    ctx->thread = std::thread([ctx]() {
        DS4_OUTPUT_DATA last{};
        int silentFrames = 0;
        while (ctx->running.load() && !g_shuttingDown.load()) {
            DS4_OUTPUT_DATA out{};
            auto err = vigem_target_ds4_get_output(g_vigem, ctx->target, &out);
            if (VIGEM_SUCCESS(err)) {
                float amp = std::max(out.LargeMotor, out.SmallMotor) / 255.0f;
                if (amp >= 0.01f) {
                    if (ctx->mode == RumbleMode::GC) {
                        SendGCRumble(ctx->vibrationChar, amp);
                    } else if (ctx->mode == RumbleMode::Pro) {
                        SendProRumble(ctx->vibrationChar, amp);
                    } else {
                        SendRumble(ctx->vibrationChar, amp);
                        if (ctx->secondaryVibrationChar)
                            SendRumble(ctx->secondaryVibrationChar, amp);
                    }
                    silentFrames = 0;
                } else {
                    if (silentFrames == 0) {
                        if (ctx->mode == RumbleMode::GC) {
                            SendGCRumble(ctx->vibrationChar, 0.0f);
                        } else if (ctx->mode == RumbleMode::Pro) {
                            SendProRumble(ctx->vibrationChar, 0.0f);
                        } else {
                            SendRumble(ctx->vibrationChar, 0.0f);
                            if (ctx->secondaryVibrationChar)
                                SendRumble(ctx->secondaryVibrationChar, 0.0f);
                        }
                    }
                    silentFrames++;
                }
                last = out;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });
}

static void StartDualRumbleThread(DualRumbleCtx* ctx) {
    ctx->thread = std::thread([ctx]() {
        DS4_OUTPUT_DATA last{};
        int silentFrames = 0;
        while (ctx->running.load() && !g_shuttingDown.load()) {
            DS4_OUTPUT_DATA out{};
            auto err = vigem_target_ds4_get_output(g_vigem, ctx->target, &out);
            if (VIGEM_SUCCESS(err)) {
                float amp = std::max(out.LargeMotor, out.SmallMotor) / 255.0f;
                if (amp >= 0.01f) {
                    SendRumble(ctx->leftVibration, amp);
                    SendRumble(ctx->rightVibration, amp);
                    silentFrames = 0;
                } else {
                    if (silentFrames == 0) {
                        SendRumble(ctx->leftVibration, 0.0f);
                        SendRumble(ctx->rightVibration, 0.0f);
                    }
                    silentFrames++;
                }
                last = out;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });
}





static bool TryParseSwitch2ControllerAd(const std::vector<uint8_t>& d, uint16_t& productId)
{
    productId = 0;
    if (d.size() < 4) return false;
    if (d[0] != 0x01 || d[1] != 0x00 || d[2] != 0x03 || d[3] != 0x7E) return false;
    if (d.size() >= 7 && d[4] == 0x05)
        productId = static_cast<uint16_t>(d[5] | (static_cast<uint16_t>(d[6]) << 8));
    return true;
}

static bool NameLooksLikeSwitchController(const std::string& nameIn)
{
    std::string n = nameIn;
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n.find("joy-con") != std::string::npos || n.find("joycon") != std::string::npos ||
           n.find("pro controller") != std::string::npos || n.find("mobapad") != std::string::npos ||
           n.find("nintendo") != std::string::npos;
}



static bool PidMatchesKind(uint16_t pid, DeviceKind kind)
{
    const bool known = (pid == PID_JOYCON_R || pid == PID_JOYCON_L || pid == PID_PRO || pid == PID_GC);
    if (!known || kind == DeviceKind::Any) return true;
    switch (kind) {
        case DeviceKind::JoyConL: return pid == PID_JOYCON_L;
        case DeviceKind::JoyConR: return pid == PID_JOYCON_R;
        case DeviceKind::Pro:     return pid == PID_PRO;
        case DeviceKind::GC:      return pid == PID_GC;
        default:                  return true;
    }
}



static std::mutex         g_connectedAddrMutex;
static std::set<uint64_t> g_connectedAddrs;
static void MarkConnectedAddr(uint64_t a)  { std::lock_guard<std::mutex> lk(g_connectedAddrMutex); g_connectedAddrs.insert(a); }
static bool IsConnectedAddr(uint64_t a)    { std::lock_guard<std::mutex> lk(g_connectedAddrMutex); return g_connectedAddrs.count(a) != 0; }
static void ClearConnectedAddrs()          { std::lock_guard<std::mutex> lk(g_connectedAddrMutex); g_connectedAddrs.clear(); }

struct BleCandidate {
    uint64_t    address   = 0;
    uint16_t    productId = 0;
    int16_t     rssi      = 0;
    std::string name;
};


struct WatcherGuard {
    BluetoothLEAdvertisementWatcher& watcher;
    event_token                      token;
    bool                             active;
    WatcherGuard(BluetoothLEAdvertisementWatcher& w, event_token t) : watcher(w), token(t), active(true) {}
    ~WatcherGuard() { Release(); }
    void Release() {
        if (!active) return;
        active = false;
        try { watcher.Stop(); } catch (...) {}
        try { watcher.Received(token); } catch (...) {}
    }
};



static void AttachPacketTap(GattCharacteristic const& inputChar, std::shared_ptr<RxStats> rx)
{
    inputChar.ValueChanged([rx](GattCharacteristic const&, GattValueChangedEventArgs const& args) {
        if (g_shuttingDown.load()) return;
        auto rdr = DataReader::FromBuffer(args.CharacteristicValue());
        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength());
        rdr.ReadBytes(buf);

        if (g_capActive.load() && rx->captureFlag) {

            if (rx->label.find("cmd-resp") == std::string::npos || (!buf.empty() && buf[0] == 0xEA))
                CapWrite(rx->label, buf);
        }

        {
            uint64_t m = 0;
            const size_t lim = std::min<size_t>(buf.size(), 64);
            for (size_t i = 0; i < lim; ++i) if (buf[i]) m |= (1ULL << i);
            rx->nonZeroMask.fetch_or(m);
        }

        const uint64_t n = ++rx->count;
        rx->lastLen.store(static_cast<uint32_t>(buf.size()));
        if (buf.size() >= FULL_REPORT_LEN) ++rx->fullCount;



        bool logIt = false;
        {
            std::lock_guard<std::mutex> lk(rx->mtx);
            rx->lastPacket = buf;
            const auto nowT = std::chrono::steady_clock::now();
            bool differs = (buf.size() != rx->lastLoggedPkt.size());
            if (!differs) {
                for (size_t i = 4; i < buf.size(); ++i)
                    if (buf[i] != rx->lastLoggedPkt[i]) { differs = true; break; }
            }
            if (n <= 3) logIt = true;
            else if (differs && rx->logLines < 300 &&
                     nowT - rx->lastLogTime >= std::chrono::milliseconds(100)) logIt = true;
            if (logIt) {
                rx->lastLoggedPkt = buf;
                rx->lastLogTime = nowT;
                ++rx->logLines;
            }
        }
        if (logIt) {
            const size_t k = std::min<size_t>(buf.size(), 40);
            std::vector<uint8_t> head(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(k));
            DiagLog("[RX " + rx->label + " #" + std::to_string(n) + "] len=" +
                    std::to_string(buf.size()) + " " + HexBytes(head));
        }
    });
}



static bool TryOpenController(const BleCandidate& cand, JoyConSide side, DeviceKind kind,
                              const std::function<void(const std::string&)>& statusCb,
                              ConnectedJoyCon& cj)
{
    cj = ConnectedJoyCon{};
    BluetoothLEDevice device = nullptr;
    try {
        device = BluetoothLEDevice::FromBluetoothAddressAsync(cand.address).get();
    } catch (...) {
        DiagLog("[WARN] BluetoothLEDevice::FromBluetoothAddressAsync threw an exception");
        return false;
    }
    if (!device) {
        DiagLog("[WARN] BLE device opened as null");
        return false;
    }
    cj.device = device;
    statusCb("Device opened, fetching GATT services...");

    GattDeviceServicesResult sr = nullptr;
    for (int att = 1; att <= 6; ++att) {
        if (g_shuttingDown.load()) break;
        try {
            sr = device.GetGattServicesAsync(BluetoothCacheMode::Uncached).get();
        } catch (...) {
            sr = nullptr;
        }
        if (sr && sr.Status() == GattCommunicationStatus::Success) break;
        statusCb("Retrying GATT (" + std::to_string(att) + "/6)...");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (!sr || sr.Status() != GattCommunicationStatus::Success) {
        DiagLog("[WARN] could not read GATT services (is this pad in pairing mode? remove it from Windows Bluetooth devices if listed)");
        try { device.Close(); } catch (...) {}
        cj = ConnectedJoyCon{};
        return false;
    }

    std::vector<GattCharacteristic> extraNotifyChars;
    for (auto svc : sr.Services()) {
        const std::string svcStr = GuidStr(svc.Uuid());
        const bool isStandardService = svcStr.find("-0000-1000-8000-00805f9b34fb") != std::string::npos;
        DiagLog("[GATT] service " + svcStr);

        GattCharacteristicsResult cr = nullptr;
        try { cr = svc.GetCharacteristicsAsync(BluetoothCacheMode::Uncached).get(); } catch (...) { continue; }
        if (!cr || cr.Status() != GattCommunicationStatus::Success) continue;

        for (auto ch : cr.Characteristics()) {
            const uint32_t props = static_cast<uint32_t>(ch.CharacteristicProperties());
            char hbuf[16];
            sprintf_s(hbuf, "0x%04X", static_cast<unsigned>(ch.AttributeHandle()));
            DiagLog(std::string("[GATT]   char handle ") + hbuf + "  " + GuidStr(ch.Uuid()) + "  [" + PropsStr(props) + "]");

            bool known = false;
            if (ch.Uuid() == guid(INPUT_REPORT_UUID))  { cj.inputChar  = ch; known = true; }
            if (ch.Uuid() == guid(WRITE_COMMAND_UUID)) { cj.writeChar  = ch; known = true; }
            if (ch.Uuid() == guid(SESSION_CHAR_UUID))  { cj.sessionChar = ch; known = true; }
            if (ch.Uuid() == guid(NATIVE_INPUT_UUID_R)) { known = true; if (side == JoyConSide::Right) cj.nativeInputChar = ch; }
            if (ch.Uuid() == guid(NATIVE_INPUT_UUID_L)) { known = true; if (side == JoyConSide::Left)  cj.nativeInputChar = ch; }
            if (ch.Uuid() == guid(CMD_RESPONSE_UUID_BASIC)) { cj.cmdRespBasicChar = ch; known = true; }
            if (ch.Uuid() == guid(CMD_RESPONSE_UUID_L))   { known = true; if (side == JoyConSide::Left)  cj.cmdRespExtChar = ch; }
            if (ch.Uuid() == guid(CMD_RESPONSE_UUID_R))   { known = true; if (side == JoyConSide::Right) cj.cmdRespExtChar = ch; }
            if (ch.Uuid() == guid(CMD_RESPONSE_UUID_PRO)) { cj.cmdRespExtChar = ch; known = true; }
            if (ch.Uuid() == guid(CMD_RESPONSE_UUID_GC))  { cj.cmdRespExtChar = ch; known = true; }
            if (ch.Uuid() == guid(RUMBLE_CHAR_UUID_L))   { known = true; if (side == JoyConSide::Left)  cj.rumbleChar = ch; }
            if (ch.Uuid() == guid(RUMBLE_CHAR_UUID_R))   { known = true; if (side == JoyConSide::Right) cj.rumbleChar = ch; }
            if (ch.Uuid() == guid(RUMBLE_CHAR_UUID_PRO)) { cj.rumbleChar = ch; known = true; }
            if (ch.Uuid() == guid(RUMBLE_CHAR_UUID_GC))  { cj.rumbleChar = ch; known = true; }
            if (ch.Uuid() == guid(VIBRATION_CHAR_UUID_L)) {
                known = true;
                cj.vibrationCharLeft = ch;
                if (side == JoyConSide::Left) cj.vibrationChar = ch;
            }
            if (ch.Uuid() == guid(VIBRATION_CHAR_UUID_R)) {
                known = true;
                cj.vibrationCharRight = ch;
                if (side == JoyConSide::Right) cj.vibrationChar = ch;
            }
            if (ch.Uuid() == guid(VIBRATION_CHAR_UUID_PRO)) { cj.vibrationChar = ch; known = true; }
            if (ch.Uuid() == guid(VIBRATION_CHAR_UUID_GC))  { cj.vibrationChar = ch; known = true; }



            if (!known && !isStandardService && (props & 0x10)) extraNotifyChars.push_back(ch);
        }
    }

    if (!cj.inputChar || (!cj.writeChar && !cj.rumbleChar)) {
        DiagLog("[ERROR] required GATT characteristics not found (input report / command). See the [GATT] lines above.");
        statusCb("Required GATT characteristics not found. Re-pair/forget the device.");
        try { device.Close(); } catch (...) {}
        cj = ConnectedJoyCon{};
        return false;
    }
    DiagLog(std::string("[GATT] summary: input=") + (cj.inputChar ? "yes" : "NO") +
            " command(0x0014)=" + (cj.writeChar ? "yes" : "NO") +
            " rumble+command(0x0016)=" + (cj.rumbleChar ? "yes" : "NO") +
            " hd-rumble(0x0012)=" + (cj.vibrationChar ? "yes" : "NO"));

    cj.address   = cand.address;
    cj.productId = cand.productId;
    cj.rx = std::make_shared<RxStats>();
    cj.rx->label = KindNameForLog(kind);
    { std::lock_guard<std::mutex> lk(g_rxStatsMutex); g_rxStats.push_back(cj.rx); }


    AttachPacketTap(cj.inputChar, cj.rx);
    auto chLabel = [&](const char* what, GattCharacteristic const& c) {
        char hb[16];
        sprintf_s(hb, "0x%04X", static_cast<unsigned>(c.AttributeHandle()));
        return std::string(KindNameForLog(kind)) + " " + what + " h=" + hb;
    };
    if (cj.nativeInputChar) {
        cj.nativeRx = std::make_shared<RxStats>();
        cj.nativeRx->label = chLabel("native-input", cj.nativeInputChar);
        cj.nativeRx->captureFlag = true;
        { std::lock_guard<std::mutex> lk(g_rxStatsMutex); g_rxStats.push_back(cj.nativeRx); }
        AttachPacketTap(cj.nativeInputChar, cj.nativeRx);
    }
    if (cj.cmdRespBasicChar) AttachCommandResponseLogger(cj.cmdRespBasicChar, chLabel("cmd-resp(basic)", cj.cmdRespBasicChar));
    if (cj.cmdRespExtChar)   AttachCommandResponseLogger(cj.cmdRespExtChar,   chLabel("cmd-resp(ext)",   cj.cmdRespExtChar));
    for (auto& ec : extraNotifyChars) {
        auto erx = AttachCommandResponseLogger(ec, chLabel("vendor", ec) + " " + GuidStr(ec.Uuid()).substr(1, 8));
        if (erx) cj.extraInputs.push_back({ ec, erx });
    }

    try {
        cj.device.RequestPreferredConnectionParameters(BluetoothLEPreferredConnectionParameters::ThroughputOptimized());
    } catch (...) {}

    {
        const uint32_t ip = static_cast<uint32_t>(cj.inputChar.CharacteristicProperties());
        const GattClientCharacteristicConfigurationDescriptorValue cccVal =
            (ip & 0x10) ? GattClientCharacteristicConfigurationDescriptorValue::Notify :
            (ip & 0x20) ? GattClientCharacteristicConfigurationDescriptorValue::Indicate :
                          GattClientCharacteristicConfigurationDescriptorValue::Notify;
        GattCommunicationStatus cccSt = GattCommunicationStatus::Unreachable;
        for (int i = 0; i < 3; ++i) {
            try {
                cccSt = cj.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(cccVal).get();
            } catch (...) {
                cccSt = GattCommunicationStatus::Unreachable;
            }
            if (cccSt == GattCommunicationStatus::Success) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }
        DiagLog(std::string("[GATT] input report notifications: ") + GattStatusName(cccSt));
        if (cccSt != GattCommunicationStatus::Success)
            DiagLog("[WARN] could not enable input notifications - the pad may require Windows-level Bluetooth pairing first");
    }

    statusCb("Connected, starting input stream...");
    ActivateInputStream(cj, kind, statusCb);



    if (cj.rx && cj.rx->count.load() == 0) {
        for (auto& e : cj.extraInputs) {
            if (e.second && e.second->fullCount.load() >= 3) {
                DiagLog("[WARN] no packets on the standard input characteristic, but '" + e.second->label +
                        "' streams full-size reports - using it for input");
                cj.inputChar = e.first;
                cj.rx = e.second;
                break;
            }
        }
    }
    return true;
}




static ConnectedJoyCon BleConnect(std::function<void(const std::string&)> statusCb, bool& outSuccess,
                                  JoyConSide side = JoyConSide::Left, DeviceKind kind = DeviceKind::Any) {
    outSuccess = false;
    ConnectedJoyCon cj{};
    const std::string kindName = KindNameForLog(kind);



    struct ScanState {
        std::mutex mtx;
        std::condition_variable cv;
        std::vector<BleCandidate> pending;
        std::map<uint64_t, TimePoint> cooldownUntil;
        std::set<uint64_t> loggedAddrs;
    };
    auto st = std::make_shared<ScanState>();

    BluetoothLEAdvertisementWatcher watcher;
    watcher.ScanningMode(BluetoothLEScanningMode::Passive);
    auto token = watcher.Received([st, kind, kindName](auto const&, auto const& args) {
        if (g_shuttingDown.load()) return;

        uint16_t productId = 0;
        bool match = false;
        std::string localName;

        try {
            auto adv = args.Advertisement();
            auto mfg = adv.ManufacturerData();
            for (uint32_t i = 0; i < mfg.Size(); i++) {
                auto sec = mfg.GetAt(i);
                if (sec.CompanyId() != JOYCON_MANUFACTURER_ID) continue;

                auto rdr = DataReader::FromBuffer(sec.Data());
                std::vector<uint8_t> d(rdr.UnconsumedBufferLength());
                rdr.ReadBytes(d);

                if (TryParseSwitch2ControllerAd(d, productId)) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                localName = winrt::to_string(adv.LocalName());
                if (!localName.empty() && NameLooksLikeSwitchController(localName)) match = true;
            }
        } catch (...) {
            return;
        }

        if (!match) return;

        const uint64_t addr = args.BluetoothAddress();
        if (IsConnectedAddr(addr)) return;

        char abuf[40];
        sprintf_s(abuf, "%012llX", static_cast<unsigned long long>(addr));

        if (!PidMatchesKind(productId, kind)) {
            bool first = false;
            { std::lock_guard<std::mutex> lk(st->mtx); first = st->loggedAddrs.insert(addr).second; }
            if (first) {
                char tmp[160];
                sprintf_s(tmp, "Ignoring controller %s (PID 0x%04X) - not a %s", abuf, (unsigned)productId, kindName.c_str());
                DiagLog(tmp);
            }
            return;
        }

        {
            std::lock_guard<std::mutex> lk(st->mtx);
            auto cd = st->cooldownUntil.find(addr);
            if (cd != st->cooldownUntil.end() && SteadyClock::now() < cd->second) return;
            for (const auto& p : st->pending) if (p.address == addr) return;
            BleCandidate c;
            c.address   = addr;
            c.productId = productId;
            c.rssi      = args.RawSignalStrengthInDBm();
            c.name      = localName;
            st->pending.push_back(c);
        }
        st->cv.notify_one();
    });
    WatcherGuard guard(watcher, token);

    statusCb("Scanning for " + kindName + "... hold its sync button");
    DiagLog("Scanning for " + kindName + " (hold its sync button)");
    try {
        watcher.Start();
    } catch (...) {
        statusCb("BLE scan failed to start (is Bluetooth turned on?)");
        return cj;
    }

    const auto deadline = SteadyClock::now() + std::chrono::seconds(60);
    bool connected = false;
    while (!connected && !g_shuttingDown.load() && SteadyClock::now() < deadline) {
        BleCandidate cand;
        {
            std::unique_lock<std::mutex> lk(st->mtx);
            st->cv.wait_for(lk, std::chrono::milliseconds(500), [&]{ return !st->pending.empty() || g_shuttingDown.load(); });
            if (st->pending.empty()) continue;
            cand = st->pending.front();
            st->pending.erase(st->pending.begin());
        }

        {
            char tmp[200];
            sprintf_s(tmp, "Found %s (addr %012llX, PID 0x%04X, RSSI %d), connecting...",
                      kindName.c_str(), static_cast<unsigned long long>(cand.address),
                      (unsigned)cand.productId, (int)cand.rssi);
            statusCb(tmp);
            DiagLog(tmp);
        }

        if (TryOpenController(cand, side, kind, statusCb, cj)) {
            connected = true;
            break;
        }



        {
            std::lock_guard<std::mutex> lk(st->mtx);
            st->cooldownUntil[cand.address] = SteadyClock::now() + std::chrono::seconds(4);
        }
        statusCb("That device didn't work - still scanning for " + kindName + " (hold its sync button)");
    }
    guard.Release();

    if (!connected) {
        cj = ConnectedJoyCon{};
        if (g_shuttingDown.load()) statusCb("Cancelled");
        else statusCb("Timed out - no " + kindName + " connected within 60 s");
        DiagLog("[ERROR] could not connect " + kindName);
        return cj;
    }

    MarkConnectedAddr(cj.address);
    outSuccess = true;
    DiagLog(kindName + " connected" + (cj.initStrategy.empty() ? std::string(" (no confirmed input stream yet)")
                                                               : " via init strategy: " + cj.initStrategy));
    statusCb("Connected!");
    return cj;
}

static void SetTaskStatus(int idx, const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_connMutex);
    if (idx >= 0 && idx < (int)g_connectionTasks.size()) g_connectionTasks[idx].statusMsg = msg;
}

static void InitViGEm() {
    if (g_vigem) return;
    g_vigem = vigem_alloc();
    if (!g_vigem || !VIGEM_SUCCESS(vigem_connect(g_vigem))) {
        AppLog("[ERROR] ViGEm failed to connect");
    }
}
static PVIGEM_TARGET AddDS4() {
    auto t = vigem_target_ds4_alloc();
    auto err = vigem_target_add(g_vigem, t);
    if (!VIGEM_SUCCESS(err))
        AppLog("[ERROR] vigem_target_add failed");
    return t;
}

static void ApplyBtnMapping(DS4_REPORT_EX& r, ButtonMapping m) {
    switch(m) {
        case ButtonMapping::L3:       r.Report.wButtons |= DS4_BUTTON_THUMB_LEFT;    break;
        case ButtonMapping::R3:       r.Report.wButtons |= DS4_BUTTON_THUMB_RIGHT;   break;
        case ButtonMapping::L1:       r.Report.wButtons |= DS4_BUTTON_SHOULDER_LEFT; break;
        case ButtonMapping::R1:       r.Report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;break;
        case ButtonMapping::L2:       r.Report.bTriggerL = 255;                      break;
        case ButtonMapping::R2:       r.Report.bTriggerR = 255;                      break;
        case ButtonMapping::CROSS:    r.Report.wButtons |= DS4_BUTTON_CROSS;         break;
        case ButtonMapping::CIRCLE:   r.Report.wButtons |= DS4_BUTTON_CIRCLE;        break;
        case ButtonMapping::SQUARE:   r.Report.wButtons |= DS4_BUTTON_SQUARE;        break;
        case ButtonMapping::TRIANGLE: r.Report.wButtons |= DS4_BUTTON_TRIANGLE;      break;
        case ButtonMapping::SHARE:    r.Report.wButtons |= DS4_BUTTON_SHARE;         break;
        case ButtonMapping::OPTIONS:  r.Report.wButtons |= DS4_BUTTON_OPTIONS;       break;
        case ButtonMapping::DPAD_UP:    DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&r.Report), DS4_BUTTON_DPAD_NORTH); break;
        case ButtonMapping::DPAD_DOWN:  DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&r.Report), DS4_BUTTON_DPAD_SOUTH); break;
        case ButtonMapping::DPAD_LEFT:  DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&r.Report), DS4_BUTTON_DPAD_WEST);  break;
        case ButtonMapping::DPAD_RIGHT: DS4_SET_DPAD(reinterpret_cast<PDS4_REPORT>(&r.Report), DS4_BUTTON_DPAD_EAST);  break;
        default: break;
    }
}
static void ApplyGLGR(DS4_REPORT_EX& r, const std::vector<uint8_t>& buf) {
    if (buf.size() < 9 || g_proConfig.layouts.empty()) return;
    int li = g_proConfig.activeLayoutIndex;
    if (li < 0 || li >= (int)g_proConfig.layouts.size()) li = 0;
    auto& lay = g_proConfig.layouts[li];
    uint64_t st = 0;
    for (int i = 3; i <= 8; ++i) st = (st << 8) | buf[i];
    if (st & 0x000000000200ULL) ApplyBtnMapping(r, lay.glMapping);
    if (st & 0x000000000100ULL) ApplyBtnMapping(r, lay.grMapping);
}
static void HandleSpecialProButtons(const std::vector<uint8_t>& buf) {
    if (buf.size() < 9) return;
    uint64_t st = 0;
    for (int i = 3; i <= 8; ++i) st = (st << 8) | buf[i];
    constexpr uint64_t SCREENSHOT = 0x000020000000;
    constexpr uint64_t CBTN       = 0x000040000000;
    constexpr uint64_t GL         = 0x000000000200;
    constexpr uint64_t GR         = 0x000000000100;
    constexpr uint64_t ZL         = 0x000000800000;
    constexpr uint64_t ZR         = 0x008000000000;

    bool combo = (st & ZL) && (st & ZR) && (st & GL) && (st & GR);
    if (combo && !g_comboPressed) { g_openLayoutManager.store(true); g_comboPressed = true; }
    else if (!combo) g_comboPressed = false;

    bool screenshot = (st & SCREENSHOT) != 0;
    if (screenshot && !g_screenshotButtonPressed) { INPUT ip{}; ip.type=INPUT_KEYBOARD; ip.ki.wVk=VK_F12; SendInput(1,&ip,sizeof(ip)); g_screenshotButtonPressed=true; }
    else if (!screenshot && g_screenshotButtonPressed) { INPUT ip{}; ip.type=INPUT_KEYBOARD; ip.ki.wVk=VK_F12; ip.ki.dwFlags=KEYEVENTF_KEYUP; SendInput(1,&ip,sizeof(ip)); g_screenshotButtonPressed=false; }

    bool cBtn = (st & CBTN) != 0;
    if (cBtn && !g_cButtonPressed) {
        if (!g_proConfig.layouts.empty()) {
            g_proConfig.activeLayoutIndex = (g_proConfig.activeLayoutIndex + 1) % (int)g_proConfig.layouts.size();
            SaveProConfig();
            AppLog(std::string("Layout -> ") + g_proConfig.layouts[g_proConfig.activeLayoutIndex].name);
        }
        g_cButtonPressed = true;
    } else if (!cBtn) g_cButtonPressed = false;
}

static void SendKey(WORD vk, bool down) {
    INPUT ip{}; ip.type=INPUT_KEYBOARD; ip.ki.wVk=vk;
    ip.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(ip));
}

struct CalibWizard {
    bool active   = false;
    int  step     = 0;
    bool isLeft   = true;
    int rawX = 2048, rawY = 2048;
    int centerX = 2048, centerY = 2048;
    int minX = 4095, maxX = 0, minY = 4095, maxY = 0;
    bool capturing = false;
    int  captureFrames = 0;
    std::vector<uint8_t> lastBuf;
};
static CalibWizard  g_calib;
static std::mutex   g_calibBufMutex;

static void ResetCalib(bool isLeft) {
    g_calib.active        = true;
    g_calib.step          = 0;
    g_calib.isLeft        = isLeft;
    g_calib.rawX          = 2048;
    g_calib.rawY          = 2048;
    g_calib.centerX       = 2048;
    g_calib.centerY       = 2048;
    g_calib.minX          = 4095;
    g_calib.maxX          = 0;
    g_calib.minY          = 4095;
    g_calib.maxY          = 0;
    g_calib.capturing     = false;
    g_calib.captureFrames = 0;
    {
        std::lock_guard<std::mutex> lk(g_calibBufMutex);
        g_calib.lastBuf.clear();
    }
}

static void FeedCalibBuffer(const std::vector<uint8_t>& buf, bool isLeftSide) {
    if (!g_calib.active) return;
    if (g_calib.isLeft != isLeftSide) return;
    std::lock_guard<std::mutex> lk(g_calibBufMutex);
    g_calib.lastBuf = buf;
}

static void UpdateCalibLiveValues() {
    std::lock_guard<std::mutex> lk(g_calibBufMutex);
    if (g_calib.lastBuf.size() < 16) return;
    ExtractRawStick(g_calib.lastBuf, g_calib.isLeft, g_calib.rawX, g_calib.rawY);
    if (g_calib.capturing) {
        g_calib.minX = std::min(g_calib.minX, g_calib.rawX);
        g_calib.maxX = std::max(g_calib.maxX, g_calib.rawX);
        g_calib.minY = std::min(g_calib.minY, g_calib.rawY);
        g_calib.maxY = std::max(g_calib.maxY, g_calib.rawY);
        ++g_calib.captureFrames;
    }
}






static void M12ProQuatToGyro(FauxGyroState& st, const std::vector<uint8_t>& buf, size_t base, float outDps[3])
{
    std::lock_guard<std::mutex> lk(st.mtx);
    outDps[0] = outDps[1] = outDps[2] = 0.f;

    if (buf.size() < base + 17 || buf[base + 3] != 0x0C) return;

    const uint16_t ts12 = static_cast<uint16_t>(((buf[base + 1] & 0x0F) << 8) | buf[base]);
    const uint8_t idx = buf[base + 4] & 0x03;

    auto rd32 = [](const uint8_t* p) -> uint32_t {
        return static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2]) << 16)
            | (static_cast<uint32_t>(p[3]) << 24);
    };
    const uint8_t* m = &buf[base + 5];
    const uint32_t w0 = rd32(m + 0);
    const uint32_t w1 = rd32(m + 4);
    const uint32_t w2 = rd32(m + 8);

    const uint32_t f0 = (w0 >> 1) & 0x01FFFFFFu;
    const uint32_t f1 = w1 & 0x01FFFFFFu;
    const uint32_t f2 = ((w2 << 1) | (w1 >> 31)) & 0x01FFFFFFu;

    constexpr float kScale = 16777216.0f * 1.41421356237f;
    const float v0 = (static_cast<int32_t>(f0) - 0x01000000) / kScale;
    const float v1 = (static_cast<int32_t>(f1) - 0x01000000) / kScale;
    const float v2 = (static_cast<int32_t>(f2) - 0x01000000) / kScale;
    const float sumSq = v0 * v0 + v1 * v1 + v2 * v2;
    if (sumSq > 1.05f) return;
    const float d = std::sqrt(std::max(0.0f, 1.0f - sumSq));

    float q[4];
    switch (idx) {
        case 0:  q[0] = d;  q[1] = v0; q[2] = v1; q[3] = v2; break;
        case 1:  q[0] = v2; q[1] = d;  q[2] = v0; q[3] = v1; break;
        case 2:  q[0] = v1; q[1] = v2; q[2] = d;  q[3] = v0; break;
        default: q[0] = v0; q[1] = v1; q[2] = v2; q[3] = d;  break;
    }

    if (!st.hasQuat) {
        st.hasQuat = true;
        st.prevTs = ts12;
        for (int i = 0; i < 4; ++i) st.prevQ[i] = q[i];
        st.out[0] = st.out[1] = st.out[2] = 0.f;
        return;
    }

    const uint16_t dticks = static_cast<uint16_t>((ts12 - st.prevTs) & 0x0FFFu);
    if (dticks == 0 || dticks > 400) {
        if (dticks > 400) {
            st.prevTs = ts12;
            for (int i = 0; i < 4; ++i) st.prevQ[i] = q[i];
            st.out[0] = st.out[1] = st.out[2] = 0.f;
        } else {
            outDps[0] = st.out[0]; outDps[1] = st.out[1]; outDps[2] = st.out[2];
        }
        return;
    }

    const float dt = static_cast<float>(dticks) / 800.0f;
    const float dot = st.prevQ[0]*q[0] + st.prevQ[1]*q[1] + st.prevQ[2]*q[2] + st.prevQ[3]*q[3];
    if (dot < 0.0f) { for (int i = 0; i < 4; ++i) q[i] = -q[i]; }


    const float pw = st.prevQ[0], px = -st.prevQ[1], py = -st.prevQ[2], pz = -st.prevQ[3];
    const float dw = pw*q[0] - px*q[1] - py*q[2] - pz*q[3];
    const float dx = pw*q[1] + px*q[0] + py*q[3] - pz*q[2];
    const float dy = pw*q[2] - px*q[3] + py*q[0] + pz*q[1];
    const float dz = pw*q[3] + px*q[2] - py*q[1] + pz*q[0];

    st.prevTs = ts12;
    for (int i = 0; i < 4; ++i) st.prevQ[i] = q[i];

    float wx = 0.f, wy = 0.f, wz = 0.f;
    const float vn = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (vn > 1e-7f) {
        const float ang = 2.0f * std::atan2(vn, std::clamp(dw, -1.0f, 1.0f));
        const float k = (ang / (dt * vn)) * 57.2957795f;
        wx = dx * k;
        wy = dy * k;
        wz = dz * k;
    }


    const float raw[3] = { wx, wz, -wy };
    constexpr float kDeadzoneDps = 0.6f;
    constexpr float kAlpha = 0.85f;
    for (int i = 0; i < 3; ++i) {
        float v = raw[i];
        v = (v > kDeadzoneDps) ? (v - kDeadzoneDps) : (v < -kDeadzoneDps) ? (v + kDeadzoneDps) : 0.f;
        st.out[i] += kAlpha * (v - st.out[i]);
        outDps[i] = st.out[i];
    }
}






static void ConsoleToGenuine(std::vector<uint8_t>& buf, bool isLeft, FauxGyroState* fg = nullptr)
{
    std::vector<uint8_t> g(63, 0);

    const size_t otherOff = isLeft ? 13 : 10;
    g[otherOff] = 0x00; g[otherOff + 1] = 0x08; g[otherOff + 2] = 0x80;

    if (buf.size() >= 8) {
        const uint16_t nb = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
        auto has = [&](int bit) { return ((nb >> bit) & 1) != 0; };

        const size_t stickOff = isLeft ? 10 : 13;
        g[stickOff] = buf[5]; g[stickOff + 1] = buf[6]; g[stickOff + 2] = buf[7];

        if (isLeft) {
            uint8_t b5 = 0, b6 = 0;
            if (has(0))  b6 |= 0x01;
            if (has(3))  b6 |= 0x02;
            if (has(1))  b6 |= 0x04;
            if (has(2))  b6 |= 0x08;
            if (has(14)) b6 |= 0x10;
            if (has(15)) b6 |= 0x20;
            if (has(4))  b6 |= 0x40;
            if (has(5))  b6 |= 0x80;
            if (has(6))  b5 |= 0x01;
            if (has(7))  b5 |= 0x08;
            if (has(8))  b5 |= 0x20;
            g[5] = b5; g[6] = b6;
        } else {
            uint8_t b4 = 0, b5 = 0;
            if (has(2))  b4 |= 0x01;
            if (has(3))  b4 |= 0x02;
            if (has(0))  b4 |= 0x04;
            if (has(1))  b4 |= 0x08;
            if (has(14)) b4 |= 0x10;
            if (has(15)) b4 |= 0x20;
            if (has(4))  b4 |= 0x40;
            if (has(5))  b4 |= 0x80;
            if (has(6))  b5 |= 0x02;
            if (has(7))  b5 |= 0x04;
            if (has(8))  b5 |= 0x10;
            if (has(12)) b5 |= 0x40;
            g[4] = b4; g[5] = b5;
        }
        g[0] = buf[0]; g[1] = buf[1];
    }



    if (fg && g_fg.enabled.load()) {
        const size_t base = isLeft ? 0x0F : 0x10;
        if (buf.size() >= base + 0x1C && buf[base - 1] >= 0x1C) {
            auto rd16 = [&](size_t o) { return static_cast<int>(static_cast<int16_t>(buf[o] | (buf[o + 1] << 8))); };
            const int rx = rd16(base + 0x12);
            const int ry = rd16(base + 0x16);
            const int rz = rd16(base + 0x1A);

            float w[3] = { 0.f, 0.f, 0.f };
            M12ProQuatToGyro(*fg, buf, base, w);

            const float gain = static_cast<float>(g_fg.gainPercent.load()) / 100.0f;
            const float sx = g_fg.invX.load() ? -1.f : 1.f;
            const float sy = g_fg.invY.load() ? -1.f : 1.f;
            const float sz = g_fg.invZ.load() ? -1.f : 1.f;
            auto put16 = [&](size_t o, int v) {
                v = std::max(-32768, std::min(32767, v));
                g[o]     = static_cast<uint8_t>(v & 0xFF);
                g[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            };
            g[0x29] = 0x01;
            put16(0x30, rx);
            put16(0x32, rz);
            put16(0x34, -ry);

            put16(0x36, static_cast<int>(std::lround(w[0] * sx * gain * 133.333f)));
            put16(0x38, static_cast<int>(std::lround(w[1] * sy * gain * 133.333f)));
            put16(0x3A, static_cast<int>(std::lround(w[2] * sz * gain * 133.333f)));
        }
    }
    buf.swap(g);
}

static void AttachSingleJoyConHandler(SingleJoyConPlayer& player, GyroMode gyroMode, uint8_t dsuSlot) {
    player.joycon.inputChar.ValueChanged(
        [&player, gyroMode, dsuSlot]
        (GattCharacteristic const&, GattValueChangedEventArgs const& args)
    {
        if (g_shuttingDown.load()) return;
        const auto now = SteadyClock::now();
        auto rdr = DataReader::FromBuffer(args.CharacteristicValue());
        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength());
        rdr.ReadBytes(buf);
        if (player.joycon.consoleMode) ConsoleToGenuine(buf, player.side == JoyConSide::Left, player.joycon.fg.get());

        FeedCalibBuffer(buf, player.side == JoyConSide::Left);

        const double bleDelta = MsBetween(player.latency.lastBleTime, now);
        player.latency.lastBleTime = now;

        if (player.side == JoyConSide::Right && player.mouseEnabled) {
            uint32_t btnState = ExtractButtonState(buf);
            bool chatPressed = (btnState & 0x000040) != 0;
            if (chatPressed && !player.wasChatPressed) {
                player.mouseMode = (player.mouseMode + 1) % 4;
                const char* names[] = {"OFF","FAST","NORMAL","SLOW"};
                uint8_t leds[] = {0x01, 0x02, 0x04, 0x08};
                AppLog(std::string("Mouse mode: ") + names[player.mouseMode]);
                SetPlayerLEDs(player.joycon.writeChar, leds[player.mouseMode]);
                EmitSound(player.joycon.writeChar);
            }
            player.wasChatPressed = chatPressed;

            if (player.mouseMode > 0) {
                auto [rx, ry] = GetRawOpticalMouse(buf);
                if (player.firstOpticalRead) { player.lastOpticalX=rx; player.lastOpticalY=ry; player.firstOpticalRead=false; }
                else {
                    int16_t dx=rx-player.lastOpticalX, dy=ry-player.lastOpticalY;
                    player.lastOpticalX=rx; player.lastOpticalY=ry;
                    if (dx||dy) {
                        float s = player.mouseMode==1?1.f:player.mouseMode==2?0.6f:0.3f;
                        INPUT ip{}; ip.type=INPUT_MOUSE; ip.mi.dx=(int)(dx*s); ip.mi.dy=(int)(dy*s); ip.mi.dwFlags=MOUSEEVENTF_MOVE;
                        SendInput(1,&ip,sizeof(ip));
                    }
                }
                bool R=(btnState&0x004000)!=0, ZR=(btnState&0x008000)!=0, ST=(btnState&0x000004)!=0;
                auto mkMouse=[](DWORD f){INPUT ip{}; ip.type=INPUT_MOUSE; ip.mi.dwFlags=f; SendInput(1,&ip,sizeof(ip));};
                if (R&&!player.leftBtnPressed)   mkMouse(MOUSEEVENTF_LEFTDOWN);
                if (!R&&player.leftBtnPressed)   mkMouse(MOUSEEVENTF_LEFTUP);
                player.leftBtnPressed=R;
                if (ZR&&!player.rightBtnPressed) mkMouse(MOUSEEVENTF_RIGHTDOWN);
                if (!ZR&&player.rightBtnPressed) mkMouse(MOUSEEVENTF_RIGHTUP);
                player.rightBtnPressed=ZR;
                if (ST&&!player.middleBtnPressed) mkMouse(MOUSEEVENTF_MIDDLEDOWN);
                if (!ST&&player.middleBtnPressed) mkMouse(MOUSEEVENTF_MIDDLEUP);
                player.middleBtnPressed=ST;

                auto sd = DecodeJoystick(buf, player.side, player.orientation);
                const int SZ=4000, BT=28000;
                if (abs(sd.y)>SZ) {
                    float inten=(abs(sd.y)-SZ)/(32767.f-SZ);
                    float spd=inten*40.f;
                    player.scrollAccumulator += sd.y>0 ? -spd : spd;
                    if (abs(player.scrollAccumulator)>=120.f) {
                        int clicks=(int)(player.scrollAccumulator/120.f);
                        player.scrollAccumulator-=clicks*120.f;
                        INPUT ip{}; ip.type=INPUT_MOUSE; ip.mi.mouseData=clicks*120; ip.mi.dwFlags=MOUSEEVENTF_WHEEL;
                        SendInput(1,&ip,sizeof(ip));
                    }
                } else player.scrollAccumulator=0.f;
                auto mkX=[](DWORD xb, DWORD f){INPUT ip{}; ip.type=INPUT_MOUSE; ip.mi.mouseData=xb; ip.mi.dwFlags=f; SendInput(1,&ip,sizeof(ip));};
                if (sd.x<-BT&&!player.mb4Pressed) { mkX(XBUTTON1,MOUSEEVENTF_XDOWN); mkX(XBUTTON1,MOUSEEVENTF_XUP); player.mb4Pressed=true; }
                else if (sd.x>=-BT) player.mb4Pressed=false;
                if (sd.x>BT&&!player.mb5Pressed) { mkX(XBUTTON2,MOUSEEVENTF_XDOWN); mkX(XBUTTON2,MOUSEEVENTF_XUP); player.mb5Pressed=true; }
                else if (sd.x<=BT) player.mb5Pressed=false;
                buf[4]&=~0x40; buf[4]&=~0x80; buf[5]&=~0x04;
                if (buf.size()>=16){buf[13]=0x00;buf[14]=0x08;buf[15]=0x80;}
            } else player.firstOpticalRead=true;
        }

        if (!ShouldEmit(g_opts.updatePolicy, player.latency.lastEmitTime, SteadyClock::now())) return;
        const auto ds = SteadyClock::now();
        DS4_REPORT_EX report = GenerateDS4Report(buf, player.side, player.orientation, player.dpadMode, player.remapSideways);
        if (g_opts.swapABXY) ApplySwapABXY(report);
        if (gyroMode==GyroMode::DsuUdp && g_dsuServer.IsRunning()) {
            g_dsuServer.UpdateController(dsuSlot, report); 
        }
        if (g_shuttingDown.load() || !g_vigem || !player.ds4Controller) return;
        vigem_target_ds4_update_ex(g_vigem, player.ds4Controller, report);
        const auto vc = SteadyClock::now();
        g_latencyLogger.Record(g_opts.updatePolicy, CtrlTypeName(1), ++player.latency.eventIndex,
                               bleDelta, 0.0, -1.0, UsBetween(ds,vc), UsBetween(now,vc));
    });
}

static ID3D11Device*           g_pd3dDevice       = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain        = nullptr;
static ID3D11RenderTargetView* g_mainRTV           = nullptr;
static HWND                    g_hwnd              = nullptr;

static void CreateRTV() {
    ID3D11Texture2D* bb = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRTV);
    bb->Release();
}
static void CleanupRTV() { if (g_mainRTV){g_mainRTV->Release();g_mainRTV=nullptr;} }
static bool CreateDX11(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount=2; sd.BufferDesc.Width=0; sd.BufferDesc.Height=0;
    sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator=60; sd.BufferDesc.RefreshRate.Denominator=1;
    sd.Flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow=hwnd;
    sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&sd,&g_pSwapChain,&g_pd3dDevice,&fl,&g_pd3dDeviceContext))) return false;
    CreateRTV(); return true;
}
static void CleanupDX11() {
    CleanupRTV();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain=nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release();  g_pd3dDeviceContext=nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();         g_pd3dDevice=nullptr; }
}

static void RequestImmediateExit() {
    g_shuttingDown.store(true);
    ExitProcess(0);
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch(msg) {
        case WM_CLOSE:
            RequestImmediateExit();
            return 0;
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                CleanupRTV();
                g_pSwapChain->ResizeBuffers(0,(UINT)LOWORD(lParam),(UINT)HIWORD(lParam),DXGI_FORMAT_UNKNOWN,0);
                CreateRTV();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam&0xfff0)==SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            g_shuttingDown.store(true);
            g_hwnd = nullptr;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void HelpMarker(const char* txt) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::TextUnformatted(txt); ImGui::EndTooltip(); }
}

static const char* FAUX_GYRO_FILE = "joycon2cpp_fauxgyro.cfg";

static void SaveFauxGyroConfig() {
    std::ofstream f(FAUX_GYRO_FILE);
    if (!f.is_open()) return;
    f << "enabled=" << (g_fg.enabled.load() ? 1 : 0) << "\n"
      << "invx="    << (g_fg.invX.load() ? 1 : 0) << "\n"
      << "invy="    << (g_fg.invY.load() ? 1 : 0) << "\n"
      << "invz="    << (g_fg.invZ.load() ? 1 : 0) << "\n"
      << "gain="    << g_fg.gainPercent.load() << "\n"
      << "dual="    << g_fg.dualSource.load() << "\n";
}

static void LoadFauxGyroConfig() {
    std::ifstream f(FAUX_GYRO_FILE);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        const auto p = line.find('=');
        if (p == std::string::npos) continue;
        const std::string key = line.substr(0, p);
        const int val = std::atoi(line.c_str() + p + 1);
        if      (key == "enabled") g_fg.enabled.store(val != 0);
        else if (key == "invx")    g_fg.invX.store(val != 0);
        else if (key == "invy")    g_fg.invY.store(val != 0);
        else if (key == "invz")    g_fg.invZ.store(val != 0);
        else if (key == "gain")    g_fg.gainPercent.store(std::max(25, std::min(300, val)));
        else if (key == "dual")    g_fg.dualSource.store(std::max(0, std::min(2, val)));
    }
}



static void DrawFauxGyroControls() {
    bool on = g_fg.enabled.load();
    if (ImGui::Checkbox("Hardware IMU gyro (M12 Pro quaternion + accel)", &on)) {
        g_fg.enabled.store(on);
        SaveFauxGyroConfig();
    }
    ImGui::SameLine();
    HelpMarker("Decodes the M12 Pro's hardware 3D orientation quaternion (including full yaw) into 3-axis gyro + aligned accel.\n"
               "Feeds both Steam (DS4 gyro) and the DSU server (X = Pitch, Y = Yaw, Z = Roll).");
    if (!on) return;

    ImGui::Indent(10);
    bool ix = g_fg.invX.load(), iy = g_fg.invY.load(), iz = g_fg.invZ.load();
    bool changed = false;
    changed |= ImGui::Checkbox("Invert Pitch (X)##fg", &ix); ImGui::SameLine();
    changed |= ImGui::Checkbox("Invert Yaw (Y)##fg", &iy); ImGui::SameLine();
    changed |= ImGui::Checkbox("Invert Roll (Z)##fg", &iz);
    ImGui::SameLine(); HelpMarker("If an axis moves the wrong way in a game, invert it here. Takes effect immediately.");

    int gp = g_fg.gainPercent.load();
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderInt("Strength %##fg", &gp, 25, 300)) { g_fg.gainPercent.store(gp); changed = true; }

    const char* srcs[] = { "Both pads", "Right pad", "Left pad" };
    int ds = g_fg.dualSource.load();
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Dual JoyCon gyro from##fg", &ds, srcs, 3)) { g_fg.dualSource.store(ds); changed = true; }
    ImGui::SameLine(); HelpMarker("Which pad of a Dual JoyCon player supplies gyro. Applied when you connect.");

    if (changed) {
        g_fg.invX.store(ix); g_fg.invY.store(iy); g_fg.invZ.store(iz);
        SaveFauxGyroConfig();
    }
    ImGui::Unindent(10);
}

static void DrawPlayerConfigRow(int i, PlayerConfig& cfg) {
    ImGui::PushID(i);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::Text("Player %d", i+1);

    ImGui::TableSetColumnIndex(1);
    const char* ctypes[] = {"Single JoyCon","Dual JoyCon","Pro Controller","NSO GC"};
    int ct = (int)cfg.controllerType - 1;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##type", &ct, ctypes, 4))
        cfg.controllerType = (ControllerType)(ct+1);

    ImGui::TableSetColumnIndex(2);
    if (cfg.controllerType == SingleJoyCon) {
        const char* sides[] = {"Left","Right"};
        int s = (cfg.joyconSide == JoyConSide::Left) ? 0 : 1;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##side", &s, sides, 2))
            cfg.joyconSide = s==0 ? JoyConSide::Left : JoyConSide::Right;
    } else ImGui::TextDisabled("—");

    ImGui::TableSetColumnIndex(3);
    if (cfg.controllerType == SingleJoyCon) {
        const char* orients[] = {"Upright","Sideways"};
        int o = (cfg.joyconOrientation == JoyConOrientation::Upright) ? 0 : 1;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##orient", &o, orients, 2))
            cfg.joyconOrientation = o==0 ? JoyConOrientation::Upright : JoyConOrientation::Sideways;
    } else ImGui::TextDisabled("—");

    ImGui::TableSetColumnIndex(4);
    if (cfg.controllerType == SingleJoyCon) {
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##mouse", &cfg.mouseEnabled);
    } else ImGui::TextDisabled("—");

    ImGui::TableSetColumnIndex(5);
    const bool leftJoyConDpadApplies =
        (cfg.controllerType == SingleJoyCon && cfg.joyconSide == JoyConSide::Left);
    if (leftJoyConDpadApplies) {
        const char* dp[] = {"Off","On"};
        int dv = (cfg.dpadMode==DpadMode::Off)?0:1;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##dpad", &dv, dp, 2))
            cfg.dpadMode = dv==0?DpadMode::Off:DpadMode::On;
    } else ImGui::TextDisabled("—");


    ImGui::TableSetColumnIndex(6);
    const bool remapApplies = (cfg.controllerType == SingleJoyCon) &&
        (cfg.joyconOrientation == JoyConOrientation::Sideways);
    if (remapApplies) {
        bool remapChecked = (cfg.remapSideways == RemapSideways::Yes);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Checkbox("##remap", &remapChecked))
            cfg.remapSideways = remapChecked ? RemapSideways::Yes : RemapSideways::No;
    } else ImGui::TextDisabled("—");


    ImGui::PopID();
}

static void DrawSetupScreen() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##setup", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

    ImGui::Spacing();
    ImGui::SetCursorPosX((io.DisplaySize.x - ImGui::CalcTextSize("joycon2cpp").x) * 0.5f);
    ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "joycon2cpp");
    ImGui::Separator(); ImGui::Spacing();

    if (ImGui::CollapsingHeader("Players", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        if (ImGui::BeginTable("players", 7,
            ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Player",       ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Type",         ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("Side",         ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Orientation",  ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableSetupColumn("Mouse",        ImGuiTableColumnFlags_WidthStretch, 0.7f);
            ImGui::TableSetupColumn("D-Pad",        ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("Remap",        ImGuiTableColumnFlags_WidthStretch, 0.9f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)g_playerConfigs.size(); ++i)
                DrawPlayerConfigRow(i, g_playerConfigs[i]);

            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("+ Add Player") && g_playerConfigs.size() < 4)
            g_playerConfigs.push_back({});
        ImGui::SameLine();
        if (ImGui::Button("- Remove Last") && g_playerConfigs.size() > 1)
            g_playerConfigs.pop_back();
        ImGui::Spacing();
    }

    DrawFauxGyroControls();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Spacing();
        ImGui::Indent(10);

        const char* policies[] = {"Low Latency (immediate)","Balanced 120Hz","Legacy 60Hz"};
        int pol = (int)g_opts.updatePolicy;
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("Update Policy", &pol, policies, 3))
            g_opts.updatePolicy = (UpdatePolicy)pol;
        ImGui::SameLine(); HelpMarker("Low Latency forwards every BLE packet immediately.\nBalanced/Legacy cap the output rate.");

        ImGui::Checkbox("Record latency metrics to CSV", &g_opts.latencyMetrics);
        if (g_opts.latencyMetrics) {
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("CSV path", g_opts.latencyCsvPath, sizeof(g_opts.latencyCsvPath));
        }

        ImGui::Checkbox("Swap A/B and X/Y buttons", &g_opts.swapABXY);
        ImGui::SameLine(); HelpMarker("Swaps Nintendo A<->B and X<->Y for every connected controller (Single JoyCon, Dual JoyCon, Pro Controller, and NSO GC).");

        ImGui::Unindent(10);
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Stick Calibration")) {
        ImGui::Spacing();
        ImGui::Indent(10);
        ImGui::TextWrapped("Calibrate after connecting. Connect your controllers first, then open this panel in-session.");

        const auto& cal = GetActiveCalibration();
        ImGui::Spacing();
        if (ImGui::BeginTable("calvals", 4, ImGuiTableFlags_Borders|ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("Stick"); ImGui::TableSetupColumn("Center");
            ImGui::TableSetupColumn("Range X"); ImGui::TableSetupColumn("Range Y");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Left");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d, %d", cal.leftStick.centerX, cal.leftStick.centerY);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d–%d", cal.leftStick.minX, cal.leftStick.maxX);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d–%d", cal.leftStick.minY, cal.leftStick.maxY);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("Right");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d, %d", cal.rightStick.centerX, cal.rightStick.centerY);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d–%d", cal.rightStick.minX, cal.rightStick.maxX);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d–%d", cal.rightStick.minY, cal.rightStick.maxY);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset to defaults")) {
            CalibrationProfile def;
            def.name = "Default";
            while (GetCalibrationProfiles().size() > 1)
                DeleteCalibrationProfile(1);
            DeleteCalibrationProfile(0);
            AddCalibrationProfile(def);
            SetActiveCalibrationIndex(0);
            SaveCalibrationProfiles("calibration.json");
        }
        ImGui::Unindent(10);
        ImGui::Spacing();
    }

    bool hasProController = false;
    for (auto& pc : g_playerConfigs) if (pc.controllerType == ProController) { hasProController = true; break; }
    if (hasProController && ImGui::CollapsingHeader("Pro Controller GL/GR Layouts")) {
        ImGui::Spacing();
        ImGui::Indent(10);
        ImGui::Text("Active layout: %s", g_proConfig.layouts[g_proConfig.activeLayoutIndex].name);
        ImGui::Spacing();
        for (int i = 0; i < (int)g_proConfig.layouts.size(); ++i) {
            ImGui::PushID(i);
            auto& lay = g_proConfig.layouts[i];
            bool isActive = (i == g_proConfig.activeLayoutIndex);
            if (isActive) ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            bool open = ImGui::TreeNodeEx(lay.name, ImGuiTreeNodeFlags_Framed);
            if (isActive) ImGui::PopStyleColor();
            if (open) {
                ImGui::InputText("Name##n", lay.name, 64);
                int gl = (int)lay.glMapping, gr = (int)lay.grMapping;
                ImGui::SetNextItemWidth(160); ImGui::Combo("GL##gl", &gl, BtnMapNames, 17); lay.glMapping=(ButtonMapping)gl;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160); ImGui::Combo("GR##gr", &gr, BtnMapNames, 17); lay.grMapping=(ButtonMapping)gr;
                ImGui::Spacing();
                if (!isActive && ImGui::Button("Set Active")) { g_proConfig.activeLayoutIndex=i; SaveProConfig(); }
                ImGui::SameLine();
                if (g_proConfig.layouts.size()>1 && ImGui::Button("Delete")) {
                    g_proConfig.layouts.erase(g_proConfig.layouts.begin()+i);
                    if (g_proConfig.activeLayoutIndex>=(int)g_proConfig.layouts.size())
                        g_proConfig.activeLayoutIndex=(int)g_proConfig.layouts.size()-1;
                    SaveProConfig(); ImGui::TreePop(); ImGui::PopID(); break;
                }
                SaveProConfig();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (ImGui::Button("+ New Layout")) {
            GLGRLayout nl; snprintf(nl.name,64,"Layout %d",(int)g_proConfig.layouts.size()+1);
            g_proConfig.layouts.push_back(nl); SaveProConfig();
        }
        ImGui::Unindent(10);
        ImGui::Spacing();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    float btnW = 180;
    ImGui::SetCursorPosX((io.DisplaySize.x - btnW) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.2f,0.6f,0.2f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f,0.75f,0.3f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.15f,0.5f,0.15f,1.f});
    bool doConnect = ImGui::Button("Connect Controllers", {btnW, 36});
    ImGui::PopStyleColor(3);

    if (doConnect && !g_playerConfigs.empty()) {
        g_connectionTasks.clear();
        for (int i = 0; i < (int)g_playerConfigs.size(); ++i) {
            auto& pc = g_playerConfigs[i];
            if (pc.controllerType == SingleJoyCon) {
                std::string side = (pc.joyconSide==JoyConSide::Left)?"Left":"Right";
                g_connectionTasks.push_back({"Player " + std::to_string(i+1) + " — " + side + " JoyCon"});
            } else if (pc.controllerType == DualJoyCon) {
                g_connectionTasks.push_back({"Player " + std::to_string(i+1) + " — RIGHT JoyCon"});
                g_connectionTasks.push_back({"Player " + std::to_string(i+1) + " — LEFT JoyCon"});
            } else if (pc.controllerType == ProController) {
                g_connectionTasks.push_back({"Player " + std::to_string(i+1) + " — Pro Controller"});
            } else {
                g_connectionTasks.push_back({"Player " + std::to_string(i+1) + " — NSO GC Controller"});
            }
        }
        g_connectionTaskIndex = 0;
        g_connectionDone      = false;
        g_connectionError     = "";
        g_screen = AppScreen::Connecting;


        for (auto& pc : g_playerConfigs) {
            if (pc.controllerType == NSOGCController) continue;
            pc.gyroMode = g_fg.enabled.load() ? GyroMode::DsuUdp : GyroMode::Raw;
            const int ds = g_fg.dualSource.load();
            pc.gyroSource = (ds == 0) ? GyroSource::Both : (ds == 1) ? GyroSource::Right : GyroSource::Left;
        }
        SaveFauxGyroConfig();

        InitViGEm();
        bool needsDsu = false;
        for (auto& pc : g_playerConfigs) if (pc.gyroMode==GyroMode::DsuUdp) { needsDsu=true; break; }
        if (needsDsu) g_dsuServer.Start();
        if (g_opts.latencyMetrics)
            g_latencyLogger.Start(g_opts.latencyCsvPath);

        std::thread([configs = g_playerConfigs]() mutable {
            int taskIdx = 0;

            g_singlePlayers.clear();
            g_dualPlayers.clear();
            g_proPlayers.clear();

            for (auto* p : g_singleRumbleCtxs) delete p;
            for (auto* p : g_dualRumbleCtxs)   delete p;
            for (auto* p : g_proRumbleCtxs)     delete p;
            g_singleRumbleCtxs.clear();
            g_dualRumbleCtxs.clear();
            g_proRumbleCtxs.clear();

            ClearConnectedAddrs();
            { std::lock_guard<std::mutex> lk(g_rxStatsMutex); g_rxStats.clear(); }
            DiagLog("---- connecting " + std::to_string(configs.size()) + " player(s) ----");

            int dsuSlot = 0;
            for (int pi = 0; pi < (int)configs.size(); ++pi) {
                auto& pc = configs[pi];

                if (pc.controllerType == SingleJoyCon) {
                    SetTaskStatus(taskIdx, "Scanning...");
                    bool ok = false;
                    auto cj = BleConnect([&](const std::string& s){ SetTaskStatus(taskIdx, s); }, ok, pc.joyconSide,
                                         pc.joyconSide == JoyConSide::Left ? DeviceKind::JoyConL : DeviceKind::JoyConR);
                    if (!ok) { g_connectionError="Failed: "+g_connectionTasks[taskIdx].label; g_connectionDone=true; return; }
                    g_connectionTasks[taskIdx].done=true; g_connectionTasks[taskIdx].success=true;
                    auto t = AddDS4();
                    auto sp = std::make_unique<SingleJoyConPlayer>();
                    sp->joycon = cj;
                    sp->ds4Controller = t;
                    sp->side = pc.joyconSide;
                    sp->orientation = pc.joyconOrientation;
                    sp->mouseEnabled = pc.mouseEnabled;
                    sp->dpadMode = pc.dpadMode;
                    sp->remapSideways = pc.remapSideways;
                    g_singlePlayers.push_back(std::move(sp));
                    auto& player = *g_singlePlayers.back();
                    if (pc.gyroMode==GyroMode::DsuUdp && g_dsuServer.IsRunning()) g_dsuServer.SetControllerConnected(dsuSlot);
                    AttachSingleJoyConHandler(player, pc.gyroMode, dsuSlot);
                    cj.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

                    auto* rctx = new SingleRumbleCtx{ cj.vibrationChar, t };
                    g_singleRumbleCtxs.push_back(rctx);
                    StartSingleRumbleThread(rctx);

                    AppLog("Player " + std::to_string(pi+1) + " Single JoyCon connected");
                    ++taskIdx; ++dsuSlot;

                } else if (pc.controllerType == DualJoyCon) {
                    SetTaskStatus(taskIdx, "Scanning...");
                    bool okR = false;
                    auto rjc = BleConnect([&](const std::string& s){ SetTaskStatus(taskIdx, s); }, okR, JoyConSide::Right, DeviceKind::JoyConR);
                    if (!okR) { g_connectionError="Failed: "+g_connectionTasks[taskIdx].label; g_connectionDone=true; return; }
                    g_connectionTasks[taskIdx].done=true; g_connectionTasks[taskIdx].success=true;
                    ++taskIdx;

                    SetTaskStatus(taskIdx, "Scanning...");
                    bool okL = false;
                    auto ljc = BleConnect([&](const std::string& s){ SetTaskStatus(taskIdx, s); }, okL, JoyConSide::Left, DeviceKind::JoyConL);
                    if (!okL) { g_connectionError="Failed: "+g_connectionTasks[taskIdx].label; g_connectionDone=true; return; }
                    g_connectionTasks[taskIdx].done=true; g_connectionTasks[taskIdx].success=true;
                    ++taskIdx;

                    auto dp = std::make_unique<DualJoyConPlayer>();
                    dp->leftJoyCon=ljc; dp->rightJoyCon=rjc;
                    dp->gyroSource=pc.gyroSource; dp->gyroMode=pc.gyroMode; dp->leftDpadMode=pc.dpadMode;
                    dp->dsuSlot=dsuSlot;
                    dp->ds4Controller=AddDS4(); dp->running.store(true);
                    dp->sharedState=std::make_shared<DualJoyConSharedState>();
                    if (dp->gyroMode==GyroMode::DsuUdp&&g_dsuServer.IsRunning()) g_dsuServer.SetControllerConnected(dsuSlot);
                    auto ss=dp->sharedState;
                    ljc.inputChar.ValueChanged([ss, cm = ljc.consoleMode, fg = ljc.fg](GattCharacteristic const&, GattValueChangedEventArgs const& a){
                        if (g_shuttingDown.load()) return;
                        auto now=SteadyClock::now(); auto rdr=DataReader::FromBuffer(a.CharacteristicValue());
                        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength()); rdr.ReadBytes(buf);
                        if (cm) ConsoleToGenuine(buf, true, fg.get());
                        FeedCalibBuffer(buf, true);
                        std::lock_guard<std::mutex> lk(ss->mutex);
                        ss->left.bleDeltaMs=MsBetween(ss->lastLeftBleTime,now); ss->lastLeftBleTime=now;
                        ss->left.buffer=std::move(buf); ss->left.receivedAt=now; ss->left.sequence=++ss->sequence;
                        ss->cv.notify_one();
                    });
                    ljc.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
                    rjc.inputChar.ValueChanged([ss, cm = rjc.consoleMode, fg = rjc.fg](GattCharacteristic const&, GattValueChangedEventArgs const& a){
                        if (g_shuttingDown.load()) return;
                        auto now=SteadyClock::now(); auto rdr=DataReader::FromBuffer(a.CharacteristicValue());
                        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength()); rdr.ReadBytes(buf);
                        if (cm) ConsoleToGenuine(buf, false, fg.get());
                        FeedCalibBuffer(buf, false);
                        std::lock_guard<std::mutex> lk(ss->mutex);
                        ss->right.bleDeltaMs=MsBetween(ss->lastRightBleTime,now); ss->lastRightBleTime=now;
                        ss->right.buffer=std::move(buf); ss->right.receivedAt=now; ss->right.sequence=++ss->sequence;
                        ss->cv.notify_one();
                    });
                    rjc.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
                    dp->updateThread=std::thread([dpptr=dp.get(),ss](){
                        uint64_t lastSeq=0;
                        uint64_t lastDsuLeftSeq=0, lastDsuRightSeq=0;
                        while (dpptr->running.load(std::memory_order_acquire) && !g_shuttingDown.load()) {
                            TimedInputBuffer ls,rs;
                            {
                                std::unique_lock<std::mutex> lk(ss->mutex);
                                ss->cv.wait_for(lk,std::chrono::milliseconds(1),[&]{ return !dpptr->running.load()||ss->sequence!=lastSeq; });
                                if (!dpptr->running.load()) break;
                                if ((ss->left.buffer.empty()&&ss->right.buffer.empty())||ss->sequence==lastSeq) continue;
                                auto now=SteadyClock::now();
                                if (!ShouldEmit(g_opts.updatePolicy,ss->lastEmitTime,now)){lastSeq=ss->sequence;continue;}
                                ls=ss->left; rs=ss->right; lastSeq=ss->sequence;
                            }
                            auto report=GenerateDualJoyConDS4Report(ls.buffer,rs.buffer,dpptr->gyroSource,dpptr->leftDpadMode);
                            if (g_opts.swapABXY) ApplySwapABXY(report);
                            if (dpptr->gyroMode==GyroMode::DsuUdp&&g_dsuServer.IsRunning()) {
                                const bool leftNew  = (ls.sequence != 0 && ls.sequence != lastDsuLeftSeq);
                                const bool rightNew = (rs.sequence != 0 && rs.sequence != lastDsuRightSeq);
                                const bool pushDsu =
                                    (dpptr->gyroSource == GyroSource::Right && rightNew) ||
                                    (dpptr->gyroSource == GyroSource::Left  && leftNew)  ||
                                    (dpptr->gyroSource == GyroSource::Both  && (leftNew || rightNew));
                                if (pushDsu) {
                                    lastDsuLeftSeq  = ls.sequence;
                                    lastDsuRightSeq = rs.sequence;
                                    g_dsuServer.UpdateController(dpptr->dsuSlot,report);
                                }
                            }
                            if (g_shuttingDown.load() || !g_vigem || !dpptr->ds4Controller) break;
                            vigem_target_ds4_update_ex(g_vigem,dpptr->ds4Controller,report);
                        }
                    });

                    auto* rctx = new DualRumbleCtx{ ljc.vibrationChar, rjc.vibrationChar, dp->ds4Controller };
                    g_dualRumbleCtxs.push_back(rctx);
                    StartDualRumbleThread(rctx);

                    g_dualPlayers.push_back(std::move(dp));
                    AppLog("Player " + std::to_string(pi+1) + " Dual JoyCon connected");
                    ++dsuSlot;

                } else if (pc.controllerType == ProController) {
                    SetTaskStatus(taskIdx, "Scanning...");
                    bool ok = false;
                    auto cj = BleConnect([&](const std::string& s){ SetTaskStatus(taskIdx, s); }, ok, JoyConSide::Right, DeviceKind::Pro);
                    if (!ok) { g_connectionError="Failed: "+g_connectionTasks[taskIdx].label; g_connectionDone=true; return; }
                    g_connectionTasks[taskIdx].done=true; g_connectionTasks[taskIdx].success=true;
                    auto tgt=AddDS4();
                    auto latPtr=std::make_shared<LatencyTracker>();
                    auto gm=pc.gyroMode; uint8_t ds=(uint8_t)dsuSlot;
                    cj.inputChar.ValueChanged([tgt,gm,ds,latPtr](GattCharacteristic const&, GattValueChangedEventArgs const& a) mutable {
                        if (g_shuttingDown.load()) return;
                        auto now=SteadyClock::now(); auto rdr=DataReader::FromBuffer(a.CharacteristicValue());
                        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength()); rdr.ReadBytes(buf);
                        FeedCalibBuffer(buf, g_calib.isLeft);
                        double bd=MsBetween(latPtr->lastBleTime,now); latPtr->lastBleTime=now;
                        if (!ShouldEmit(g_opts.updatePolicy,latPtr->lastEmitTime,SteadyClock::now())) return;
                        DS4_REPORT_EX report=GenerateProControllerReport(buf);
                        ApplyGLGR(report,buf); HandleSpecialProButtons(buf);
                        if (g_opts.swapABXY) ApplySwapABXY(report);
                        if (gm==GyroMode::DsuUdp&&g_dsuServer.IsRunning()) {
                            ApplyGLGR(report,buf); g_dsuServer.UpdateController(ds,report);
                        }
                        if (g_shuttingDown.load() || !g_vigem || !tgt) return;
                        vigem_target_ds4_update_ex(g_vigem,tgt,report);
                    });
                    cj.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();
                    if (pc.gyroMode==GyroMode::DsuUdp&&g_dsuServer.IsRunning()) g_dsuServer.SetControllerConnected(dsuSlot);

                    auto* rctx = new SingleRumbleCtx{ cj.rumbleChar, tgt, nullptr, RumbleMode::Pro };
                    g_proRumbleCtxs.push_back(rctx);
                    StartSingleRumbleThread(rctx);

                    g_proPlayers.push_back({cj,tgt});
                    AppLog("Player " + std::to_string(pi+1) + " Pro Controller connected");
                    ++taskIdx; ++dsuSlot;

                } else {
                    SetTaskStatus(taskIdx, "Scanning...");
                    bool ok = false;
                    auto cj = BleConnect([&](const std::string& s){ SetTaskStatus(taskIdx, s); }, ok, JoyConSide::Left, DeviceKind::GC);
                    if (!ok) { g_connectionError="Failed: "+g_connectionTasks[taskIdx].label; g_connectionDone=true; return; }
                    g_connectionTasks[taskIdx].done=true; g_connectionTasks[taskIdx].success=true;
                    auto tgt=AddDS4();
                    cj.inputChar.ValueChanged([tgt](GattCharacteristic const&, GattValueChangedEventArgs const& a) mutable {
                        if (g_shuttingDown.load()) return;
                        auto rdr=DataReader::FromBuffer(a.CharacteristicValue());
                        std::vector<uint8_t> buf(rdr.UnconsumedBufferLength()); rdr.ReadBytes(buf);
                        DS4_REPORT_EX report=GenerateNSOGCReport(buf);
                        if (g_opts.swapABXY) ApplySwapABXY(report);
                        if (g_shuttingDown.load() || !g_vigem || !tgt) return;
                        vigem_target_ds4_update_ex(g_vigem,tgt,report);
                    });
                    cj.inputChar.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

                    auto* rctx = new SingleRumbleCtx{ cj.vibrationChar, tgt, nullptr, RumbleMode::GC };
                    g_proRumbleCtxs.push_back(rctx);
                    StartSingleRumbleThread(rctx);

                    g_proPlayers.push_back({cj,tgt});
                    AppLog("Player " + std::to_string(pi+1) + " NSO GC connected");
                    ++taskIdx; ++dsuSlot;
                }
            }
            g_connectionDone = true;
        }).detach();
    }

    ImGui::End();
}

static void DrawConnectingScreen() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##connecting", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);

    ImGui::Spacing();
    ImGui::SetCursorPosX((io.DisplaySize.x - ImGui::CalcTextSize("Connecting...").x)*0.5f);
    ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "Connecting...");
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (!g_connectionError.empty()) {
        ImGui::TextColored({1.f,0.3f,0.3f,1.f}, "Error: %s", g_connectionError.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Back to Setup")) { g_connectionError=""; g_screen=AppScreen::Setup; }
    } else {
        for (auto& t : g_connectionTasks) {
            if (t.done) {
                ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "[OK] %s", t.label.c_str());
            } else {
                std::string status;
                { std::lock_guard<std::mutex> lk(g_connMutex); status = t.statusMsg; }
                static float spin = 0.f; spin += ImGui::GetIO().DeltaTime * 3.f;
                const char* spinners[] = {"|","/","-","\\"};
                ImGui::TextColored({1.f,0.8f,0.2f,1.f}, "[%s] %s — %s",
                    spinners[(int)(spin)%4], t.label.c_str(), status.c_str());
                break;
            }
        }


        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextDisabled("Connection log (also saved to joycon2cpp_debug.log)");
        {
            std::vector<std::string> tail;
            {
                std::lock_guard<std::mutex> lk(g_logMutex);
                size_t start = g_logLines.size() > 14 ? g_logLines.size() - 14 : 0;
                tail.assign(g_logLines.begin() + start, g_logLines.end());
            }
            ImGui::BeginChild("##connlog", ImVec2(-FLT_MIN, 0), true);
            for (auto& l : tail) ImGui::TextWrapped("%s", l.c_str());
            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (g_connectionDone) {
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "All controllers connected!");
            ImGui::Spacing();
            float bw=120;
            ImGui::SetCursorPosX((io.DisplaySize.x-bw)*0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button,{0.2f,0.6f,0.2f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.3f,0.75f,0.3f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0.15f,0.5f,0.15f,1.f});
            if (ImGui::Button("Start!", {bw,36})) g_screen=AppScreen::Running;
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();
}

static void DrawCalibWizard() {
    if (!g_calib.active) return;
    UpdateCalibLiveValues();

    ImGui::OpenPopup("Stick Calibration Wizard");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f,0.5f});
    ImGui::SetNextWindowSize({480,360}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("Stick Calibration Wizard", &g_calib.active)) {
        const char* stickName = g_calib.isLeft ? "Left Stick" : "Right Stick";
        ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "Calibrating: %s", stickName);
        ImGui::Separator(); ImGui::Spacing();

        ImGui::Text("Raw X: %4d   Raw Y: %4d", g_calib.rawX, g_calib.rawY);
        ImGui::Spacing();

        if (g_calib.step == 0) {
            ImGui::TextWrapped("This wizard will calibrate your stick's center and range.\n\n"
                               "Step 1: Release the stick and let it sit at rest. Then click Capture Center.");
            ImGui::Spacing();
            if (ImGui::Button("Capture Center", {160,32})) {
                g_calib.centerX = g_calib.rawX;
                g_calib.centerY = g_calib.rawY;
                g_calib.step = 1;
            }
        } else if (g_calib.step == 1) {
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "Center captured: %d, %d", g_calib.centerX, g_calib.centerY);
            ImGui::Spacing();
            ImGui::TextWrapped("Step 2: Slowly rotate the stick all the way around in a full circle to capture the extents.\n"
                               "Click Start Capture, rotate, then click Done.");
            ImGui::Spacing();
            if (!g_calib.capturing) {
                if (ImGui::Button("Start Capturing Extents", {200,32})) {
                    g_calib.minX=4095; g_calib.maxX=0; g_calib.minY=4095; g_calib.maxY=0;
                    g_calib.captureFrames=0; g_calib.capturing=true;
                }
            } else {
                ImGui::TextColored({1.f,0.5f,0.1f,1.f}, "Capturing... rotate the stick fully!");
                ImGui::Text("Frames: %d   MinX:%d MaxX:%d MinY:%d MaxY:%d",
                    g_calib.captureFrames, g_calib.minX, g_calib.maxX, g_calib.minY, g_calib.maxY);
                if (ImGui::Button("Done Rotating", {160,32})) {
                    g_calib.capturing = false;
                    g_calib.step = 2;
                }
            }
        } else if (g_calib.step == 2) {
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "Center: %d, %d", g_calib.centerX, g_calib.centerY);
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "X Range: %d – %d", g_calib.minX, g_calib.maxX);
            ImGui::TextColored({0.3f,1.f,0.3f,1.f}, "Y Range: %d – %d", g_calib.minY, g_calib.maxY);
            ImGui::Spacing();
            ImGui::TextWrapped("Review the values above. Click Apply to save, or Redo to recapture.");
            ImGui::Spacing();
            if (ImGui::Button("Apply", {100,32})) {
                if (g_calib.minX >= g_calib.maxX || g_calib.minY >= g_calib.maxY
                    || g_calib.captureFrames < 10) {
                    AppLog("Calibration failed: rotate the stick fully before applying.");
                    g_calib.step = 1;
                } else {
                    CalibrationProfile updated = GetActiveCalibration();
                    auto& sc = g_calib.isLeft ? updated.leftStick : updated.rightStick;
                    sc.centerX = g_calib.centerX; sc.centerY = g_calib.centerY;
                    sc.minX = g_calib.minX;       sc.maxX = g_calib.maxX;
                    sc.minY = g_calib.minY;       sc.maxY = g_calib.maxY;
                    int idx = GetActiveCalibrationIndex();
                    DeleteCalibrationProfile(idx);
                    AddCalibrationProfile(updated);
                    SetActiveCalibrationIndex((int)GetCalibrationProfiles().size()-1);
                    SaveCalibrationProfiles("calibration.json");
                    AppLog(std::string("Calibration applied for ") + stickName);
                    g_calib.active = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Redo", {100,32})) { g_calib.step=0; }
        }

        ImGui::Spacing(); ImGui::Separator();
        if (ImGui::Button("Cancel")) g_calib.active=false;
        ImGui::EndPopup();
    }
}

static void DrawLayoutManager() {
    if (!g_showLayoutManager) return;
    ImGui::OpenPopup("GL/GR Layout Manager");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, {0.5f,0.5f});
    ImGui::SetNextWindowSize({500,400}, ImGuiCond_Always);

    if (ImGui::BeginPopupModal("GL/GR Layout Manager", &g_showLayoutManager)) {
        ImGui::Text("Active: %s", g_proConfig.layouts[g_proConfig.activeLayoutIndex].name);
        ImGui::Separator(); ImGui::Spacing();

        for (int i = 0; i < (int)g_proConfig.layouts.size(); ++i) {
            ImGui::PushID(i);
            auto& lay = g_proConfig.layouts[i];
            bool isActive = (i == g_proConfig.activeLayoutIndex);
            if (isActive) ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f,0.3f,0.15f,1.f));
            ImGui::BeginChild(ImGui::GetID("##lay"), {-1,80}, true);
            ImGui::InputText("##name", lay.name, 64);
            ImGui::SameLine();
            if (isActive) ImGui::TextColored({0.3f,1.f,0.3f,1.f},"[ACTIVE]");
            int gl=(int)lay.glMapping, gr=(int)lay.grMapping;
            ImGui::SetNextItemWidth(140); ImGui::Combo("GL##g", &gl, BtnMapNames, 17); lay.glMapping=(ButtonMapping)gl;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140); ImGui::Combo("GR##r", &gr, BtnMapNames, 17); lay.grMapping=(ButtonMapping)gr;
            ImGui::SameLine();
            if (!isActive && ImGui::SmallButton("Set Active")) { g_proConfig.activeLayoutIndex=i; SaveProConfig(); }
            ImGui::SameLine();
            if (g_proConfig.layouts.size()>1 && ImGui::SmallButton("Del")) {
                g_proConfig.layouts.erase(g_proConfig.layouts.begin()+i);
                if (g_proConfig.activeLayoutIndex>=(int)g_proConfig.layouts.size())
                    g_proConfig.activeLayoutIndex=(int)g_proConfig.layouts.size()-1;
                SaveProConfig(); ImGui::EndChild(); if (isActive) ImGui::PopStyleColor(); ImGui::PopID(); break;
            }
            ImGui::EndChild();
            if (isActive) ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::Spacing();
        if (ImGui::Button("+ New Layout")) {
            GLGRLayout nl; snprintf(nl.name,64,"Layout %d",(int)g_proConfig.layouts.size()+1);
            g_proConfig.layouts.push_back(nl); SaveProConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) { g_showLayoutManager=false; SaveProConfig(); }
        ImGui::EndPopup();
    }
}

static void DrawRunningScreen() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##running", nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);

    ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "joycon2cpp — Running");
    ImGui::SameLine(io.DisplaySize.x - 110);
    ImGui::PushStyleColor(ImGuiCol_Button,{0.6f,0.2f,0.2f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.75f,0.3f,0.3f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0.5f,0.15f,0.15f,1.f});
    if (ImGui::Button("Disconnect & Exit", {80,0})) {
        RequestImmediateExit();
    }
    ImGui::PopStyleColor(3);
    ImGui::Separator(); ImGui::Spacing();

    if (ImGui::CollapsingHeader("Connected Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);
        for (int i=0; i<(int)g_singlePlayers.size(); ++i) {
            auto& p=*g_singlePlayers[i];
            ImGui::BulletText("Single JoyCon (%s, %s)",
                p.side==JoyConSide::Left?"Left":"Right",
                p.orientation==JoyConOrientation::Upright?"Upright":"Sideways");
        }
        for (int i=0; i<(int)g_dualPlayers.size(); ++i)
            ImGui::BulletText("Dual JoyCon pair");
        for (int i=0; i<(int)g_proPlayers.size(); ++i)
            ImGui::BulletText("Pro / NSO GC Controller");
        ImGui::Unindent(10); ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Hardware IMU Gyro", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);
        DrawFauxGyroControls();
        ImGui::Unindent(10); ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Live Input Monitor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);
        ImGui::TextDisabled("Every notify characteristic of each pad. Press buttons / move sticks and watch which line changes.");
        if (!g_capActive.load()) {
            if (ImGui::Button("Record 20 s motion capture")) CapStart(20);
            ImGui::SameLine();
            ImGui::TextDisabled("saves joycon2cpp_motion_capture.txt");
        } else {
            ImGui::TextColored({1.f,0.8f,0.2f,1.f}, "Recording... %d s left. Hold BOTH pads still for 4 s, then rotate each slowly around every axis.",
                               g_capSecondsLeft.load());
        }
        std::vector<std::shared_ptr<RxStats>> statsCopy;
        { std::lock_guard<std::mutex> lk(g_rxStatsMutex); statsCopy = g_rxStats; }
        if (statsCopy.empty()) ImGui::TextColored({1.f,0.6f,0.2f,1.f}, "No controllers registered.");
        for (auto& st : statsCopy) {
            std::vector<uint8_t> pkt;
            { std::lock_guard<std::mutex> lk(st->mtx); pkt = st->lastPacket; }
            const uint64_t cnt = st->count.load();
            ImVec4 col = cnt > 0 ? ImVec4(0.3f,1.f,0.3f,1.f) : ImVec4(1.f,0.35f,0.35f,1.f);
            ImGui::TextColored(col, "%s: %llu packets, last length %u bytes", st->label.c_str(),
                               (unsigned long long)cnt, (unsigned)st->lastLen.load());
            if (!pkt.empty()) {
                std::vector<uint8_t> head(pkt.begin(), pkt.begin() + (std::ptrdiff_t)std::min<size_t>(pkt.size(), 20));
                ImGui::TextDisabled("  %s", HexBytes(head).c_str());
            } else {
                ImGui::TextDisabled("  (no packets received - see joycon2cpp_debug.log)");
            }
        }
        ImGui::Unindent(10); ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Stick Calibration")) {
        ImGui::Indent(10);
        ImGui::TextDisabled("Connect a JoyCon first, then calibrate below.");
        ImGui::Spacing();

        bool hasAny = !g_singlePlayers.empty() || !g_dualPlayers.empty() || !g_proPlayers.empty();
        if (!hasAny) {
            ImGui::TextColored({1.f,0.6f,0.2f,1.f},"No controllers connected.");
        } else {
            ImGui::Text("Stick to calibrate:");
            ImGui::SameLine();
            if (ImGui::Button("Left Stick##cal")) ResetCalib(true);
            ImGui::SameLine();
            if (ImGui::Button("Right Stick##cal")) ResetCalib(false);
        }

        const auto& cal=GetActiveCalibration();
        ImGui::Spacing();
        ImGui::Text("Left:  center(%d,%d)  X[%d-%d]  Y[%d-%d]",
            cal.leftStick.centerX,cal.leftStick.centerY,
            cal.leftStick.minX,cal.leftStick.maxX,cal.leftStick.minY,cal.leftStick.maxY);
        ImGui::Text("Right: center(%d,%d)  X[%d-%d]  Y[%d-%d]",
            cal.rightStick.centerX,cal.rightStick.centerY,
            cal.rightStick.minX,cal.rightStick.maxX,cal.rightStick.minY,cal.rightStick.maxY);
        ImGui::Unindent(10); ImGui::Spacing();
    }

    bool hasProActive = !g_proPlayers.empty();
    if (hasProActive && ImGui::CollapsingHeader("GL/GR Layouts")) {
        ImGui::Indent(10);
        ImGui::Text("Active: %s  (GL: %s, GR: %s)",
            g_proConfig.layouts[g_proConfig.activeLayoutIndex].name,
            BtnMapStr(g_proConfig.layouts[g_proConfig.activeLayoutIndex].glMapping),
            BtnMapStr(g_proConfig.layouts[g_proConfig.activeLayoutIndex].grMapping));
        ImGui::SameLine();
        if (ImGui::Button("Manage Layouts")) g_showLayoutManager = true;
        ImGui::SameLine();
        if (ImGui::Button("Next Layout")) {
            g_proConfig.activeLayoutIndex = (g_proConfig.activeLayoutIndex+1) % (int)g_proConfig.layouts.size();
            SaveProConfig();
        }
        ImGui::Unindent(10); ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Indent(10);
        const char* policies[]={"Low Latency","Balanced 120Hz","Legacy 60Hz"};
        int pol=(int)g_opts.updatePolicy;
        ImGui::SetNextItemWidth(200);
        if (ImGui::Combo("Update Policy##run",&pol,policies,3))
            g_opts.updatePolicy=(UpdatePolicy)pol;

        ImGui::Checkbox("Swap A/B and X/Y buttons##run", &g_opts.swapABXY);
        ImGui::SameLine(); HelpMarker("Swaps Nintendo A<->B and X<->Y for every connected controller. Takes effect immediately.");

        ImGui::Unindent(10); ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static std::string logBuffer;
        static uint64_t lastLogVersion = (uint64_t)-1;
    
        if (lastLogVersion != g_logVersion.load())
        {
            std::lock_guard<std::mutex> lk(g_logMutex);
    
            logBuffer.clear();
    
            for (auto& line : g_logLines)
            {
                logBuffer += line;
                logBuffer += "\r\n";
            }
    
            lastLogVersion = g_logVersion.load();
        }
    
        if (ImGui::Button("Copy Log"))
        {
            ImGui::SetClipboardText(logBuffer.c_str());
        }
    
        ImGui::InputTextMultiline(
            "##log",
            logBuffer.data(),
            logBuffer.capacity() + 1,
            ImVec2(-FLT_MIN, 150),
            ImGuiInputTextFlags_ReadOnly
        );
    }

    if (g_openLayoutManager.load()) { g_openLayoutManager.store(false); g_showLayoutManager=true; }

    DrawCalibWizard();
    DrawLayoutManager();

    ImGui::End();
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    init_apartment();
    LoadProConfig();
    LoadFauxGyroConfig();
    LoadCalibrationProfiles("calibration.json");

    if (g_playerConfigs.empty()) g_playerConfigs.push_back({});

    WNDCLASSEXW wc{sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, hInst,
                   nullptr, nullptr, nullptr, nullptr, L"joycon2cpp", nullptr};
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowW(L"joycon2cpp", L"joycon2cpp",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           900, 620, nullptr, nullptr, hInst, nullptr);

    if (!CreateDX11(g_hwnd)) { DestroyWindow(g_hwnd); UnregisterClassW(wc.lpszClassName, hInst); return 1; }
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 6.f;
    style.FrameRounding  = 4.f;
    style.GrabRounding   = 4.f;
    style.WindowBorderSize = 0.f;
    style.Colors[ImGuiCol_WindowBg] = {0.08f,0.08f,0.10f,1.f};
    style.Colors[ImGuiCol_Header]   = {0.18f,0.35f,0.55f,1.f};

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        switch (g_screen) {
            case AppScreen::Setup:      DrawSetupScreen();      break;
            case AppScreen::Connecting: DrawConnectingScreen(); break;
            case AppScreen::Running:    DrawRunningScreen();    break;
        }

        ImGui::Render();
        const float cc[4] = {0.06f,0.06f,0.08f,1.f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRTV, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    g_shuttingDown.store(true);

    for (auto* p : g_singleRumbleCtxs) if (p) p->running.store(false);
    for (auto* p : g_dualRumbleCtxs)   if (p) p->running.store(false);
    for (auto* p : g_proRumbleCtxs)    if (p) p->running.store(false);

    for (auto* p : g_singleRumbleCtxs) { if (p && p->thread.joinable()) p->thread.join(); }
    for (auto* p : g_dualRumbleCtxs)   { if (p && p->thread.joinable()) p->thread.join(); }
    for (auto* p : g_proRumbleCtxs)    { if (p && p->thread.joinable()) p->thread.join(); }

    for (auto* p : g_singleRumbleCtxs) delete p;
    for (auto* p : g_dualRumbleCtxs)   delete p;
    for (auto* p : g_proRumbleCtxs)     delete p;
    g_singleRumbleCtxs.clear();
    g_dualRumbleCtxs.clear();
    g_proRumbleCtxs.clear();

    for (auto& dp : g_dualPlayers) {
        if (!dp) continue;
        dp->running.store(false);
        if (dp->sharedState) dp->sharedState->cv.notify_all();
    }
    for (auto& dp : g_dualPlayers) {
        if (dp && dp->updateThread.joinable()) dp->updateThread.join();
    }

    if (g_vigem) {
        for (auto& dp : g_dualPlayers) {
            if (dp && dp->ds4Controller) {
                vigem_target_remove(g_vigem, dp->ds4Controller);
                vigem_target_free(dp->ds4Controller);
                dp->ds4Controller = nullptr;
            }
        }
        for (auto& sp : g_singlePlayers) {
            if (sp && sp->ds4Controller) {
                vigem_target_remove(g_vigem, sp->ds4Controller);
                vigem_target_free(sp->ds4Controller);
                sp->ds4Controller = nullptr;
            }
        }
        for (auto& pp : g_proPlayers) {
            if (pp.ds4Controller) {
                vigem_target_remove(g_vigem, pp.ds4Controller);
                vigem_target_free(pp.ds4Controller);
                pp.ds4Controller = nullptr;
            }
        }

        vigem_disconnect(g_vigem);
        vigem_free(g_vigem);
        g_vigem = nullptr;
    }

    g_dsuServer.Stop();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDX11();
    if (g_hwnd) {
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
    UnregisterClassW(wc.lpszClassName, hInst);
    uninit_apartment();
    return 0;
}