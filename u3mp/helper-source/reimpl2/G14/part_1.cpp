// G14 part 1: online backend, room messages out. The shared helpers of the sending slots, the receive append and
// the slots Init (0), Shutdown (2) and AttachSession (3). Written from spec G14_online_backend_out.md, sections 2.1,
// 3 and 4.

extern "C" {
// the C part and other groups (spec 2.4)
void *u3h_onb_object(void);
int u3h_smn_up(void);
int u3h_smn_send(const void *m, uint32_t n);
void u3h_smn_attach(int on);
int u3h_ol_up(void);
int u3h_ol_frame(uint32_t type, const void *m, uint32_t n);
const char *u3h_fw_sess(uintptr_t base, uint8_t *ss);
void u3h_fw_line(const char *a, uint32_t hex, int digits, const char *b, int32_t v);
void u3x_onb_ping_restart(void *be);

// this part
void *u3x_onb_obj(uintptr_t base);
void u3x_onb_strcpy(char *d, const char *s, uint32_t n);
void u3x_onb_note(int slot);
int u3x_onb_bound(void *be, uint8_t *ss);
uint32_t u3x_onb_cookie(void *be, uint8_t *ss);
int32_t u3x_onb_send(const void *msg, uint32_t n);
void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v);
void u3x_onb_rx(const uint8_t *p, uint32_t n);
void u3x_onb_name_time(uintptr_t base, char *out);
void u3x_onb_geo(uintptr_t base, uint16_t *a, uint16_t *b);
int64_t u3x_onb_s0(void *be);
int64_t u3x_onb_s2(void *be);
int64_t u3x_onb_s3(void *be, uint8_t *ss);
}

namespace g14p1 {

// game image offsets (spec 3.1)
constexpr uintptr_t IMG_CLOCK = 0x17b4ba4;        // s32, the engine's local clock
constexpr uintptr_t IMG_GEO_A = 0x17d5c88;        // float pattern, location word A
constexpr uintptr_t IMG_GEO_B = 0x17d5c8c;        // float pattern, location word B
constexpr uintptr_t IMG_OWN_NAME = 0x17d6010;     // 0x24 bytes, the player's network name
constexpr uintptr_t IMG_ONLINE_OBJECT = 0x17dd760;
constexpr uintptr_t IMG_MANAGER = 0x17dd768;

// the online backend object (spec 3.2)
constexpr uintptr_t BE_BINDINGS = 0x30;           // four session pointers
constexpr uintptr_t BE_RX_COUNT = 0x54;           // u32, bytes waiting in the receive buffer
constexpr uintptr_t BE_RX_BUFFER = 0x58;
constexpr uint32_t BE_RX_SIZE = 0x1000;
constexpr int BINDINGS = 4;

constexpr uintptr_t SS_BACKEND = 8;               // session: the backend it is attached to

// eb[] words (spec 3.4)
constexpr int EB_INIT_RESULT = 130, EB_SLOT_CALLS = 131, EB_LAST_SLOT = 132, EB_SENT = 133, EB_LAST_TYPE = 134,
              EB_RX_DROPPED = 147;

constexpr uint32_t MSG_FIRST = 300, MSG_LAST = 0x3ff, FRAME_FIRST = 0x400;
constexpr uint32_t NAME_MAX = 0x24;
constexpr uint32_t LINE_MAX = 120;

U3X_RO const char t_session[] = " session, room ";

template <typename T> [[gnu::always_inline]] inline T &field(void *p, uintptr_t off) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off);
}

[[gnu::always_inline]] inline uint8_t *&binding(void *be, int k) {
    return field<uint8_t *>(be, BE_BINDINGS + 8u * static_cast<uintptr_t>(k));
}

// a built log line (spec 3.8): at most 120 characters, what does not fit is dropped
struct Text {
    char b[LINE_MAX + 1];
    uint32_t n = 0;
    [[gnu::always_inline]] void add(const char *t) { while (*t && n < LINE_MAX) b[n++] = *t++; }
    [[gnu::always_inline]] const char *z() { b[n] = 0; return b; }
};

}  // namespace g14p1

