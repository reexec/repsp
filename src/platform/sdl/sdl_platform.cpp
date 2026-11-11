#include "repsp/platform/platform.h"

#include <SDL3/SDL.h>
#include <memory>

namespace repsp::platform
{

namespace
{

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
        config_ = config;
        if (config.dummy)
        {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        }

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return false;
        }

        SDL_WindowFlags flags = 0;
        if (config.fullscreen)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
        if (window_ == nullptr && !config.dummy)
        {
            return false;
        }

        return true;
    }

    void Shutdown() override
    {
        if (window_ != nullptr)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void SwapBuffers() override
    {
    }

    bool PollEvents() override
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                return false;
            }
        }
        return true;
    }

private:
    DisplayConfig config_;
    SDL_Window* window_ = nullptr;
};

class SdlInputBackend final : public InputBackend
{
public:
    bool Initialize() override
    {
        return SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    }

    void Shutdown() override
    {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    }

    void Poll(ControllerState& /*state*/) override
    {
        SDL_PumpEvents();
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
        config_ = config;
        if (config.dummy)
        {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        }

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            return false;
        }

        SDL_AudioSpec spec;
        SDL_zero(spec);
        spec.format = SDL_AUDIO_S16LE;
        spec.channels = config.channels;
        spec.freq = config.sample_rate;

        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr && !config.dummy)
        {
            return false;
        }

        if (stream_ != nullptr)
        {
            SDL_ResumeAudioStreamDevice(stream_);
        }
        return true;
    }

    void Shutdown() override
    {
        if (stream_ != nullptr)
        {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    bool QueueAudio(const int16_t* samples, size_t frame_count) override
    {
        if (stream_ == nullptr)
        {
            return config_.dummy;
        }
        int byte_count = static_cast<int>(frame_count * config_.channels * sizeof(int16_t));
        return SDL_PutAudioStreamData(stream_, samples, byte_count);
    }

private:
    AudioConfig config_;
    SDL_AudioStream* stream_ = nullptr;
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
