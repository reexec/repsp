#include "repsp/hle/utility.h"

#include "repsp/hle/kernel_errors.h"
#include "repsp/module/param_sfo.h"
#include "repsp/platform/guarded_run.h"
#include "repsp/platform/path.h"
#include "repsp/recomp/recomp_helpers.h"
#include "repsp/runtime/guest_memory.h"
#include "repsp/runtime/log.h"
#include "repsp/runtime/nid_registry.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace repsp::hle::utility
{

namespace
{

constexpr int32_t kDialogStatusNone = 0;
constexpr int32_t kDialogStatusInit = 1;
constexpr int32_t kDialogStatusVisible = 2;
constexpr int32_t kDialogStatusQuit = 3;
constexpr int32_t kDialogStatusFinished = 4;

constexpr uint32_t kDialogCommonSize = 48u;
constexpr uint32_t kSavedataMinParamSize = 1480u;
constexpr uint32_t kSavedataMaxParamSize = 1600u;
constexpr uint32_t kSavedataExtendedOffsetThreshold = 1500u;
constexpr uint32_t kMsgDialogMinParamSize = 572u;
constexpr uint32_t kMsgDialogMaxParamSize = 640u;

constexpr int32_t kMsgDialogModeError = 0;
constexpr int32_t kMsgDialogModeText = 1;
constexpr uint32_t kMsgDialogOptionYesNoButtons = 0x10u;

constexpr int32_t kMsgDialogButtonInvalid = 0;
constexpr int32_t kMsgDialogButtonYes = 1;
constexpr int32_t kMsgDialogButtonNo = 2;
constexpr int32_t kMsgDialogButtonBack = 3;

constexpr uint32_t kSavedataModeAutoload = 0u;
constexpr uint32_t kSavedataModeAutosave = 1u;
constexpr uint32_t kSavedataModeLoad = 2u;
constexpr uint32_t kSavedataModeSave = 3u;
constexpr uint32_t kSavedataModeListLoad = 4u;
constexpr uint32_t kSavedataModeListSave = 5u;
constexpr uint32_t kSavedataModeListDelete = 6u;
constexpr uint32_t kSavedataModeAllDelete = 7u;
constexpr uint32_t kSavedataModeSizes = 8u;

constexpr uint32_t kSfoMagic = 0x46535000u;
constexpr uint32_t kSfoVersion = 0x00000101u;
constexpr uint16_t kSfoFormatBinary = 0x0004u;
constexpr uint16_t kSfoFormatUtf8 = 0x0204u;
constexpr uint16_t kSfoFormatInt32 = 0x0404u;

constexpr uint32_t kMsClusterSizeBytes = 32768u;
constexpr int32_t kMsReportedFreeClusters = 32768; // 1 GiB free reported to guest
constexpr int32_t kMsReportedMaxClusters = 32768;

// Offsets inside pspUtilityDialogCommon (48 bytes)
constexpr uint32_t kCommonOffSize = 0x00u;
constexpr uint32_t kCommonOffResult = 0x1Cu;

// Offsets inside SceUtilitySavedataParam (1500-byte layout)
constexpr uint32_t kSaveOffMode = 0x30u;
constexpr uint32_t kSaveOffOverwrite = 0x38u;
constexpr uint32_t kSaveOffGameName = 0x3Cu;     // char[13]
constexpr uint32_t kSaveOffSaveName = 0x4Cu;     // char[20]
constexpr uint32_t kSaveOffSaveNameList = 0x60u; // char(*)[20]
constexpr uint32_t kSaveOffFileName = 0x64u;     // char[13]
constexpr uint32_t kSaveOffDataBuf = 0x74u;
constexpr uint32_t kSaveOffDataBufSize = 0x78u;
constexpr uint32_t kSaveOffDataSize = 0x7Cu;
constexpr uint32_t kSaveOffSfoTitle = 0x80u;          // char[128]
constexpr uint32_t kSaveOffSfoSavedataTitle = 0x100u; // char[128]
constexpr uint32_t kSaveOffSfoDetail = 0x180u;        // char[1024]
constexpr uint32_t kSaveOffSfoParentalLevel = 0x580u; // uint8_t
constexpr uint32_t kSaveOffIcon0 = 0x584u;
constexpr uint32_t kSaveOffIcon1 = 0x594u;
constexpr uint32_t kSaveOffPic1 = 0x5A4u;
constexpr uint32_t kSaveOffSnd0 = 0x5B4u;
constexpr uint32_t kSaveOffMsFree = 0x5D0u;
constexpr uint32_t kSaveOffMsData = 0x5D4u;
constexpr uint32_t kSaveOffUtilityData = 0x5D8u;

// Offsets inside pspUtilityMsgDialogParams (580-byte layout)
constexpr uint32_t kMsgOffUnknownResult = 0x30u;
constexpr uint32_t kMsgOffMode = 0x34u;
constexpr uint32_t kMsgOffErrorValue = 0x38u;
constexpr uint32_t kMsgOffMessage = 0x3Cu; // char[512]
constexpr uint32_t kMsgOffOptions = 0x23Cu;
constexpr uint32_t kMsgOffButtonPressed = 0x240u;

enum class ActiveDialogType
{
    kNone = 0,
    kSavedata = 1,
    kMsgDialog = 2,
};

struct UtilityState
{
    Config config;
    bool registered = false;
    ActiveDialogType active_type = ActiveDialogType::kNone;
    int32_t dialog_status = kDialogStatusNone;
    DialogPolicy policy;
    Stats stats;
};

std::mutex g_mutex;
UtilityState g_state;

bool IsValidRange(uint32_t addr, uint32_t size)
{
    if (addr == 0 || size == 0)
    {
        return false;
    }
    if (size - 1u > UINT32_MAX - addr)
    {
        return false;
    }
    return runtime::GetGuestMemory().IsValidAddress(addr, size);
}

uint8_t ReadU8(uint32_t addr)
{
    return *runtime::GetGuestMemory().Translate(addr);
}

uint32_t ReadU32(uint32_t addr)
{
    uint32_t val = 0;
    std::memcpy(&val, runtime::GetGuestMemory().Translate(addr), sizeof(val));
    return val;
}

int32_t ReadI32(uint32_t addr)
{
    return static_cast<int32_t>(ReadU32(addr));
}

void WriteU8(uint32_t addr, uint8_t val)
{
    *runtime::GetGuestMemory().Translate(addr) = val;
}

void WriteU32(uint32_t addr, uint32_t val)
{
    std::memcpy(runtime::GetGuestMemory().Translate(addr), &val, sizeof(val));
}

void WriteI32(uint32_t addr, int32_t val)
{
    WriteU32(addr, static_cast<uint32_t>(val));
}

std::string ReadFixedString(uint32_t addr, size_t max_bytes)
{
    if (!IsValidRange(addr, static_cast<uint32_t>(max_bytes)))
    {
        return {};
    }
    const char* ptr = reinterpret_cast<const char*>(runtime::GetGuestMemory().Translate(addr));
    size_t len = 0;
    while (len < max_bytes && ptr[len] != '\0')
    {
        ++len;
    }
    return std::string(ptr, len);
}

void WriteFixedString(uint32_t addr, size_t max_bytes, const std::string& text)
{
    if (max_bytes == 0 || !IsValidRange(addr, static_cast<uint32_t>(max_bytes)))
    {
        return;
    }
    char* dst = reinterpret_cast<char*>(runtime::GetGuestMemory().Translate(addr));
    std::memset(dst, 0, max_bytes);
    const size_t copy_len = std::min(text.size(), max_bytes - 1u);
    if (copy_len > 0)
    {
        std::memcpy(dst, text.data(), copy_len);
    }
}

bool IsSafeNameComponent(const std::string& name, bool allow_empty)
{
    if (name.empty())
    {
        return allow_empty;
    }
    if (name == "." || name == ".." || name.find("..") != std::string::npos)
    {
        return false;
    }
    for (unsigned char ch : name)
    {
        if (ch < 0x20u || ch == '/' || ch == '\\' || ch == ':')
        {
            return false;
        }
    }
    return true;
}

std::filesystem::path ResolveSaveRoot(const Config& cfg)
{
    if (!cfg.save_root.empty())
    {
        return cfg.save_root;
    }
    return platform::GetUserDataDirectory("repsp") / "savedata";
}

bool AtomicWriteBinaryFile(const std::filesystem::path& target, const uint8_t* data, size_t size)
{
    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    std::filesystem::path tmp_path = target;
    tmp_path += ".tmp";

    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
        {
            return false;
        }
        if (size > 0)
        {
            out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        }
        out.flush();
        if (!out.good())
        {
            out.close();
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }

    std::filesystem::rename(tmp_path, target, ec);
    if (ec)
    {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open())
    {
        return false;
    }
    const std::streamsize size = in.tellg();
    if (size < 0)
    {
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(out.data()), size))
    {
        out.clear();
        return false;
    }
    return true;
}

void AppendLe16(std::vector<uint8_t>& dst, uint16_t val)
{
    dst.push_back(static_cast<uint8_t>(val & 0xFFu));
    dst.push_back(static_cast<uint8_t>((val >> 8) & 0xFFu));
}

void AppendLe32(std::vector<uint8_t>& dst, uint32_t val)
{
    dst.push_back(static_cast<uint8_t>(val & 0xFFu));
    dst.push_back(static_cast<uint8_t>((val >> 8) & 0xFFu));
    dst.push_back(static_cast<uint8_t>((val >> 16) & 0xFFu));
    dst.push_back(static_cast<uint8_t>((val >> 24) & 0xFFu));
}

void BuildParamSfo(const std::string& directory_name,
                   const std::string& file_name,
                   const std::string& title,
                   const std::string& savedata_title,
                   const std::string& detail,
                   uint8_t parental_level,
                   std::vector<uint8_t>& out)
{
    struct SfoField
    {
        const char* key;
        uint16_t format;
        uint32_t max_len;
        std::vector<uint8_t> bytes;
    };

    auto make_str_field = [](const char* key, uint32_t max_len, const std::string& val)
    {
        SfoField field;
        field.key = key;
        field.format = kSfoFormatUtf8;
        field.max_len = max_len;
        const size_t copy_len = std::min(val.size(), static_cast<size_t>(max_len > 0 ? max_len - 1u : 0u));
        field.bytes.assign(val.begin(), val.begin() + static_cast<std::ptrdiff_t>(copy_len));
        field.bytes.push_back(0u);
        return field;
    };

    SfoField parental;
    parental.key = "PARENTAL_LEVEL";
    parental.format = kSfoFormatInt32;
    parental.max_len = 4u;
    AppendLe32(parental.bytes, static_cast<uint32_t>(parental_level));

    SfoField file_list;
    file_list.key = "SAVEDATA_FILE_LIST";
    file_list.format = kSfoFormatBinary;
    file_list.max_len = 3168u;
    file_list.bytes.assign(3168u, 0u);
    if (!file_name.empty())
    {
        const size_t copy_len = std::min<size_t>(file_name.size(), 12u);
        std::memcpy(file_list.bytes.data(), file_name.data(), copy_len);
    }

    SfoField savedata_params;
    savedata_params.key = "SAVEDATA_PARAMS";
    savedata_params.format = kSfoFormatBinary;
    savedata_params.max_len = 128u;
    savedata_params.bytes.assign(128u, 0u);

    // Keys sorted in ascending ASCII order per PSF convention.
    std::vector<SfoField> fields;
    fields.push_back(make_str_field("CATEGORY", 4u, "MS"));
    fields.push_back(std::move(parental));
    fields.push_back(make_str_field("SAVEDATA_DETAIL", 1024u, detail));
    fields.push_back(make_str_field("SAVEDATA_DIRECTORY", 64u, directory_name));
    fields.push_back(std::move(file_list));
    fields.push_back(std::move(savedata_params));
    fields.push_back(make_str_field("SAVEDATA_TITLE", 128u, savedata_title));
    fields.push_back(make_str_field("TITLE", 128u, title));

    const uint32_t num_entries = static_cast<uint32_t>(fields.size());
    const uint32_t key_table_offset = 20u + num_entries * 16u;

    std::vector<uint8_t> key_table;
    std::vector<uint16_t> key_offsets;
    for (const auto& f : fields)
    {
        key_offsets.push_back(static_cast<uint16_t>(key_table.size()));
        const char* p = f.key;
        while (*p != '\0')
        {
            key_table.push_back(static_cast<uint8_t>(*p++));
        }
        key_table.push_back(0u);
    }
    while ((key_table.size() % 4u) != 0u)
    {
        key_table.push_back(0u);
    }

    const uint32_t data_table_offset = key_table_offset + static_cast<uint32_t>(key_table.size());
    std::vector<uint8_t> data_table;
    std::vector<uint32_t> data_offsets;
    for (const auto& f : fields)
    {
        data_offsets.push_back(static_cast<uint32_t>(data_table.size()));
        data_table.insert(data_table.end(), f.bytes.begin(), f.bytes.end());
        while ((data_table.size() % 4u) != 0u)
        {
            data_table.push_back(0u);
        }
    }

    out.clear();
    out.reserve(data_table_offset + data_table.size());
    AppendLe32(out, kSfoMagic);
    AppendLe32(out, kSfoVersion);
    AppendLe32(out, key_table_offset);
    AppendLe32(out, data_table_offset);
    AppendLe32(out, num_entries);

    for (size_t i = 0; i < fields.size(); ++i)
    {
        AppendLe16(out, key_offsets[i]);
        AppendLe16(out, fields[i].format);
        AppendLe32(out, static_cast<uint32_t>(fields[i].bytes.size()));
        AppendLe32(out, fields[i].max_len);
        AppendLe32(out, data_offsets[i]);
    }

    out.insert(out.end(), key_table.begin(), key_table.end());
    out.insert(out.end(), data_table.begin(), data_table.end());
}

uint64_t ComputeDirectorySizeBytes(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
    {
        return 0u;
    }
    uint64_t total = 0u;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec))
        {
            total += entry.file_size(ec);
        }
    }
    return total;
}

