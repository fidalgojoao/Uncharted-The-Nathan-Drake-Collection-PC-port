// u3voice: Uncharted 3 voice chat on PC (see u3voice.h). The codec, jitter buffer, gate and mixing are those of the
// Uncharted 2 voice chat (u2mp/voice.h); this file is the U3 side: per-connection speakers and the helper's API.
#include "u3voice.h"

#include "../u2mp/voice.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using u2mp::voice::Decoder;
using u2mp::voice::Encoder;
using u2mp::voice::Gate;
using u2mp::voice::JitterBuffer;
using u2mp::voice::kFrameSamples;
using u2mp::voice::kSampleRate;

constexpr int kPeers = 32;
constexpr int kLoopbackPeer = 31;

void say(const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    std::printf("Uncharted 3 voice: %s\n", line);
    std::fflush(stdout);
}

const char *env(const char *name, const char *u2_name) {
    const char *v = std::getenv(name);
    if ((!v || !*v) && u2_name) v = std::getenv(u2_name);
    return v && *v ? v : nullptr;
}

enum class Mode { Off, Open, PushToTalk };
struct Config {
    Mode mode = Mode::Open;
    SDL_Scancode ptt = SDL_SCANCODE_T, mute_key = SDL_SCANCODE_M;
    std::atomic<float> volume{1.0f};
    bool loopback = false;
};
Config &config() {
    static Config c;
    static std::once_flag once;
    std::call_once(once, [] {
        if (const char *m = env("UNCHARTED_VOICE", "UNCHARTED_U2MP_VOICE")) {
            const std::string v(m);
            if (v == "off" || v == "0") c.mode = Mode::Off;
            else if (v == "ptt") c.mode = Mode::PushToTalk;
            else if (v != "open") say("UNCHARTED_VOICE=%s not understood (off|open|ptt), using open", m);
        }
        auto key = [](const char *name, const char *u2, SDL_Scancode &out, const char *dflt) {
            if (const char *k = env(name, u2)) {
                const SDL_Scancode code = SDL_GetScancodeFromName(k);
                if (code != SDL_SCANCODE_UNKNOWN) out = code;
                else say("key \"%s\" unknown, using %s", k, dflt);
            }
        };
        key("UNCHARTED_VOICE_PTT_KEY", "UNCHARTED_U2MP_VOICE_PTT_KEY", c.ptt, "T");
        key("UNCHARTED_VOICE_MUTE_KEY", nullptr, c.mute_key, "Ctrl+M");
        if (const char *v = env("UNCHARTED_VOICE_VOLUME", "UNCHARTED_U2MP_VOICE_VOLUME"))
            c.volume = std::clamp(float(std::atof(v)), 0.0f, 4.0f);
        c.loopback = env("UNCHARTED_VOICE_LOOPBACK", nullptr) != nullptr;
        static const char *const names[] = {"off", "open microphone (voice-activity gate)", "push to talk"};
        say("%s, volume %.2f%s", names[int(c.mode)], double(c.volume.load()), c.loopback ? ", loopback" : "");
    });
    return c;
}

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- state
std::mutex api_mutex;                  // take / put / reset / stop (the game thread, but keep it safe)
SDL_AudioStream *capture = nullptr, *playback = nullptr;
bool audio_open = false, sdl_audio = false;
std::atomic<bool> ptt_down{false}, mic_muted{false};
bool mute_key_was_down = false;
uint16_t send_sequence = 0;
std::atomic<bool> peer_muted[kPeers];
std::atomic<int64_t> heard_ms[kPeers], sent_ms{0};

// capture thread -> take()
std::mutex out_mutex;
std::deque<std::vector<uint8_t>> outgoing;
std::unique_ptr<Encoder> encoder;
Gate gate;

// put() -> playback thread
struct Speaker {
    JitterBuffer jitter;
    Decoder decoder;
};
std::mutex play_mutex;
std::unique_ptr<Speaker> speakers[kPeers];

