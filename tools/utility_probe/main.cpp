#include "repsp/hle/kernel_errors.h"
#include "repsp/hle/utility.h"
#include "repsp/module/param_sfo.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/recomp/guest_cpu.h"
#include "repsp/runtime/guest_memory.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond)                                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(cond))                                                                   \
        {                                                                              \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

namespace
{

struct MockCpu
{
    uint32_t gpr[32] = {};
};

MockCpu g_cpu;

void MockSave(void* ctx) { std::memcpy(ctx, &g_cpu, sizeof(g_cpu)); }
void MockRestore(const void* ctx) { std::memcpy(&g_cpu, ctx, sizeof(g_cpu)); }
uint32_t MockGetGpr(int idx) { return (idx > 0 && idx < 32) ? g_cpu.gpr[idx] : 0; }
void MockSetGpr(int idx, uint32_t val)
{
    if (idx > 0 && idx < 32)
    {
        g_cpu.gpr[idx] = val;
    }
}
uint32_t MockGetFpr(int) { return 0; }
void MockSetFpr(int, uint32_t) {}
void MockCallGuest(uint32_t) {}

int32_t CallNid(uint32_t nid, uint32_t a0 = 0, uint32_t a1 = 0)
{
    MockSetGpr(4, a0);
    MockSetGpr(5, a1);
    repsp_recomp_hle_call(nid);
    return static_cast<int32_t>(MockGetGpr(2));
}

constexpr uint32_t kNidSavedataInitStart = 0x50C4CD57u;
constexpr uint32_t kNidSavedataGetStatus = 0x8874DBE0u;
constexpr uint32_t kNidSavedataUpdate = 0xD4B95FFBu;
constexpr uint32_t kNidSavedataShutdownStart = 0x9790B33Cu;

constexpr uint32_t kNidMsgDialogInitStart = 0x2AD8E239u;
constexpr uint32_t kNidMsgDialogGetStatus = 0x9A1C91D7u;
constexpr uint32_t kNidMsgDialogUpdate = 0x95FC253Bu;
constexpr uint32_t kNidMsgDialogShutdownStart = 0x67AF3428u;

constexpr uint32_t kSavedataParamSize = 1500u;
constexpr uint32_t kMsgDialogParamSize = 580u;

constexpr uint32_t kSavedataModeAutoload = 0u;
constexpr uint32_t kSavedataModeAutosave = 1u;
constexpr uint32_t kSavedataModeLoad = 2u;
constexpr uint32_t kSavedataModeSave = 3u;
constexpr uint32_t kSavedataModeListLoad = 4u;
constexpr uint32_t kSavedataModeListSave = 5u;
constexpr uint32_t kSavedataModeListDelete = 6u;
constexpr uint32_t kSavedataModeAllDelete = 7u;
constexpr uint32_t kSavedataModeSizes = 8u;

void WriteZeroedParam(uint32_t addr, uint32_t size)
{
    std::vector<uint8_t> zeros(size, 0);
    repsp::runtime::GetGuestMemory().WriteBlock(addr, zeros.data(), zeros.size());
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x00u, size);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x04u, 9u);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x08u, 0u);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x0Cu, 16u);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x10u, 16u);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x14u, 16u);
    repsp::runtime::GetGuestMemory().WriteU32(addr + 0x18u, 16u);
}

void WriteGuestCString(uint32_t addr, const std::string& text, size_t max_len)
{
    std::vector<uint8_t> buf(max_len, 0);
    const size_t copy_len = (text.size() < max_len) ? text.size() : (max_len - 1);
    std::memcpy(buf.data(), text.data(), copy_len);
    repsp::runtime::GetGuestMemory().WriteBlock(addr, buf.data(), buf.size());
}

std::string ReadGuestCString(uint32_t addr, size_t max_len)
{
    std::vector<uint8_t> buf(max_len, 0);
    repsp::runtime::GetGuestMemory().ReadBlock(addr, buf.data(), buf.size());
    size_t len = 0;
    while (len < max_len && buf[len] != 0)
    {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(buf.data()), len);
}

void WriteFileDataStruct(uint32_t struct_addr, uint32_t buf_addr, uint32_t buf_size, uint32_t size)
{
    auto& mem = repsp::runtime::GetGuestMemory();
    mem.WriteU32(struct_addr + 0x00u, buf_addr);
    mem.WriteU32(struct_addr + 0x04u, buf_size);
    mem.WriteU32(struct_addr + 0x08u, size);
    mem.WriteU32(struct_addr + 0x0Cu, 0u);
}