void WriteUsedDataInfo(uint32_t addr, uint64_t bytes)
{
    if (!IsValidRange(addr, 28u))
    {
        return;
    }
    const int32_t kb = static_cast<int32_t>((bytes + 1023u) / 1024u);
    const int32_t clusters = static_cast<int32_t>((bytes + kMsClusterSizeBytes - 1u) / kMsClusterSizeBytes);
    const int32_t kb32 = clusters * static_cast<int32_t>(kMsClusterSizeBytes / 1024u);

    char size_str[8] = {};
    char size32_str[8] = {};
    std::snprintf(size_str, sizeof(size_str), "%d KB", kb);
    std::snprintf(size32_str, sizeof(size32_str), "%d KB", kb32);

    WriteI32(addr + 0x00u, clusters);
    WriteI32(addr + 0x04u, kb);
    WriteFixedString(addr + 0x08u, 8u, size_str);
    WriteI32(addr + 0x10u, kb32);
    WriteFixedString(addr + 0x14u, 8u, size32_str);
}

void PopulateSavedataSizeStructs(uint32_t param_addr,
                                 uint32_t param_size,
                                 const std::string& game_name,
                                 const std::string& save_name,
                                 const std::filesystem::path& slot_dir,
                                 uint64_t pending_data_bytes)
{
    if (param_size < kSavedataExtendedOffsetThreshold)
    {
        return;
    }

    const uint32_t ms_free_addr = ReadU32(param_addr + kSaveOffMsFree);
    if (IsValidRange(ms_free_addr, 20u))
    {
        WriteI32(ms_free_addr + 0x00u, static_cast<int32_t>(kMsClusterSizeBytes));
        WriteI32(ms_free_addr + 0x04u, kMsReportedFreeClusters);
        WriteI32(ms_free_addr + 0x08u, kMsReportedMaxClusters);
        WriteFixedString(ms_free_addr + 0x0Cu, 8u, "1024 MB");
    }

    const uint64_t on_disk_bytes = ComputeDirectorySizeBytes(slot_dir);
    const uint32_t ms_data_addr = ReadU32(param_addr + kSaveOffMsData);
    if (IsValidRange(ms_data_addr, 64u))
    {
        WriteFixedString(ms_data_addr + 0x00u, 13u, game_name);
        WriteFixedString(ms_data_addr + 0x10u, 20u, save_name);
        WriteUsedDataInfo(ms_data_addr + 0x24u, on_disk_bytes);
    }

    const uint32_t util_data_addr = ReadU32(param_addr + kSaveOffUtilityData);
    if (IsValidRange(util_data_addr, 28u))
    {
        const uint64_t util_bytes = (pending_data_bytes > 0u) ? pending_data_bytes : on_disk_bytes;
        WriteUsedDataInfo(util_data_addr, util_bytes);
    }
}