void SDLCALL on_capture(void *, SDL_AudioStream *stream, int, int) {
    int16_t pcm[kFrameSamples];
    std::vector<uint8_t> packet;
    while (SDL_GetAudioStreamAvailable(stream) >= int(sizeof pcm)) {
        if (SDL_GetAudioStreamData(stream, pcm, sizeof pcm) != int(sizeof pcm)) break;
        const bool voiced = gate.update(pcm, kFrameSamples);   // keeps the noise floor current
        const bool talk = config().mode == Mode::PushToTalk ? ptt_down.load(std::memory_order_relaxed) : voiced;
        if (!talk || mic_muted.load(std::memory_order_relaxed) || !encoder || !encoder->encode(pcm, packet)) continue;
        if (packet.empty() || packet.size() > 255) continue;
        std::lock_guard<std::mutex> lock(out_mutex);
        if (outgoing.size() < 25) outgoing.push_back(std::move(packet));   // 0.5 s: nobody took them
    }
}

void SDLCALL on_playback(void *, SDL_AudioStream *stream, int additional, int) {
    static thread_local std::vector<uint8_t> frame, next;
    while (additional > 0) {
        int32_t mix[kFrameSamples] = {};
        int16_t pcm[kFrameSamples];
        {
            std::lock_guard<std::mutex> lock(play_mutex);
            const float gain = config().volume.load(std::memory_order_relaxed);
            for (auto &sp : speakers) {
                if (!sp) continue;
                const JitterBuffer::Out out = sp->jitter.pop(frame, &next);
                bool ok = false;
                if (out == JitterBuffer::Out::Frame) ok = sp->decoder.decode(frame.data(), frame.size(), pcm);
                else if (out == JitterBuffer::Out::Lost)
                    ok = next.empty() ? sp->decoder.decode(nullptr, 0, pcm) : sp->decoder.decode(next.data(), next.size(), pcm, true);
                if (ok) u2mp::voice::mix_into(mix, pcm, kFrameSamples, gain);
            }
        }
        u2mp::voice::to_pcm16(mix, pcm, kFrameSamples);
        SDL_PutAudioStreamData(stream, pcm, sizeof pcm);
        additional -= int(sizeof pcm);
    }
}

const char *device_name(SDL_AudioStream *stream) {
    const char *n = stream ? SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream)) : nullptr;
    return n ? n : "?";
}

void open_audio() {
    if (audio_open) return;
    audio_open = true;
    sdl_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);   // reference counted: the game's audio may have it already
    if (!sdl_audio) {
        say("SDL audio unavailable (%s): no voice chat", SDL_GetError());
        return;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, kSampleRate};
    if ((playback = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, on_playback, nullptr))) {
        SDL_ResumeAudioStreamDevice(playback);
        say("playing on %s", device_name(playback));
    } else {
        say("no playback stream (%s)", SDL_GetError());
    }
    encoder = std::make_unique<Encoder>();
    if (!encoder->ok()) {
        say("Opus encoder unavailable: listening only");
    } else if ((capture = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, on_capture, nullptr))) {
        SDL_ResumeAudioStreamDevice(capture);
        say("recording from %s", device_name(capture));
    } else {
        say("no microphone (%s): listening only", SDL_GetError());
    }
}

void forget(int peer) {
    peer_muted[peer] = false;
    heard_ms[peer] = 0;
    std::lock_guard<std::mutex> lock(play_mutex);
    speakers[peer].reset();
}

