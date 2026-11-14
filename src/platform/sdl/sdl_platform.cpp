#include "repsp/platform/platform.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace repsp::platform
{

namespace
{

struct InputMappingEntry
{
    uint32_t psp_bit;
    SDL_Scancode scancode;
    SDL_GamepadButton gamepad_button;
};

// Single unified keyboard/gamepad -> PSP button mapping table.
constexpr InputMappingEntry kButtonMappings[] = {
    {kPspCtrlUp, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {kPspCtrlRight, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {kPspCtrlDown, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {kPspCtrlLeft, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {kPspCtrlCross, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
    {kPspCtrlCircle, SDL_SCANCODE_X, SDL_GAMEPAD_BUTTON_EAST},
    {kPspCtrlSquare, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_WEST},
    {kPspCtrlTriangle, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_NORTH},
    {kPspCtrlLTrigger, SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {kPspCtrlRTrigger, SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {kPspCtrlStart, SDL_SCANCODE_RETURN, SDL_GAMEPAD_BUTTON_START},
    {kPspCtrlSelect, SDL_SCANCODE_BACKSPACE, SDL_GAMEPAD_BUTTON_BACK},
};

std::mutex g_input_mutex;
ControllerState g_input_snapshot;
bool g_input_dummy = true;

float NormalizeSdlAxis(Sint16 raw)
{
    return (raw >= 0)
        ? (static_cast<float>(raw) / 32767.0f)
        : (static_cast<float>(raw) / 32768.0f);
}

void ResetInputSnapshot(bool dummy)
{
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_input_snapshot = ControllerState{};
    g_input_dummy = dummy;
}

void UpdateInputSnapshotFromSdl(SDL_Gamepad* gamepad)
{
    int num_keys = 0;
    const bool* keys = SDL_GetKeyboardState(&num_keys);

    ControllerState next;
    for (const auto& entry : kButtonMappings)
    {
        bool pressed = (keys && entry.scancode < num_keys && keys[entry.scancode]);
        if (!pressed && gamepad && SDL_GetGamepadButton(gamepad, entry.gamepad_button))
        {
            pressed = true;
        }
        if (pressed)
        {
            next.buttons |= entry.psp_bit;
        }
    }

    float lx = gamepad ? NormalizeSdlAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) : 0.0f;
    float ly = gamepad ? NormalizeSdlAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) : 0.0f;
    if (keys)
    {
        if (SDL_SCANCODE_J < num_keys && keys[SDL_SCANCODE_J]) lx -= 1.0f;
        if (SDL_SCANCODE_L < num_keys && keys[SDL_SCANCODE_L]) lx += 1.0f;
        if (SDL_SCANCODE_I < num_keys && keys[SDL_SCANCODE_I]) ly -= 1.0f;
        if (SDL_SCANCODE_K < num_keys && keys[SDL_SCANCODE_K]) ly += 1.0f;
    }
    next.lx = std::clamp(lx, -1.0f, 1.0f);
    next.ly = std::clamp(ly, -1.0f, 1.0f);

    std::lock_guard<std::mutex> lock(g_input_mutex);
    if (!g_input_dummy)
    {
        g_input_snapshot = next;
    }
}

SDL_Gamepad* OpenFirstGamepad()
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    SDL_Gamepad* pad = nullptr;
    if (ids && count > 0)
    {
        pad = SDL_OpenGamepad(ids[0]);
    }
    if (ids)
    {
        SDL_free(ids);
    }
    return pad;
}

class SdlDisplayBackend final : public DisplayBackend
{
public:
    SdlDisplayBackend() = default;
    ~SdlDisplayBackend() override
    {
        Shutdown();
    }

    bool Initialize(const DisplayConfig& config) override
    {
        Shutdown();
        config_ = config;
        close_requested_.store(false);

        if (config.dummy)
        {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            {
                return false;
            }
            window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, 0);
            initialized_ = true;
            return true;
        }

        // Run SDL video/window/event pump on a dedicated thread so the OS message
        // loop remains responsive while the main thread blocks in Scheduler::Run().
        ui_ready_ = false;
        ui_init_ok_ = false;
        ui_stop_.store(false);
        ui_thread_ = std::thread([this]()
        {
            UiThreadMain();
        });

        std::unique_lock<std::mutex> lock(ui_mutex_);
        ui_cv_.wait(lock, [this]() { return ui_ready_; });
        if (!ui_init_ok_)
        {
            lock.unlock();
            if (ui_thread_.joinable())
            {
                ui_thread_.join();
            }
            return false;
        }
        initialized_ = true;
        return true;
    }

    void Shutdown() override
    {
        if (!initialized_)
        {
            return;
        }
        initialized_ = false;

        if (config_.dummy)
        {
            if (window_ != nullptr)
            {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }

        ui_stop_.store(true);
        ui_cv_.notify_all();
        if (ui_thread_.joinable())
        {
            ui_thread_.join();
        }
    }

    void PresentRgba8(const uint8_t* pixels, int width, int height, int pitch_bytes) override
    {
        if (config_.dummy || pixels == nullptr || width <= 0 || height <= 0 || pitch_bytes <= 0)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(ui_mutex_);
        const size_t dst_pitch = static_cast<size_t>(width) * 4u;
        frame_buf_.resize(dst_pitch * static_cast<size_t>(height));
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(frame_buf_.data() + static_cast<size_t>(y) * dst_pitch,
                        pixels + static_cast<size_t>(y) * static_cast<size_t>(pitch_bytes),
                        dst_pitch);
        }
        frame_w_ = width;
        frame_h_ = height;
        frame_dirty_ = true;
        ui_cv_.notify_all();
    }

    void SwapBuffers() override
    {
    }

    bool PollEvents() override
    {
        if (config_.dummy)
        {
            return true;
        }
        return !close_requested_.load();
    }

private:
    void UiThreadMain()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = false;
            ui_ready_ = true;
            ui_cv_.notify_all();
            return;
        }

        SDL_WindowFlags flags = 0;
        if (config_.fullscreen)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        SDL_Window* win = SDL_CreateWindow(config_.title.c_str(), config_.width, config_.height, flags);
        if (win == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = false;
            ui_ready_ = true;
            ui_cv_.notify_all();
            return;
        }

        SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
        SDL_Texture* tex = nullptr;
        int tex_w = 0;
        int tex_h = 0;

        bool gamepad_init = SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        SDL_Gamepad* gamepad = gamepad_init ? OpenFirstGamepad() : nullptr;

        {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            ui_init_ok_ = true;
            ui_ready_ = true;
            ui_cv_.notify_all();
        }

        std::vector<uint8_t> local_buf;
        while (!ui_stop_.load())
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    close_requested_.store(true);
                }
                else if (event.type == SDL_EVENT_GAMEPAD_ADDED)
                {
                    if (gamepad == nullptr)
                    {
                        gamepad = SDL_OpenGamepad(event.gdevice.which);
                    }
                }
                else if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
                {
                    if (gamepad != nullptr && SDL_GetGamepadID(gamepad) == event.gdevice.which)
                    {
                        SDL_CloseGamepad(gamepad);
                        gamepad = OpenFirstGamepad();
                    }
                }
            }
            UpdateInputSnapshotFromSdl(gamepad);

            int w = 0;
            int h = 0;
            bool has_frame = false;
            {
                std::unique_lock<std::mutex> lock(ui_mutex_);
                if (!frame_dirty_ && !ui_stop_.load())
                {
                    ui_cv_.wait_for(lock, std::chrono::milliseconds(8));
                }
                if (frame_dirty_)
                {
                    local_buf = frame_buf_;
                    w = frame_w_;
                    h = frame_h_;
                    frame_dirty_ = false;
                    has_frame = true;
                }
            }

            if (has_frame && ren != nullptr && w > 0 && h > 0)
            {
                if (tex == nullptr || tex_w != w || tex_h != h)
                {
                    if (tex != nullptr)
                    {
                        SDL_DestroyTexture(tex);
                    }
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
                    if (tex != nullptr)
                    {
                        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                    }
                    tex_w = w;
                    tex_h = h;
                }
                if (tex != nullptr)
                {
                    SDL_UpdateTexture(tex, nullptr, local_buf.data(), w * 4);
                    SDL_RenderClear(ren);
                    SDL_RenderTexture(ren, tex, nullptr, nullptr);
                    SDL_RenderPresent(ren);
                }
            }
        }

        if (gamepad != nullptr)
        {
            SDL_CloseGamepad(gamepad);
        }
        if (gamepad_init)
        {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
        }
        ResetInputSnapshot(true);

        if (tex != nullptr) SDL_DestroyTexture(tex);
        if (ren != nullptr) SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    DisplayConfig config_;
    bool initialized_ = false;
    SDL_Window* window_ = nullptr;
    std::atomic<bool> close_requested_{false};
    std::atomic<bool> ui_stop_{false};
    std::thread ui_thread_;
    std::mutex ui_mutex_;
    std::condition_variable ui_cv_;
    bool ui_ready_ = false;
    bool ui_init_ok_ = false;
    std::vector<uint8_t> frame_buf_;
    int frame_w_ = 0;
    int frame_h_ = 0;
    bool frame_dirty_ = false;
};

class SdlInputBackend final : public InputBackend
{
public:
    bool Initialize() override
    {
        // SDL gamepad/event polling runs exclusively on SdlDisplayBackend's UI thread.
        return true;
    }

    void Shutdown() override
    {
    }

    void Poll(ControllerState& state) override
    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        state = g_input_dummy ? ControllerState{} : g_input_snapshot;
    }
};

