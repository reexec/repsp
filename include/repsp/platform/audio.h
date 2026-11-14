#ifndef REPSP_PLATFORM_AUDIO_H
#define REPSP_PLATFORM_AUDIO_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace repsp::platform
{

struct AudioConfig
{
    int32_t sample_rate = 44100;
    int32_t channels = 2;
    int32_t buffer_frames = 256;
    bool dummy = false;
};

using AudioPullCallback = std::function<void(int16_t* stereo_out, size_t frame_count)>;

class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    virtual bool Initialize(const AudioConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void SetPullCallback(AudioPullCallback callback) = 0;
    virtual bool QueueAudio(const int16_t* samples, size_t frame_count) = 0;
};

} // namespace repsp::platform

#endif // REPSP_PLATFORM_AUDIO_H