std::vector<std::string> ReadSaveNameList(uint32_t list_addr)
{
    std::vector<std::string> names;
    if (list_addr == 0)
    {
        return names;
    }
    constexpr size_t kMaxListEntries = 64u;
    for (size_t i = 0; i < kMaxListEntries; ++i)
    {
        const uint32_t entry_addr = list_addr + static_cast<uint32_t>(i * 20u);
        if (!IsValidRange(entry_addr, 20u))
        {
            break;
        }
        const uint8_t first = ReadU8(entry_addr);
        if (first < 0x20u)
        {
            break;
        }
        std::string entry = ReadFixedString(entry_addr, 20u);
        if (!IsSafeNameComponent(entry, false))
        {
            break;
        }
        names.push_back(std::move(entry));
    }
    return names;
}

std::string ResolveTargetSaveName(uint32_t param_addr,
                                  uint32_t mode,
                                  const std::filesystem::path& save_root,
                                  const std::string& game_name)
{
    std::string save_name = ReadFixedString(param_addr + kSaveOffSaveName, 20u);
    if (save_name == "<>")
    {
        save_name.clear();
    }

    const bool is_list_mode = (mode == kSavedataModeListLoad || mode == kSavedataModeListSave ||
                               mode == kSavedataModeListDelete || mode == kSavedataModeAllDelete);
    if (!save_name.empty() || !is_list_mode)
    {
        return save_name;
    }

    const uint32_t list_addr = ReadU32(param_addr + kSaveOffSaveNameList);
    const std::vector<std::string> listed = ReadSaveNameList(list_addr);
    for (const auto& candidate : listed)
    {
        std::error_code ec;
        if (mode == kSavedataModeListSave)
        {
            return candidate;
        }
        if (std::filesystem::exists(save_root / (game_name + candidate), ec))
        {
            return candidate;
        }
    }

    if (mode == kSavedataModeListLoad || mode == kSavedataModeListDelete || mode == kSavedataModeAllDelete)
    {
        std::error_code ec;
        if (std::filesystem::exists(save_root, ec) && std::filesystem::is_directory(save_root, ec))
        {
            std::string best;
            for (const auto& entry : std::filesystem::directory_iterator(save_root, ec))
            {
                if (!entry.is_directory(ec))
                {
                    continue;
                }
                const std::string dir_name = entry.path().filename().string();
                if (dir_name.rfind(game_name, 0) == 0)
                {
                    const std::string suffix = dir_name.substr(game_name.size());
                    if (best.empty() || suffix < best)
                    {
                        best = suffix;
                    }
                }
            }
            if (!best.empty())
            {
                return best;
            }
        }
    }

    return save_name;
}