class SdlAudioBackend final : public AudioBackend
{
public:
    SdlAudioBackend() = default;
    ~SdlAudioBackend() override
    {
        Shutdown();
    }

    bool Initialize(const AudioConfig& config) override
    {
        Shutdown();

        config_ = config;
        if (config_.sample_rate <= 0)
        {
            config_.sample_rate = 44100;
        }
        if (config_.channels <= 0)
        {
            config_.channels = 2;
        }
        if (config_.buffer_frames <= 0)
        {
            config_.buffer_frames = 256;
        }

        if (config_.dummy)
        {
            dummy_stop_ = false;
            dummy_reset_clock_ = true;
            dummy_thread_ = std::thread(&SdlAudioBackend::DummyThreadMain, this);
            initialized_ = true;
            return true;
        }

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return false;
        }
        audio_subsys_init_ = true;

        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = config_.channels;
        spec.freq = config_.sample_rate;

        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec,
            &SdlAudioBackend::SdlStreamGetCallback,
            this);
        if (stream_ == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_subsys_init_ = false;
            return false;
        }

        SDL_ResumeAudioStreamDevice(stream_);
        initialized_ = true;
        return true;
    }

    void Shutdown() override
    {
        if (stream_ != nullptr)
        {
            // Destroy stream first so SDL stops invoking SdlStreamGetCallback before callback state clears.
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(pull_mutex_);
            dummy_stop_ = true;
            pull_callback_ = nullptr;
        }
        dummy_cv_.notify_all();

        if (dummy_thread_.joinable())
        {
            dummy_thread_.join();
        }

        if (audio_subsys_init_)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audio_subsys_init_ = false;
        }
        initialized_ = false;
    }

    void SetPullCallback(AudioPullCallback callback) override
    {
        {
            std::lock_guard<std::mutex> lock(pull_mutex_);
            pull_callback_ = std::move(callback);
            dummy_reset_clock_ = true;
        }
        dummy_cv_.notify_all();
    }

    bool QueueAudio(const int16_t* samples, size_t frame_count) override
    {
        if (stream_ == nullptr)
        {
            return config_.dummy;
        }
        int byte_count = static_cast<int>(frame_count * static_cast<size_t>(config_.channels) * sizeof(int16_t));
        return SDL_PutAudioStreamData(stream_, samples, byte_count);
    }

