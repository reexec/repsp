#ifndef REPSP_HLE_UTILITY_H
#define REPSP_HLE_UTILITY_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace repsp::hle::utility
{

enum class DialogKind : uint32_t
{
    kMsgDialog = 0,
    kSavedata = 1,
};

enum class ChoiceKind : uint32_t
{
    kNone = 0,
    kYesNo = 1,
};

enum class DialogAnswer : uint32_t
{
    kConfirm = 0,
    kYes = 1,
    kNo = 2,
    kCancel = 3,
};

using DialogPolicy = std::function<DialogAnswer(DialogKind kind,
                                                const std::string& text,
                                                ChoiceKind choice)>;

struct Config
{
    std::filesystem::path save_root;
    int32_t language = 9;    // PSP_SYSTEMPARAM_LANGUAGE_KOREAN
    int32_t button_swap = 0; // PSP_UTILITY_ACCEPT_CIRCLE
};

struct DialogRecord
{
    DialogKind kind = DialogKind::kMsgDialog;
    uint32_t mode = 0;
    uint32_t error_value = 0;
    std::string text;
    ChoiceKind choice = ChoiceKind::kNone;
    DialogAnswer answer = DialogAnswer::kConfirm;
    int32_t button_pressed = 0;
};

struct Stats
{
    uint64_t savedata_init_calls = 0;
    uint64_t msgdialog_init_calls = 0;
    std::array<uint64_t, 24> savedata_mode_counts{};
    int32_t last_savedata_mode = -1;
    int32_t last_savedata_result = 0;
    uint64_t savedata_bytes_read = 0;
    uint64_t savedata_bytes_written = 0;
    std::vector<DialogRecord> dialog_records;
};

// Register the 8 sceUtility (Savedata & MsgDialog) NIDs.
void Register(const Config& config = {});
void Shutdown();

// Install a host policy for answering undrawn utility dialogs (nullptr restores default Confirm/Yes).
void SetDialogPolicy(DialogPolicy policy);

Stats GetStats();
std::string FormatStats();

} // namespace repsp::hle::utility

#endif // REPSP_HLE_UTILITY_H