bool WriteAuxiliaryFileIfPresent(uint32_t struct_addr, const std::filesystem::path& out_path)
{
    if (!IsValidRange(struct_addr, 16u))
    {
        return true;
    }
    const uint32_t buf_addr = ReadU32(struct_addr + 0x00u);
    const uint32_t buf_size = ReadU32(struct_addr + 0x04u);
    const uint32_t data_size = ReadU32(struct_addr + 0x08u);
    if (buf_addr == 0 || data_size == 0)
    {
        return true;
    }
    if (buf_size > 0 && data_size > buf_size)
    {
        return false;
    }
    if (!IsValidRange(buf_addr, data_size))
    {
        return false;
    }
    const uint8_t* ptr = runtime::GetGuestMemory().Translate(buf_addr);
    return AtomicWriteBinaryFile(out_path, ptr, data_size);
}

void ReadAuxiliaryFileIfPresent(uint32_t struct_addr, const std::filesystem::path& in_path)
{
    if (!IsValidRange(struct_addr, 16u))
    {
        return;
    }
    const uint32_t buf_addr = ReadU32(struct_addr + 0x00u);
    const uint32_t buf_size = ReadU32(struct_addr + 0x04u);
    if (buf_addr == 0 || buf_size == 0)
    {
        return;
    }
    std::vector<uint8_t> bytes;
    if (!ReadBinaryFile(in_path, bytes) || bytes.size() > buf_size)
    {
        return;
    }
    if (!IsValidRange(buf_addr, static_cast<uint32_t>(bytes.size())))
    {
        return;
    }
    if (!bytes.empty())
    {
        std::memcpy(runtime::GetGuestMemory().Translate(buf_addr), bytes.data(), bytes.size());
    }
    WriteU32(struct_addr + 0x08u, static_cast<uint32_t>(bytes.size()));
}

DialogAnswer InvokeDialogPolicy(DialogKind kind, const std::string& text, ChoiceKind choice)
{
    DialogPolicy policy_copy;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        policy_copy = g_state.policy;
    }
    if (policy_copy)
    {
        return policy_copy(kind, text, choice);
    }
    return (choice == ChoiceKind::kYesNo) ? DialogAnswer::kYes : DialogAnswer::kConfirm;
}

const char* DialogAnswerName(DialogAnswer answer)
{
    switch (answer)
    {
    case DialogAnswer::kConfirm:
        return "confirm";
    case DialogAnswer::kYes:
        return "yes";
    case DialogAnswer::kNo:
        return "no";
    case DialogAnswer::kCancel:
        return "cancel";
    }
    return "unknown";
}

const char* SavedataModeName(uint32_t mode)
{
    switch (mode)
    {
    case kSavedataModeAutoload:
        return "AUTOLOAD";
    case kSavedataModeAutosave:
        return "AUTOSAVE";
    case kSavedataModeLoad:
        return "LOAD";
    case kSavedataModeSave:
        return "SAVE";
    case kSavedataModeListLoad:
        return "LISTLOAD";
    case kSavedataModeListSave:
        return "LISTSAVE";
    case kSavedataModeListDelete:
        return "LISTDELETE";
    case kSavedataModeAllDelete:
        return "ALLDELETE";
    case kSavedataModeSizes:
        return "SIZES";
    default:
        return "UNKNOWN";
    }
}