private:
    static void SDLCALL SdlStreamGetCallback(
        void* userdata,
        SDL_AudioStream* stream,
        int additional_amount,
        int /*total_amount*/)
    {
        auto* self = static_cast<SdlAudioBackend*>(userdata);
        if (self == nullptr || stream == nullptr || additional_amount <= 0)
        {
            return;
        }

        AudioPullCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->pull_mutex_);
            cb = self->pull_callback_;
        }
        if (!cb)
        {
            // Allow legacy QueueAudio push mode without zero injection.
            return;
        }

        const size_t channels = static_cast<size_t>(std::max<int32_t>(1, self->config_.channels));
        const size_t frame_bytes = channels * sizeof(int16_t);
        const size_t frames_needed = (static_cast<size_t>(additional_amount) + frame_bytes - 1) / frame_bytes;
        if (frames_needed == 0)
        {
            return;
        }

        self->sdl_scratch_.assign(frames_needed * channels, 0);
        cb(self->sdl_scratch_.data(), frames_needed);
        SDL_PutAudioStreamData(
            stream,
            self->sdl_scratch_.data(),
            static_cast<int>(frames_needed * frame_bytes));
    }

    void DummyThreadMain()
    {
        const uint64_t rate = static_cast<uint64_t>(std::max<int32_t>(1, config_.sample_rate));
        const size_t channels = static_cast<size_t>(std::max<int32_t>(1, config_.channels));
        const size_t period_frames = static_cast<size_t>(std::max<int32_t>(64, config_.buffer_frames));
        std::vector<int16_t> scratch(period_frames * channels, 0);

        auto base_time = std::chrono::steady_clock::now();
        uint64_t pulled_frames = 0;

        for (;;)
        {
            AudioPullCallback cb;
            {
                std::unique_lock<std::mutex> lock(pull_mutex_);
                dummy_cv_.wait(lock, [this]()
                {
                    return dummy_stop_ || static_cast<bool>(pull_callback_);
                });
                if (dummy_stop_)
                {
                    break;
                }
                if (dummy_reset_clock_)
                {
                    base_time = std::chrono::steady_clock::now();
                    pulled_frames = 0;
                    dummy_reset_clock_ = false;
                }
                cb = pull_callback_;
            }

            if (!cb)
            {
                continue;
            }

            const uint64_t next_target = pulled_frames + period_frames;
            const auto next_wakeup =
                base_time + std::chrono::nanoseconds((next_target * 1000000000ull) / rate);

            {
                std::unique_lock<std::mutex> lock(pull_mutex_);
                dummy_cv_.wait_until(lock, next_wakeup, [this]()
                {
                    return dummy_stop_ || !pull_callback_ || dummy_reset_clock_;
                });
                if (dummy_stop_)
                {
                    break;
                }
                if (!pull_callback_ || dummy_reset_clock_)
                {
                    continue;
                }
                cb = pull_callback_;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now <= base_time)
            {
                continue;
            }

            const uint64_t elapsed_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - base_time).count());
            const uint64_t due_total = (elapsed_ns * rate) / 1000000000ull;
            if (due_total <= pulled_frames)
            {
                continue;
            }

            uint64_t frames_due = due_total - pulled_frames;
            const uint64_t max_burst = rate / 5ull; // cap burst to 200 ms on OS stall
            if (frames_due > max_burst)
            {
                pulled_frames = due_total - max_burst;
                frames_due = max_burst;
            }

            while (frames_due >= period_frames)
            {
                {
                    std::lock_guard<std::mutex> lock(pull_mutex_);
                    if (dummy_stop_ || !pull_callback_ || dummy_reset_clock_)
                    {
                        break;
                    }
                    cb = pull_callback_;
                }
                std::fill(scratch.begin(), scratch.end(), int16_t{0});
                cb(scratch.data(), period_frames);
                pulled_frames += period_frames;
                frames_due -= period_frames;
            }
        }
    }

    AudioConfig config_;
    bool initialized_ = false;
    bool audio_subsys_init_ = false;
    SDL_AudioStream* stream_ = nullptr;
    std::mutex pull_mutex_;
    std::condition_variable dummy_cv_;
    AudioPullCallback pull_callback_;
    bool dummy_stop_ = false;
    bool dummy_reset_clock_ = true;
    std::thread dummy_thread_;
    std::vector<int16_t> sdl_scratch_;
};