// queue frames (size byte + packet each) into a peer's jitter buffer
void queue(int peer, uint16_t sequence, const uint8_t *data, int size, int count) {
    if (peer < 0 || peer >= kPeers || !data || size <= 0 || count <= 0) return;
    if (!playback || peer_muted[peer].load(std::memory_order_relaxed)) return;
    heard_ms[peer].store(now_ms(), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(play_mutex);
    if (!speakers[peer]) speakers[peer] = std::make_unique<Speaker>();
    for (int i = 0, at = 0; i < count && at < size; i++) {
        const int n = data[at];
        if (n == 0 || at + 1 + n > size) break;
        speakers[peer]->jitter.push(uint16_t(sequence + i), data + at + 1, size_t(n));
        at += 1 + n;
    }
}

// ---- the API
int U3VOICE_ABI take(uint8_t *out, int cap, uint16_t *first_sequence) {
    Config &c = config();
    if (c.mode == Mode::Off || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> api(api_mutex);
    open_audio();
    const bool *keys = SDL_GetKeyboardState(nullptr);
    if (keys) {
        if (c.mode == Mode::PushToTalk) ptt_down.store(keys[c.ptt], std::memory_order_relaxed);
        // the own-microphone mute: the key with Ctrl held (default Ctrl+M), toggled on press
        const bool ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL];
        const bool down = keys[c.mute_key] && (ctrl || env("UNCHARTED_VOICE_MUTE_KEY", nullptr));
        if (down && !mute_key_was_down) {
            mic_muted = !mic_muted;
            say("your microphone is %s", mic_muted ? "muted" : "on");
        }
        mute_key_was_down = down;
    }
    std::deque<std::vector<uint8_t>> batch;
    {
        std::lock_guard<std::mutex> lock(out_mutex);
        batch.swap(outgoing);
    }
    int frames = 0, at = 0;
    const uint16_t first = send_sequence;
    for (const auto &p : batch) {
        if (at + 1 + int(p.size()) > cap || frames == 8) break;   // 8 frames (160 ms) per packet at most
        out[at] = uint8_t(p.size());
        std::memcpy(out + at + 1, p.data(), p.size());
        at += 1 + int(p.size());
        frames++;
    }
    if (!frames) return 0;
    send_sequence = uint16_t(send_sequence + frames);
    if (first_sequence) *first_sequence = first;
    sent_ms.store(now_ms(), std::memory_order_relaxed);
    if (c.loopback) queue(kLoopbackPeer, first, out, at, frames);
    return frames;
}

void U3VOICE_ABI put(int peer, uint16_t first_sequence, const uint8_t *data, int size, int count) {
    if (config().mode == Mode::Off) return;
    std::lock_guard<std::mutex> api(api_mutex);
    open_audio();
    queue(peer, first_sequence, data, size, count);
}

void U3VOICE_ABI set_muted(int peer, int on) {
    if (peer < 0 || peer >= kPeers) return;
    peer_muted[peer] = on != 0;
    if (on) {
        std::lock_guard<std::mutex> lock(play_mutex);
        if (speakers[peer]) speakers[peer]->jitter.clear();
    }
    say("connection %d %s", peer, on ? "muted" : "unmuted");
}

int U3VOICE_ABI muted(int peer) { return peer >= 0 && peer < kPeers && peer_muted[peer].load() ? 1 : 0; }

void U3VOICE_ABI reset(int peer) {
    if (peer < 0 || peer >= kPeers) return;
    std::lock_guard<std::mutex> api(api_mutex);
    forget(peer);
}

int U3VOICE_ABI talking(int peer) {
    const int64_t last = peer == -1 ? sent_ms.load() : peer >= 0 && peer < kPeers ? heard_ms[peer].load() : 0;
    return last && now_ms() - last < 300 ? 1 : 0;
}

float U3VOICE_ABI volume() { return config().volume.load(); }

void U3VOICE_ABI set_volume(float v) {
    config().volume = std::clamp(v, 0.0f, 4.0f);
    say("volume %.2f", double(config().volume.load()));
}

void U3VOICE_ABI stop() {
    std::lock_guard<std::mutex> api(api_mutex);
    if (capture) SDL_DestroyAudioStream(capture);   // no callback runs after this returns
    if (playback) SDL_DestroyAudioStream(playback);
    capture = playback = nullptr;
    encoder.reset();
    if (sdl_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (audio_open) say("closed");
    sdl_audio = audio_open = false;
    {
        std::lock_guard<std::mutex> lock(out_mutex);
        outgoing.clear();
    }
    for (int p = 0; p < kPeers; p++) {
        peer_muted[p] = false;
        heard_ms[p] = 0;
        speakers[p].reset();
    }
    gate = Gate{};
}

const U3VoiceApi api_table = {1, take, put, set_muted, muted, reset, talking, volume, set_volume, stop};

// UNCHARTED_VOICE_TEST=1: a thread takes the frames every 20 ms (with UNCHARTED_VOICE_LOOPBACK=1 you hear yourself
// about 100 ms late): the microphone, gate, push to talk, own mute, codec, jitter buffer and playback without the helper
[[maybe_unused]] const bool test_thread = [] {
    if (!std::getenv("UNCHARTED_VOICE_TEST")) return false;
    std::thread([] {
        uint8_t buf[2048];
        uint16_t seq;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            take(buf, sizeof buf, &seq);
        }
    }).detach();
    return true;
}();

}  // namespace

extern "C" const U3VoiceApi *uncharted_u3_voice(void) { return &api_table; }