bool ExecuteSavedataInit(uint32_t param_addr, int32_t& hle_ret_out, char (&stop_buf)[160])
{
    if (!IsValidRange(param_addr, kDialogCommonSize))
    {
        hle_ret_out = kSceUtilityErrorInvalidParam;
        return true;
    }

    const uint32_t param_size = ReadU32(param_addr + kCommonOffSize);
    if (param_size < kSavedataMinParamSize || param_size > kSavedataMaxParamSize ||
        !IsValidRange(param_addr, param_size))
    {
        hle_ret_out = kSceUtilityErrorInvalidParamSize;
        return true;
    }

    std::filesystem::path save_root;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.dialog_status != kDialogStatusNone)
        {
            hle_ret_out = kSceUtilityErrorInvalidStatus;
            return true;
        }
        save_root = ResolveSaveRoot(g_state.config);
    }

    const uint32_t mode = ReadU32(param_addr + kSaveOffMode);
    if (mode > kSavedataModeSizes)
    {
        std::snprintf(stop_buf, sizeof(stop_buf),
                      "Unimplemented sceUtilitySavedata mode %u", mode);
        return false;
    }

    const std::string game_name = ReadFixedString(param_addr + kSaveOffGameName, 13u);
    const std::string file_name = ReadFixedString(param_addr + kSaveOffFileName, 13u);
    const std::string save_name = ResolveTargetSaveName(param_addr, mode, save_root, game_name);

    if (!IsSafeNameComponent(game_name, false) ||
        !IsSafeNameComponent(save_name, true) ||
        !IsSafeNameComponent(file_name, mode == kSavedataModeSizes ||
                                            mode == kSavedataModeListDelete ||
                                            mode == kSavedataModeAllDelete))
    {
        WriteI32(param_addr + kCommonOffResult,
                 (mode == kSavedataModeAutosave || mode == kSavedataModeSave || mode == kSavedataModeListSave)
                     ? kSceUtilitySavedataErrorSaveParam
                     : kSceUtilitySavedataErrorLoadParam);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.active_type = ActiveDialogType::kSavedata;
        g_state.dialog_status = kDialogStatusInit;
        ++g_state.stats.savedata_init_calls;
        if (mode < g_state.stats.savedata_mode_counts.size())
        {
            ++g_state.stats.savedata_mode_counts[mode];
        }
        g_state.stats.last_savedata_mode = static_cast<int32_t>(mode);
        g_state.stats.last_savedata_result = ReadI32(param_addr + kCommonOffResult);
        hle_ret_out = kSceKernelErrorOk;
        return true;
    }

    const std::string dir_name = game_name + save_name;
    const std::filesystem::path slot_dir = save_root / dir_name;
    const std::filesystem::path data_file_path = slot_dir / file_name;
    const std::filesystem::path sfo_path = slot_dir / "PARAM.SFO";

    int32_t result_code = kSceKernelErrorOk;
    uint64_t bytes_read = 0u;
    uint64_t bytes_written = 0u;

    if (mode == kSavedataModeAutoload || mode == kSavedataModeLoad || mode == kSavedataModeListLoad)
    {
        std::error_code ec;
        if (file_name.empty() || !std::filesystem::exists(data_file_path, ec) ||
            !std::filesystem::is_regular_file(data_file_path, ec))
        {
            result_code = kSceUtilitySavedataErrorLoadNoData;
        }
        else
        {
            std::vector<uint8_t> data;
            if (!ReadBinaryFile(data_file_path, data))
            {
                result_code = kSceUtilitySavedataErrorLoadEio;
            }
            else
            {
                const uint32_t data_buf_addr = ReadU32(param_addr + kSaveOffDataBuf);
                const uint32_t data_buf_size = ReadU32(param_addr + kSaveOffDataBufSize);
                if (data_buf_addr == 0 || data.size() > data_buf_size ||
                    (data.size() > 0 && !IsValidRange(data_buf_addr, static_cast<uint32_t>(data.size()))))
                {
                    result_code = kSceUtilitySavedataErrorLoadParam;
                }
                else
                {
                    if (!data.empty())
                    {
                        std::memcpy(runtime::GetGuestMemory().Translate(data_buf_addr),
                                    data.data(), data.size());
                    }
                    WriteU32(param_addr + kSaveOffDataSize, static_cast<uint32_t>(data.size()));
                    bytes_read = data.size();

                    std::vector<uint8_t> sfo_bytes;
                    if (ReadBinaryFile(sfo_path, sfo_bytes))
                    {
                        auto parsed_sfo = module::ParamSfo::Parse(sfo_bytes.data(), sfo_bytes.size());
                        if (parsed_sfo.has_value())
                        {
                            if (auto v = parsed_sfo->GetString("TITLE"))
                            {
                                WriteFixedString(param_addr + kSaveOffSfoTitle, 128u, *v);
                            }
                            if (auto v = parsed_sfo->GetString("SAVEDATA_TITLE"))
                            {
                                WriteFixedString(param_addr + kSaveOffSfoSavedataTitle, 128u, *v);
                            }
                            if (auto v = parsed_sfo->GetString("SAVEDATA_DETAIL"))
                            {
                                WriteFixedString(param_addr + kSaveOffSfoDetail, 1024u, *v);
                            }
                            if (auto v = parsed_sfo->GetInteger("PARENTAL_LEVEL"))
                            {
                                WriteU8(param_addr + kSaveOffSfoParentalLevel,
                                        static_cast<uint8_t>(*v & 0xFFu));
                            }
                        }
                    }

                    ReadAuxiliaryFileIfPresent(param_addr + kSaveOffIcon0, slot_dir / "ICON0.PNG");
                    ReadAuxiliaryFileIfPresent(param_addr + kSaveOffIcon1, slot_dir / "ICON1.PMF");
                    ReadAuxiliaryFileIfPresent(param_addr + kSaveOffPic1, slot_dir / "PIC1.PNG");
                    ReadAuxiliaryFileIfPresent(param_addr + kSaveOffSnd0, slot_dir / "SND0.AT3");
                    result_code = kSceKernelErrorOk;
                }
            }
        }
        PopulateSavedataSizeStructs(param_addr, param_size, game_name, save_name, slot_dir, bytes_read);
    }
    else if (mode == kSavedataModeAutosave || mode == kSavedataModeSave || mode == kSavedataModeListSave)
    {
        const uint32_t data_buf_addr = ReadU32(param_addr + kSaveOffDataBuf);
        const uint32_t data_buf_size = ReadU32(param_addr + kSaveOffDataBufSize);
        const uint32_t data_size = ReadU32(param_addr + kSaveOffDataSize);
        const uint32_t overwrite = ReadU32(param_addr + kSaveOffOverwrite);

        std::error_code ec;
        const bool already_exists = std::filesystem::exists(slot_dir, ec);
        bool allow_save = true;
        if (mode == kSavedataModeListSave && already_exists && overwrite == 0)
        {
            const DialogAnswer ans = InvokeDialogPolicy(
                DialogKind::kSavedata,
                std::string("Overwrite savedata ") + dir_name + "?",
                ChoiceKind::kYesNo);
            allow_save = (ans == DialogAnswer::kYes || ans == DialogAnswer::kConfirm);
            if (!allow_save)
            {
                result_code = kSceKernelErrorOk;
            }
        }

        if (allow_save)
        {
            if (file_name.empty() || data_buf_addr == 0 || data_size == 0 ||
                (data_buf_size > 0 && data_size > data_buf_size) ||
                !IsValidRange(data_buf_addr, data_size))
            {
                result_code = kSceUtilitySavedataErrorSaveParam;
            }
            else
            {
                const uint8_t* src_ptr = runtime::GetGuestMemory().Translate(data_buf_addr);
                if (!AtomicWriteBinaryFile(data_file_path, src_ptr, data_size))
                {
                    result_code = kSceUtilitySavedataErrorSaveAccessError;
                }
                else
                {
                    bytes_written = data_size;

                    const std::string title = ReadFixedString(param_addr + kSaveOffSfoTitle, 128u);
                    const std::string savedata_title =
                        ReadFixedString(param_addr + kSaveOffSfoSavedataTitle, 128u);
                    const std::string detail = ReadFixedString(param_addr + kSaveOffSfoDetail, 1024u);
                    const uint8_t parental = ReadU8(param_addr + kSaveOffSfoParentalLevel);

                    std::vector<uint8_t> sfo_bytes;
                    BuildParamSfo(dir_name, file_name, title, savedata_title, detail, parental, sfo_bytes);
                    if (!AtomicWriteBinaryFile(sfo_path, sfo_bytes.data(), sfo_bytes.size()))
                    {
                        result_code = kSceUtilitySavedataErrorSaveAccessError;
                    }
                    else
                    {
                        if (!WriteAuxiliaryFileIfPresent(param_addr + kSaveOffIcon0, slot_dir / "ICON0.PNG") ||
                            !WriteAuxiliaryFileIfPresent(param_addr + kSaveOffIcon1, slot_dir / "ICON1.PMF") ||
                            !WriteAuxiliaryFileIfPresent(param_addr + kSaveOffPic1, slot_dir / "PIC1.PNG") ||
                            !WriteAuxiliaryFileIfPresent(param_addr + kSaveOffSnd0, slot_dir / "SND0.AT3"))
                        {
                            result_code = kSceUtilitySavedataErrorSaveAccessError;
                        }
                        else
                        {
                            result_code = kSceKernelErrorOk;
                        }
                    }
                }
            }
        }
        PopulateSavedataSizeStructs(param_addr, param_size, game_name, save_name, slot_dir, data_size);
    }
    else if (mode == kSavedataModeListDelete || mode == kSavedataModeAllDelete)
    {
        std::error_code ec;
        if (!std::filesystem::exists(slot_dir, ec))
        {
            result_code = kSceUtilitySavedataErrorDeleteNoData;
        }
        else
        {
            const DialogAnswer ans = InvokeDialogPolicy(
                DialogKind::kSavedata,
                std::string("Delete savedata ") + dir_name + "?",
                ChoiceKind::kYesNo);
            if (ans == DialogAnswer::kYes || ans == DialogAnswer::kConfirm)
            {
                std::filesystem::remove_all(slot_dir, ec);
                result_code = ec ? kSceUtilitySavedataErrorSaveAccessError : kSceKernelErrorOk;
            }
            else
            {
                result_code = kSceKernelErrorOk;
            }
        }
        PopulateSavedataSizeStructs(param_addr, param_size, game_name, save_name, slot_dir, 0u);
    }
    else if (mode == kSavedataModeSizes)
    {
        const uint32_t data_size = ReadU32(param_addr + kSaveOffDataSize);
        PopulateSavedataSizeStructs(param_addr, param_size, game_name, save_name, slot_dir, data_size);
        result_code = kSceKernelErrorOk;
    }

    WriteI32(param_addr + kCommonOffResult, result_code);

    runtime::Log(runtime::LogLevel::kInfo,
                 "[sceUtility] SavedataInitStart(mode=%u %s, dir=%s, file=%s) -> result=0x%08X (read=%llu, written=%llu)",
                 mode, SavedataModeName(mode), dir_name.c_str(), file_name.c_str(),
                 static_cast<uint32_t>(result_code),
                 static_cast<unsigned long long>(bytes_read),
                 static_cast<unsigned long long>(bytes_written));

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.active_type = ActiveDialogType::kSavedata;
        g_state.dialog_status = kDialogStatusInit;
        ++g_state.stats.savedata_init_calls;
        if (mode < g_state.stats.savedata_mode_counts.size())
        {
            ++g_state.stats.savedata_mode_counts[mode];
        }
        g_state.stats.last_savedata_mode = static_cast<int32_t>(mode);
        g_state.stats.last_savedata_result = result_code;
        g_state.stats.savedata_bytes_read += bytes_read;
        g_state.stats.savedata_bytes_written += bytes_written;
    }

    hle_ret_out = kSceKernelErrorOk;
    return true;
}