std::unique_ptr<SdlDisplayBackend> g_display;
std::unique_ptr<SdlInputBackend> g_input;
std::unique_ptr<SdlAudioBackend> g_audio;
bool g_initialized = false;

} // namespace

bool Initialize(const PlatformConfig& config)
{
    if (g_initialized)
    {
        return true;
    }

    if (config.headless)
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    }

    if (!SDL_Init(SDL_INIT_EVENTS))
    {
        return false;
    }

    ResetInputSnapshot(config.headless || config.display.dummy);

    g_display = std::make_unique<SdlDisplayBackend>();
    if (!g_display->Initialize(config.display))
    {
        // Allowed to fail in headless/dummy if window creation fails
        if (!config.display.dummy && !config.headless)
        {
            Shutdown();
            return false;
        }
    }

    g_input = std::make_unique<SdlInputBackend>();
    g_input->Initialize();

    g_audio = std::make_unique<SdlAudioBackend>();
    if (!g_audio->Initialize(config.audio))
    {
        if (!config.audio.dummy && !config.headless)
        {
            Shutdown();
            return false;
        }
    }

    g_initialized = true;
    return true;
}

void Shutdown()
{
    if (g_audio)
    {
        g_audio->Shutdown();
        g_audio.reset();
    }
    if (g_input)
    {
        g_input->Shutdown();
        g_input.reset();
    }
    if (g_display)
    {
        g_display->Shutdown();
        g_display.reset();
    }

    SDL_Quit();
    ResetInputSnapshot(true);
    g_initialized = false;
}

DisplayBackend* GetDisplay()
{
    return g_display.get();
}

InputBackend* GetInput()
{
    return g_input.get();
}

AudioBackend* GetAudio()
{
    return g_audio.get();
}

} // namespace repsp::platform
