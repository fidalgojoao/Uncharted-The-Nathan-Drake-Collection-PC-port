// u3voice: voice chat for Uncharted 3 multiplayer on PC.
//
// The PS4 game has no voice code: it imports nothing from libSceVoice / libSceAudioIn and its Mute menu actions are
// empty. The PS3 game (1.19) carried cellVoice data inside its own type-0 data packets: after the game payload a bit
// "has voice", then the frames. The multiplayer helper builds those packets on PC (and already writes that bit as 0),
// so voice rides the same packets again; this module is the audio side the helper calls: the microphone (SDL3
// recording stream, voice-activity gate or push to talk), Opus (the codec of the Uncharted 2 voice chat, 16 kHz mono,
// 20 ms frames), one jitter buffer per connection, mixing and playback, mute and the talking state.
//
// The helper finds the API through the export uncharted_u3_voice of the game executable (its PC platform layer
// already resolves exports; game logic itself never calls the OS). Peers are the game's connection entries 0..31.
//
// Frames handed out by take() and taken by put(): each is one byte with its size, then the Opus packet.
//
// Environment (shared with Uncharted 2's names as fallback):
//   UNCHARTED_VOICE            off | open (default: voice-activity gate) | ptt (push to talk)
//   UNCHARTED_VOICE_PTT_KEY    SDL key name for push to talk (default "T")
//   UNCHARTED_VOICE_MUTE_KEY   SDL key name that mutes / unmutes your own microphone (default "M" with Ctrl held)
//   UNCHARTED_VOICE_VOLUME     playback gain of the other players, 0..4 (default 1)
//   UNCHARTED_VOICE_LOOPBACK   1: your own frames play back to you as peer 31 (a one-PC test)
//   UNCHARTED_VOICE_TEST       1: a test thread calls take() every 20 ms (with LOOPBACK: hear yourself, no helper)
#pragma once
#include <cstddef>
#include <cstdint>

#define U3VOICE_ABI __attribute__((sysv_abi))

extern "C" {

struct U3VoiceApi {
    uint32_t version;   // 1
    // The frames the microphone produced since the last call, written to out (at most cap bytes); returns how many
    // frames, *first_sequence = the sequence number of the first one. 0 when voice is off, your microphone is muted,
    // push to talk is up or nobody spoke. Call once per game frame (it also reads the push-to-talk and mute keys).
    int(U3VOICE_ABI *take)(uint8_t *out, int cap, uint16_t *first_sequence);
    // Frames from connection `peer` (0..31): `data` holds `count` frames (size byte + packet), `size` bytes in all.
    void(U3VOICE_ABI *put)(int peer, uint16_t first_sequence, const uint8_t *data, int size, int count);
    // Mute (on != 0) or unmute what you hear from a peer; muted() reads it back.
    void(U3VOICE_ABI *set_muted)(int peer, int on);
    int(U3VOICE_ABI *muted)(int peer);
    // The connection entry was released or reused: its buffers go and its mute is cleared.
    void(U3VOICE_ABI *reset)(int peer);
    // 1 while voice from the peer arrived in the last 300 ms; peer -1 = you (your microphone sent).
    int(U3VOICE_ABI *talking)(int peer);
    // Playback volume 0..4 (the options menu); 1 when unset.
    float(U3VOICE_ABI *volume)();
    void(U3VOICE_ABI *set_volume)(float v);
    // The match or party ended: the audio streams close and every peer is forgotten.
    void(U3VOICE_ABI *stop)();
};

// the API table (never null); exported from the executable for the helper
__declspec(dllexport) const U3VoiceApi *uncharted_u3_voice(void);

}  // extern "C"