void HleUtilitySavedataInitStart()
{
    const uint32_t param_addr = recomp::Arg<uint32_t>(0);
    int32_t ret = kSceKernelErrorOk;
    char stop_buf[160] = {};
    if (!ExecuteSavedataInit(param_addr, ret, stop_buf))
    {
        platform::RaiseStop(stop_buf);
    }
    recomp::Return(static_cast<uint32_t>(ret));
}

void HleUtilitySavedataGetStatus()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kSavedata)
    {
        recomp::Return(static_cast<uint32_t>(kDialogStatusNone));
        return;
    }
    const int32_t current = g_state.dialog_status;
    if (current == kDialogStatusInit)
    {
        g_state.dialog_status = kDialogStatusVisible;
    }
    else if (current == kDialogStatusFinished)
    {
        g_state.dialog_status = kDialogStatusNone;
        g_state.active_type = ActiveDialogType::kNone;
    }
    recomp::Return(static_cast<uint32_t>(current));
}

void HleUtilitySavedataUpdate()
{
    (void)recomp::Arg<int32_t>(0);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kSavedata)
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidStatus));
        return;
    }
    if (g_state.dialog_status == kDialogStatusVisible)
    {
        g_state.dialog_status = kDialogStatusQuit;
    }
    recomp::Return(static_cast<uint32_t>(kSceKernelErrorOk));
}