struct TempDirGuard
{
    std::filesystem::path dir;
    ~TempDirGuard()
    {
        std::error_code ec;
        if (!dir.empty())
        {
            std::filesystem::remove_all(dir, ec);
        }
    }
};

} // namespace

int main()
{
    TempDirGuard temp_guard;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temp_guard.dir = std::filesystem::temp_directory_path() /
                     ("repsp_utility_probe_" + std::to_string(stamp));
    std::error_code ec;
    std::filesystem::remove_all(temp_guard.dir, ec);

    repsp_guest_cpu_ops ops{};
    ops.context_size = sizeof(MockCpu);
    ops.save = MockSave;
    ops.restore = MockRestore;
    ops.get_gpr = MockGetGpr;
    ops.set_gpr = MockSetGpr;
    ops.get_fpr = MockGetFpr;
    ops.set_fpr = MockSetFpr;
    ops.call_guest = MockCallGuest;
    repsp_recomp_register_cpu(&ops);

    auto& mem = repsp::runtime::GetGuestMemory();
    TEST_ASSERT(mem.Initialize());

    repsp::hle::utility::Config cfg;
    cfg.save_root = temp_guard.dir;
    repsp::hle::utility::Register(cfg);

    const uint32_t kSaveParamAddr = 0x08900000u;
    const uint32_t kMsgParamAddr = 0x08901000u;
    const uint32_t kDataBufAddr = 0x08910000u;
    const uint32_t kIcon0BufAddr = 0x08920000u;
    const uint32_t kSaveNameListAddr = 0x08930000u;
    const uint32_t kMsFreeAddr = 0x08931000u;
    const uint32_t kMsDataAddr = 0x08931100u;
    const uint32_t kUtilityDataAddr = 0x08931200u;

    // 1. State transition check + double start error + missing save on AUTOLOAD.
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeAutoload);
    WriteGuestCString(kSaveParamAddr + 0x3Cu, "TEST00001", 13);
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "0000", 20);
    WriteGuestCString(kSaveParamAddr + 0x64u, "PROBE.DAT", 13);
    mem.WriteU32(kSaveParamAddr + 0x74u, kDataBufAddr);
    mem.WriteU32(kSaveParamAddr + 0x78u, 1024u);

    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    // Double init while active must fail with invalid status.
    TEST_ASSERT(static_cast<uint32_t>(CallNid(kNidSavedataInitStart, kSaveParamAddr)) ==
                static_cast<uint32_t>(repsp::hle::kSceUtilityErrorInvalidStatus));
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1); // INIT -> transitions to VISIBLE
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 2); // VISIBLE
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0); // VISIBLE -> QUIT
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 3); // QUIT
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0); // QUIT -> FINISHED
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4); // FINISHED -> NONE
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0); // NONE
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) ==
                static_cast<uint32_t>(repsp::hle::kSceUtilitySavedataErrorLoadNoData));

    // 2. AUTOSAVE + SFO verification + ICON0 + AUTOLOAD round trip.
    std::vector<uint8_t> payload(256);
    for (size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFFu);
    }
    std::vector<uint8_t> icon0_bytes = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3, 4};

    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeAutosave);
    mem.WriteU32(kSaveParamAddr + 0x38u, 1u);
    WriteGuestCString(kSaveParamAddr + 0x3Cu, "TEST00001", 13);
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "", 20);
    WriteGuestCString(kSaveParamAddr + 0x64u, "PROBE.DAT", 13);
    mem.WriteBlock(kDataBufAddr, payload.data(), payload.size());
    mem.WriteU32(kSaveParamAddr + 0x74u, kDataBufAddr);
    mem.WriteU32(kSaveParamAddr + 0x78u, 1024u);
    mem.WriteU32(kSaveParamAddr + 0x7Cu, static_cast<uint32_t>(payload.size()));
    WriteGuestCString(kSaveParamAddr + 0x80u, "Probe Title", 128);
    WriteGuestCString(kSaveParamAddr + 0x100u, "Save Profile", 128);
    WriteGuestCString(kSaveParamAddr + 0x180u, "Player records and progress", 1024);
    mem.WriteU8(kSaveParamAddr + 0x580u, 3u);

    mem.WriteBlock(kIcon0BufAddr, icon0_bytes.data(), icon0_bytes.size());
    WriteFileDataStruct(kSaveParamAddr + 0x584u, kIcon0BufAddr, 256u, static_cast<uint32_t>(icon0_bytes.size()));

    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    while (CallNid(kNidSavedataGetStatus) != 0)
    {
        const int32_t st = CallNid(kNidSavedataGetStatus);
        if (st == 2)
        {
            TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
        }
        else if (st == 3)
        {
            TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
        }
    }
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);

    // Check PARAM.SFO written on disk parses cleanly via ParamSfo::Parse.
    const auto slot_dir = temp_guard.dir / "TEST00001";
    const auto sfo_path = slot_dir / "PARAM.SFO";
    TEST_ASSERT(std::filesystem::exists(sfo_path));
    {
        std::ifstream ifs(sfo_path, std::ios::binary | std::ios::ate);
        TEST_ASSERT(ifs.good());
        const auto sz = static_cast<size_t>(ifs.tellg());
        ifs.seekg(0, std::ios::beg);
        std::vector<uint8_t> sfo_raw(sz);
        ifs.read(reinterpret_cast<char*>(sfo_raw.data()), static_cast<std::streamsize>(sz));
        auto parsed = repsp::module::ParamSfo::Parse(sfo_raw.data(), sfo_raw.size());
        TEST_ASSERT(parsed.has_value());
        TEST_ASSERT(parsed->GetString("SAVEDATA_DIRECTORY") == std::optional<std::string>("TEST00001"));
        TEST_ASSERT(parsed->GetString("TITLE") == std::optional<std::string>("Probe Title"));
        TEST_ASSERT(parsed->GetString("SAVEDATA_TITLE") == std::optional<std::string>("Save Profile"));
        TEST_ASSERT(parsed->GetString("SAVEDATA_DETAIL") ==
                    std::optional<std::string>("Player records and progress"));
        TEST_ASSERT(parsed->GetInteger("PARENTAL_LEVEL") == std::optional<uint32_t>(3u));
        TEST_ASSERT(parsed->HasKey("SAVEDATA_FILE_LIST"));
        TEST_ASSERT(parsed->HasKey("SAVEDATA_PARAMS"));
    }

    // Load back via AUTOLOAD with cleared guest buffers.
    std::vector<uint8_t> zeros(1024, 0);
    mem.WriteBlock(kDataBufAddr, zeros.data(), zeros.size());
    mem.WriteBlock(kIcon0BufAddr, zeros.data(), 256);

    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeAutoload);
    WriteGuestCString(kSaveParamAddr + 0x3Cu, "TEST00001", 13);
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "", 20);
    WriteGuestCString(kSaveParamAddr + 0x64u, "PROBE.DAT", 13);
    mem.WriteU32(kSaveParamAddr + 0x74u, kDataBufAddr);
    mem.WriteU32(kSaveParamAddr + 0x78u, 1024u);
    WriteFileDataStruct(kSaveParamAddr + 0x584u, kIcon0BufAddr, 256u, 0u);

    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 2);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 3);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x7Cu) == static_cast<uint32_t>(payload.size()));

    std::vector<uint8_t> read_back(payload.size(), 0);
    mem.ReadBlock(kDataBufAddr, read_back.data(), read_back.size());
    TEST_ASSERT(read_back == payload);
    TEST_ASSERT(ReadGuestCString(kSaveParamAddr + 0x80u, 128) == "Probe Title");
    TEST_ASSERT(ReadGuestCString(kSaveParamAddr + 0x100u, 128) == "Save Profile");
    TEST_ASSERT(mem.ReadU8(kSaveParamAddr + 0x580u) == 3u);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x584u + 0x08u) ==
                static_cast<uint32_t>(icon0_bytes.size()));

    // 3. LOAD with buffer too small -> kSceUtilitySavedataErrorLoadParam.
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeLoad);
    mem.WriteU32(kSaveParamAddr + 0x78u, 64u);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) ==
                static_cast<uint32_t>(repsp::hle::kSceUtilitySavedataErrorLoadParam));

    // 4. SIZES mode writes msFree / msData / utilityData.
    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeSizes);
    WriteGuestCString(kSaveParamAddr + 0x3Cu, "TEST00001", 13);
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "", 20);
    WriteGuestCString(kSaveParamAddr + 0x64u, "PROBE.DAT", 13);
    mem.WriteU32(kSaveParamAddr + 0x7Cu, static_cast<uint32_t>(payload.size()));
    mem.WriteU32(kMsFreeAddr, 0u);
    mem.WriteU32(kMsFreeAddr + 4u, 0u);
    mem.WriteU32(kSaveParamAddr + 0x5D0u, kMsFreeAddr);
    mem.WriteU32(kSaveParamAddr + 0x5D4u, kMsDataAddr);
    mem.WriteU32(kSaveParamAddr + 0x5D8u, kUtilityDataAddr);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    TEST_ASSERT(mem.ReadU32(kMsFreeAddr) > 0u);
    TEST_ASSERT(mem.ReadU32(kMsFreeAddr + 4u) > 0u);
    TEST_ASSERT(mem.ReadU32(kUtilityDataAddr) >= 1u);

    // 5. LISTSAVE -> LISTLOAD -> LISTDELETE -> ALLDELETE (with 0x0A sentinel at entry[1]).
    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeListSave);
    WriteGuestCString(kSaveParamAddr + 0x3Cu, "TEST00001", 13);
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "SLOT01", 20);
    WriteGuestCString(kSaveNameListAddr, "SLOT01", 20);
    mem.WriteU8(kSaveNameListAddr + 20u, 0x0Au); // < 0x20 sentinel used by BOOT.BIN
    mem.WriteU32(kSaveParamAddr + 0x60u, kSaveNameListAddr);
    WriteGuestCString(kSaveParamAddr + 0x64u, "PROBE.DAT", 13);
    mem.WriteBlock(kDataBufAddr, payload.data(), payload.size());
    mem.WriteU32(kSaveParamAddr + 0x74u, kDataBufAddr);
    mem.WriteU32(kSaveParamAddr + 0x78u, 1024u);
    mem.WriteU32(kSaveParamAddr + 0x7Cu, static_cast<uint32_t>(payload.size()));
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    TEST_ASSERT(std::filesystem::exists(temp_guard.dir / "TEST00001SLOT01" / "PROBE.DAT"));

    mem.WriteBlock(kDataBufAddr, zeros.data(), zeros.size());
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeListLoad);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    mem.ReadBlock(kDataBufAddr, read_back.data(), read_back.size());
    TEST_ASSERT(read_back == payload);

    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeListDelete);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    TEST_ASSERT(!std::filesystem::exists(temp_guard.dir / "TEST00001SLOT01"));

    // Create SLOT02 then delete via ALLDELETE with list containing SLOT02 + 0x0A sentinel.
    WriteGuestCString(kSaveParamAddr + 0x4Cu, "SLOT02", 20);
    WriteGuestCString(kSaveNameListAddr, "SLOT02", 20);
    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeSave);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(std::filesystem::exists(temp_guard.dir / "TEST00001SLOT02"));

    mem.WriteU32(kSaveParamAddr + 0x30u, kSavedataModeAllDelete);
    TEST_ASSERT(CallNid(kNidSavedataInitStart, kSaveParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidSavedataUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidSavedataShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidSavedataGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kSaveParamAddr + 0x1Cu) == 0u);
    TEST_ASSERT(!std::filesystem::exists(temp_guard.dir / "TEST00001SLOT02"));

    // 6. MsgDialog text + SetDialogPolicy (Yes and No) + error mode.
    std::string captured_text;
    repsp::hle::utility::ChoiceKind captured_choice = repsp::hle::utility::ChoiceKind::kNone;
    repsp::hle::utility::SetDialogPolicy(
        [&](repsp::hle::utility::DialogKind kind, const std::string& text, repsp::hle::utility::ChoiceKind choice)
        {
            if (kind == repsp::hle::utility::DialogKind::kMsgDialog)
            {
                captured_text = text;
                captured_choice = choice;
            }
            return repsp::hle::utility::DialogAnswer::kYes;
        });

    WriteZeroedParam(kMsgParamAddr, kMsgDialogParamSize);
    mem.WriteU32(kMsgParamAddr + 0x34u, 1u); // text mode
    WriteGuestCString(kMsgParamAddr + 0x3Cu, "Create new save file?", 512);
    mem.WriteU32(kMsgParamAddr + 0x23Cu, 0x10u); // Yes/No buttons

    TEST_ASSERT(CallNid(kNidMsgDialogInitStart, kMsgParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidMsgDialogUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 0);
    TEST_ASSERT(captured_text == "Create new save file?");
    TEST_ASSERT(captured_choice == repsp::hle::utility::ChoiceKind::kYesNo);
    TEST_ASSERT(mem.ReadU32(kMsgParamAddr + 0x240u) == 1u); // YES

    repsp::hle::utility::SetDialogPolicy(
        [](repsp::hle::utility::DialogKind, const std::string&, repsp::hle::utility::ChoiceKind)
        { return repsp::hle::utility::DialogAnswer::kNo; });

    WriteZeroedParam(kMsgParamAddr, kMsgDialogParamSize);
    mem.WriteU32(kMsgParamAddr + 0x34u, 1u);
    WriteGuestCString(kMsgParamAddr + 0x3Cu, "Continue without saving?", 512);
    mem.WriteU32(kMsgParamAddr + 0x23Cu, 0x10u);
    TEST_ASSERT(CallNid(kNidMsgDialogInitStart, kMsgParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidMsgDialogUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kMsgParamAddr + 0x240u) == 2u); // NO

    // Error mode MsgDialog (mode=0, error=0x80110383) with explicit kCancel policy
    repsp::hle::utility::SetDialogPolicy(
        [](repsp::hle::utility::DialogKind, const std::string&, repsp::hle::utility::ChoiceKind)
        { return repsp::hle::utility::DialogAnswer::kCancel; });
    WriteZeroedParam(kMsgParamAddr, kMsgDialogParamSize);
    mem.WriteU32(kMsgParamAddr + 0x34u, 0u);
    mem.WriteU32(kMsgParamAddr + 0x38u, 0x80110383u);
    mem.WriteU32(kMsgParamAddr + 0x23Cu, 0u);
    TEST_ASSERT(CallNid(kNidMsgDialogInitStart, kMsgParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidMsgDialogUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kMsgParamAddr + 0x240u) == 3u); // BACK

    // Default policy (nullptr) restores Confirm/Yes -> 1
    repsp::hle::utility::SetDialogPolicy(nullptr);
    WriteZeroedParam(kMsgParamAddr, kMsgDialogParamSize);
    mem.WriteU32(kMsgParamAddr + 0x34u, 1u);
    WriteGuestCString(kMsgParamAddr + 0x3Cu, "Save complete.", 512);
    mem.WriteU32(kMsgParamAddr + 0x23Cu, 0u);
    TEST_ASSERT(CallNid(kNidMsgDialogInitStart, kMsgParamAddr) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 1);
    TEST_ASSERT(CallNid(kNidMsgDialogUpdate, 1) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogShutdownStart) == 0);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 4);
    TEST_ASSERT(CallNid(kNidMsgDialogGetStatus) == 0);
    TEST_ASSERT(mem.ReadU32(kMsgParamAddr + 0x240u) == 1u); // Confirm -> YES/OK

    const auto stats = repsp::hle::utility::GetStats();
    TEST_ASSERT(stats.dialog_records.size() == 4);
    TEST_ASSERT(stats.dialog_records[0].text == "Create new save file?");
    TEST_ASSERT(stats.dialog_records[1].text == "Continue without saving?");
    TEST_ASSERT(stats.dialog_records[2].error_value == 0x80110383u);
    TEST_ASSERT(stats.dialog_records[3].text == "Save complete.");

    const std::string formatted = repsp::hle::utility::FormatStats();
    TEST_ASSERT(formatted.find("dialog UI: not drawn") != std::string::npos);

    // 7. Unimplemented savedata mode triggers GuardedRun stop with mode in stop_reason.
    WriteZeroedParam(kSaveParamAddr, kSavedataParamSize);
    mem.WriteU32(kSaveParamAddr + 0x30u, 99u);
    const repsp::platform::GuardedRunResult stop_res = repsp::platform::GuardedRun(
        [&]()
        {
            CallNid(kNidSavedataInitStart, kSaveParamAddr);
        });
    TEST_ASSERT(stop_res.stopped);
    TEST_ASSERT(stop_res.stop_reason.find("99") != std::string::npos);

    repsp::hle::utility::Shutdown();
    mem.Shutdown();

    std::printf("utility_probe passed\n");
    return 0;
}
