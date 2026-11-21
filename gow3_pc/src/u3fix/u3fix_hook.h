// u3fix_hook.h: what the u3fix fixes share: finding code by its bytes, stub pages within jump range of the image, jumps.
#pragma once
#include <stdint.h>

namespace u3fix {

constexpr uint64_t kNotFound = ~uint64_t{0};

// offset of the only place the bytes occur in the image; kNotFound when none or more than one
uint64_t find_unique(const unsigned char *image, uint64_t size, const unsigned char *bytes, uint64_t length);
// a read-write page within rel32 range of the image (nullptr when none is free)
unsigned char *stub_page_near(const unsigned char *image, uint64_t size);
// makes the stub page read-execute
bool finish_stub(unsigned char *stub);
// writes jmp rel32 to stub at image+at, the rest of the replaced length as nops (the image is still writable then)
bool jump_to(unsigned char *image, uint64_t at, unsigned length, const unsigned char *stub);

bool raise_net_process_pool(unsigned char *image, uint64_t size);
// the package manager's loaded/unloaded messages printed into the runtime log (package_log.cpp)
bool install_package_log(unsigned char *image, uint64_t size);
// a job counter wait that lasts over 5 s is reported with its counter and callers (counter_wait_log.cpp)
bool install_counter_wait_log(unsigned char *image, uint64_t size);
// a parked job's wait for 0 that can never end is ended after 4 s (stuck_counter.cpp)
bool install_stuck_counter(unsigned char *image, uint64_t size);
// asks for the counter to be settled when the wait can never end; true when asked
bool consider_stuck_wait(const unsigned char *counter, int64_t target, int mode, int64_t value, int64_t waited_ms,
                         const char *chain);
// settles the counters asked for: called on a game thread that holds no job system lock
void settle_stuck_counters();
// a job the job system drops because its priority queue is full is reported (job_queue_full_log.cpp)
bool install_job_queue_full_log(unsigned char *image, uint64_t size);
// the sound driver runs its mixing job's work itself instead of waiting for a job (audio_mix_direct.cpp)
bool install_audio_mix_direct(unsigned char *image, uint64_t size);
// a graphics context takes a free command buffer when its thread's is still in use (cmdbuf_borrow.cpp)
bool install_cmdbuf_borrow(unsigned char *image, uint64_t size);
// the game's script and animation error messages printed into the runtime log (script_errors_log.cpp)
bool install_script_errors_log(unsigned char *image, uint64_t size);
// names without debug memory are shown as their 32-bit ids (sid_names.cpp)
bool install_sid_names(unsigned char *image, uint64_t size);
// each skeleton-to-skeleton animation mapping the game looks up is printed once, found or missing (retarget_log.cpp)
bool install_retarget_log(unsigned char *image, uint64_t size);
// skeleton retargets switch to their multiplayer settings while a net game runs, as on PS3 (retarget_mode.cpp)
bool install_retarget_mode(unsigned char *image, uint64_t size);
// every job counter's history: maker, job count, jobs finished (job_trace.cpp)
bool install_job_trace(unsigned char *image, uint64_t size);
void counter_story(const void *counter, char *out, size_t size);
// the jobs waiting in each of the 8 priority queues, as text (job_queue_full_log.cpp)
void job_queue_counts(char *out, size_t size);
// an NPC copy on a co-op guest starts its base animation at a finite phase (npc_phase.cpp)
bool install_npc_phase_guard(unsigned char *image, uint64_t size);
bool install_improved_tonemap_curve(unsigned char *image, uint64_t size);
// the auto exposure averages at least one frame: luminance filter size 0 as PS3 (exposure_filter.cpp)
bool exposure_filter_at_least_one(unsigned char *image, uint64_t size);
// names and point popups above players use the HUD's 1280x720 canvas, as on PS3 (hudnet_text_canvas.cpp, issue 64)
bool install_hudnet_text_canvas(unsigned char *image, uint64_t size);
// a word on its own line in u3-switches.txt in the --user folder (tonemap_curve.cpp)
bool user_switch(const char *word);
// the PS3 improved tonemap curve: rewrites the tonemap constants when the render settings ask for it (tonemap_curve.cpp)
bool improved_tonemap_curve(float *constants, const unsigned char *settings, bool t1_curve, bool display = true);
// the default choice: the improved curve where the render settings' exposure maximum is at least 2 (not Borneo co-op)
bool improved_curve_by_default(const unsigned char *settings);

}  // namespace u3fix