void HleUtilitySavedataShutdownStart()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kSavedata)
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidStatus));
        return;
    }
    if (g_state.dialog_status == kDialogStatusVisible || g_state.dialog_status == kDialogStatusQuit)
    {
        g_state.dialog_status = kDialogStatusFinished;
    }
    recomp::Return(static_cast<uint32_t>(kSceKernelErrorOk));
}

void HleUtilityMsgDialogInitStart()
{
    const uint32_t param_addr = recomp::Arg<uint32_t>(0);
    if (!IsValidRange(param_addr, kDialogCommonSize))
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidParam));
        return;
    }

    const uint32_t param_size = ReadU32(param_addr + kCommonOffSize);
    if (param_size < kMsgDialogMinParamSize || param_size > kMsgDialogMaxParamSize ||
        !IsValidRange(param_addr, param_size))
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidParamSize));
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_state.dialog_status != kDialogStatusNone)
        {
            recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidStatus));
            return;
        }
    }

    const int32_t mode = ReadI32(param_addr + kMsgOffMode);
    const uint32_t error_value = ReadU32(param_addr + kMsgOffErrorValue);
    const uint32_t options = ReadU32(param_addr + kMsgOffOptions);
    std::string text = ReadFixedString(param_addr + kMsgOffMessage, 512u);
    if (mode == kMsgDialogModeError && text.empty())
    {
        char err_buf[48];
        std::snprintf(err_buf, sizeof(err_buf), "Error 0x%08X", error_value);
        text = err_buf;
    }

    const ChoiceKind choice = ((options & kMsgDialogOptionYesNoButtons) != 0u)
                                  ? ChoiceKind::kYesNo
                                  : ChoiceKind::kNone;
    const DialogAnswer answer = InvokeDialogPolicy(DialogKind::kMsgDialog, text, choice);

    int32_t button_pressed = kMsgDialogButtonInvalid;
    switch (answer)
    {
    case DialogAnswer::kConfirm:
    case DialogAnswer::kYes:
        button_pressed = kMsgDialogButtonYes;
        break;
    case DialogAnswer::kNo:
        button_pressed = kMsgDialogButtonNo;
        break;
    case DialogAnswer::kCancel:
        button_pressed = kMsgDialogButtonBack;
        break;
    }

    WriteI32(param_addr + kCommonOffResult, kSceKernelErrorOk);
    WriteI32(param_addr + kMsgOffUnknownResult, kSceKernelErrorOk);
    WriteI32(param_addr + kMsgOffButtonPressed, button_pressed);

    std::string sanitized_text = text;
    for (char& c : sanitized_text)
    {
        if (c == '\n' || c == '\r')
        {
            c = ' ';
        }
    }
    runtime::Log(runtime::LogLevel::kInfo,
                 "[sceUtility] MsgDialogInitStart(mode=%d, choice=%s, error=0x%08X, answer=%s, button=%d, text=\"%s\") [dialog UI: not drawn]",
                 mode,
                 (choice == ChoiceKind::kYesNo) ? "yesno" : "none",
                 error_value,
                 DialogAnswerName(answer),
                 button_pressed,
                 sanitized_text.c_str());

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state.active_type = ActiveDialogType::kMsgDialog;
        g_state.dialog_status = kDialogStatusInit;
        ++g_state.stats.msgdialog_init_calls;

        DialogRecord rec;
        rec.kind = DialogKind::kMsgDialog;
        rec.mode = static_cast<uint32_t>(mode);
        rec.error_value = error_value;
        rec.text = std::move(sanitized_text);
        rec.choice = choice;
        rec.answer = answer;
        rec.button_pressed = button_pressed;
        g_state.stats.dialog_records.push_back(std::move(rec));
    }

    recomp::Return(static_cast<uint32_t>(kSceKernelErrorOk));
}