extern "C" {

// 4.1: the online object, only while it is the registered session manager
U3X_FN void *u3x_onb_obj(uintptr_t base) {
    void *object = at<void *>(base + g14p1::IMG_ONLINE_OBJECT);
    if (object && at<void *>(base + g14p1::IMG_MANAGER) == object) return object;
    return nullptr;
}

// 4.2: bounded text copy; always writes the ending NUL, also with n = 0
U3X_FN void u3x_onb_strcpy(char *d, const char *s, uint32_t n) {
    const uint32_t room = n ? n - 1 : 0;
    uint32_t i = 0;
    while (i < room && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

// 4.3: count a slot call and remember which slot it was
U3X_FN void u3x_onb_note(int slot) {
    uint32_t *eb = ext_eb();
    eb[g14p1::EB_SLOT_CALLS]++;
    eb[g14p1::EB_LAST_SLOT] = static_cast<uint32_t>(slot);
}

// 4.4: the lowest binding that holds this session, or -1
U3X_FN int u3x_onb_bound(void *be, uint8_t *ss) {
    for (int k = 0; k < g14p1::BINDINGS; k++)
        if (g14p1::binding(be, k) == ss) return k;
    return -1;
}

// 4.4: binding index + 1, or 0 when the session is not attached
U3X_FN uint32_t u3x_onb_cookie(void *be, uint8_t *ss) {
    return static_cast<uint32_t>(u3x_onb_bound(be, ss) + 1);
}

// 4.5: one room message, over the session socket while that is in use, else as a community-link frame
U3X_FN int32_t u3x_onb_send(const void *msg, uint32_t n) {
    const uint32_t type = rd<uint32_t>(msg, 0);
    if (type < g14p1::MSG_FIRST || type > g14p1::MSG_LAST) return -1;
    if (u3h_smn_up() != 0) {
        if (u3h_smn_send(msg, n) < 0) return -1;
    } else {
        if (u3h_ol_up() == 0) return -1;
        if (u3h_ol_frame(g14p1::FRAME_FIRST + (type - g14p1::MSG_FIRST), msg, n) == 0) return -1;
    }
    uint32_t *eb = ext_eb();
    eb[g14p1::EB_SENT]++;
    eb[g14p1::EB_LAST_TYPE] = type;
    return static_cast<int32_t>(n);
}

// 4.6: the session log line "<what><session kind> session, room 0x<8 hex digits><b><v>"
U3X_FN void u3x_onb_wire(uintptr_t base, const char *what, uint8_t *ss, uint64_t room, const char *b, int32_t v) {
    g14p1::Text line;
    line.add(what);
    line.add(u3h_fw_sess(base, ss));
    line.add(g14p1::t_session);
    u3h_fw_line(line.z(), static_cast<uint32_t>(room), 8, b, v);
}

// 4.7: append a pushed room message to the receive buffer of the C part's object storage
U3X_FN void u3x_onb_rx(const uint8_t *p, uint32_t n) {
    uint8_t *object = static_cast<uint8_t *>(u3h_onb_object());
    if (u3h_smn_up() != 0) return;   // the buffer belongs to the session socket's stream
    const uint32_t waiting = g14p1::field<uint32_t>(object, g14p1::BE_RX_COUNT);
    const uint32_t total = waiting + n;   // 32-bit sum
    if (total > g14p1::BE_RX_SIZE) {      // no room: the whole push is dropped
        ext_eb()[g14p1::EB_RX_DROPPED]++;
        return;
    }
    uint8_t *to = object + g14p1::BE_RX_BUFFER + waiting;
    for (uint32_t i = 0; i < n; i++) to[i] = p[i];
    g14p1::field<uint32_t>(object, g14p1::BE_RX_COUNT) = total;
}

// 4.8: the automatic room name "<own network name>.<local clock, unsigned decimal>"
U3X_FN void u3x_onb_name_time(uintptr_t base, char *out) {
    const char *name = reinterpret_cast<const char *>(base + g14p1::IMG_OWN_NAME);
    uint32_t n = 0;
    while (n < g14p1::NAME_MAX && name[n]) { out[n] = name[n]; n++; }
    out[n++] = '.';
    uint32_t clock = static_cast<uint32_t>(at<int32_t>(base + g14p1::IMG_CLOCK));
    char digits[10];
    int k = 0;
    do { digits[k++] = static_cast<char>('0' + clock % 10); clock /= 10; } while (clock);
    while (k) out[n++] = digits[--k];
    out[n] = 0;
}

// 4.9: the two location words as integers, crossed: *a from word B, *b from word A
U3X_FN void u3x_onb_geo(uintptr_t base, uint16_t *a, uint16_t *b) {
    const uint32_t word_a = at<uint32_t>(base + g14p1::IMG_GEO_A);
    const uint32_t word_b = at<uint32_t>(base + g14p1::IMG_GEO_B);
    *a = static_cast<uint16_t>(f_to_int(word_b));
    *b = static_cast<uint16_t>(f_to_int(word_a));
}

// 4.10, slot 0, Init: 0 when the session service can be reached, else -1
U3X_FN int64_t u3x_onb_s0(void *be) {
    u3x_onb_note(0);
    g14p1::field<uint32_t>(be, g14p1::BE_RX_COUNT) = 0;
    u3x_onb_ping_restart(be);
    const bool connected = u3h_smn_up() == 1 || u3h_ol_up() != 0;
    const int64_t result = connected ? 0 : -1;
    ext_eb()[g14p1::EB_INIT_RESULT] = static_cast<uint32_t>(result);
    if (connected) u3h_smn_attach(1);
    return result;
}

// 4.11, slot 2, Shutdown: the bindings and the sessions stay as they are
U3X_FN int64_t u3x_onb_s2(void *be) {
    u3x_onb_note(2);
    g14p1::field<uint32_t>(be, g14p1::BE_RX_COUNT) = 0;
    u3h_smn_attach(0);
    return 0;
}

// 4.12, slot 3, AttachSession: the session's own binding, else the lowest free one
U3X_FN int64_t u3x_onb_s3(void *be, uint8_t *ss) {
    u3x_onb_note(3);
    int k = u3x_onb_bound(be, ss);
    if (k < 0) k = u3x_onb_bound(be, nullptr);
    if (k < 0) return -1;
    g14p1::binding(be, k) = ss;
    g14p1::field<void *>(ss, g14p1::SS_BACKEND) = be;
    return k;
}

}  // extern "C"