void HleUtilityMsgDialogGetStatus()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kMsgDialog)
    {
        recomp::Return(static_cast<uint32_t>(kDialogStatusNone));
        return;
    }
    const int32_t current = g_state.dialog_status;
    if (current == kDialogStatusInit)
    {
        g_state.dialog_status = kDialogStatusVisible;
    }
    else if (current == kDialogStatusFinished)
    {
        g_state.dialog_status = kDialogStatusNone;
        g_state.active_type = ActiveDialogType::kNone;
    }
    recomp::Return(static_cast<uint32_t>(current));
}

void HleUtilityMsgDialogUpdate()
{
    (void)recomp::Arg<int32_t>(0);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kMsgDialog)
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidStatus));
        return;
    }
    if (g_state.dialog_status == kDialogStatusVisible)
    {
        g_state.dialog_status = kDialogStatusQuit;
    }
    recomp::Return(static_cast<uint32_t>(kSceKernelErrorOk));
}

void HleUtilityMsgDialogShutdownStart()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.active_type != ActiveDialogType::kMsgDialog)
    {
        recomp::Return(static_cast<uint32_t>(kSceUtilityErrorInvalidStatus));
        return;
    }
    if (g_state.dialog_status == kDialogStatusVisible || g_state.dialog_status == kDialogStatusQuit)
    {
        g_state.dialog_status = kDialogStatusFinished;
    }
    recomp::Return(static_cast<uint32_t>(kSceKernelErrorOk));
}

} // namespace

void Register(const Config& config)
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        DialogPolicy saved_policy = std::move(g_state.policy);
        g_state = UtilityState{};
        g_state.config = config;
        g_state.registered = true;
        g_state.policy = std::move(saved_policy);
    }

    auto& reg = runtime::GetNidRegistry();
    reg.Register("sceUtility", 0x50C4CD57u, "sceUtilitySavedataInitStart", HleUtilitySavedataInitStart);
    reg.Register("sceUtility", 0x8874DBE0u, "sceUtilitySavedataGetStatus", HleUtilitySavedataGetStatus);
    reg.Register("sceUtility", 0xD4B95FFBu, "sceUtilitySavedataUpdate", HleUtilitySavedataUpdate);
    reg.Register("sceUtility", 0x9790B33Cu, "sceUtilitySavedataShutdownStart", HleUtilitySavedataShutdownStart);

    reg.Register("sceUtility", 0x2AD8E239u, "sceUtilityMsgDialogInitStart", HleUtilityMsgDialogInitStart);
    reg.Register("sceUtility", 0x9A1C91D7u, "sceUtilityMsgDialogGetStatus", HleUtilityMsgDialogGetStatus);
    reg.Register("sceUtility", 0x95FC253Bu, "sceUtilityMsgDialogUpdate", HleUtilityMsgDialogUpdate);
    reg.Register("sceUtility", 0x67AF3428u, "sceUtilityMsgDialogShutdownStart", HleUtilityMsgDialogShutdownStart);
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.active_type = ActiveDialogType::kNone;
    g_state.dialog_status = kDialogStatusNone;
    g_state.registered = false;
}

void SetDialogPolicy(DialogPolicy policy)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.policy = std::move(policy);
}

Stats GetStats()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state.stats;
}

std::string FormatStats()
{
    const Stats s = GetStats();
    std::ostringstream oss;
    char res_hex[16];
    std::snprintf(res_hex, sizeof(res_hex), "0x%08X", static_cast<uint32_t>(s.last_savedata_result));

    oss << "Utility:     dialog UI: not drawn, savedata_init=" << s.savedata_init_calls
        << ", msgdialog_init=" << s.msgdialog_init_calls
        << ", read=" << s.savedata_bytes_read << " B"
        << ", written=" << s.savedata_bytes_written << " B"
        << ", last_mode=" << s.last_savedata_mode
        << ", last_result=" << res_hex;

    bool first_mode = true;
    for (uint32_t m = 0; m < s.savedata_mode_counts.size(); ++m)
    {
        if (s.savedata_mode_counts[m] > 0)
        {
            oss << (first_mode ? ", modes=[" : ", ")
                << SavedataModeName(m) << "(" << m << ")=" << s.savedata_mode_counts[m];
            first_mode = false;
        }
    }
    if (!first_mode)
    {
        oss << "]";
    }

    for (size_t i = 0; i < s.dialog_records.size(); ++i)
    {
        const auto& rec = s.dialog_records[i];
        char err_hex[16];
        std::snprintf(err_hex, sizeof(err_hex), "0x%08X", rec.error_value);
        oss << "\n  Dialog[" << i << "]: kind="
            << ((rec.kind == DialogKind::kMsgDialog) ? "MsgDialog" : "Savedata")
            << ", mode=" << rec.mode
            << ", err=" << err_hex
            << ", choice=" << ((rec.choice == ChoiceKind::kYesNo) ? "yesno" : "none")
            << ", answer=" << DialogAnswerName(rec.answer)
            << ", btn=" << rec.button_pressed
            << ", text=\"" << rec.text << "\"";
    }
    return oss.str();
}

} // namespace repsp::hle::utility
