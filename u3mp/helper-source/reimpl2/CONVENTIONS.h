// CONVENTIONS.h: the shared tools of the helper's C++ part (u3x_fixes.cpp), for implementers. This is everything an
// implementer may use from that file; new functions are appended to it, so these names are in scope. Build flags:
// --target=x86_64-unknown-elf -std=c++20 -O2 -ffreestanding -fno-builtin -fPIC -fvisibility=hidden -mno-red-zone -mno-sse
// -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables -nostdlibinc (no library, no float/double passed or
// returned by value between non-inlined functions, no string literals outside U3X_RO arrays, no static tables of game values).
#include <stdint.h>

#define U3X_FN [[gnu::section(".text.ext"), gnu::noinline]]
#define U3X_RO [[gnu::section(".rodata.ext")]]
#define U3X_BSS [[gnu::section(".bss.u3ext.x3x"), gnu::aligned(16), gnu::used]]

namespace {

using Log = void (*)(uintptr_t base, const char *line);

U3X_RO const char hexdig[] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

constexpr uint32_t F_ONE = 0x3f800000u;

[[gnu::always_inline]] inline uint32_t f_add(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\taddss %%xmm1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1");
    return r;
}
[[gnu::always_inline]] inline uint32_t f_sub(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\tsubss %%xmm1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1");
    return r;
}
[[gnu::always_inline]] inline uint32_t f_mul(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\tmulss %%xmm1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1");
    return r;
}
[[gnu::always_inline]] inline bool ptr_ok(const void *p) {   // = the C part's stage_ptr_ok
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000u && v < (1ull << 47) && !(v & 7);
}
template <typename T> [[gnu::always_inline]] inline T rd(const void *p, uintptr_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + off);
}

// a line for boot.log: text + decimal or hex numbers, at most 190 characters
struct Line {
    char b[200];
    int n = 0;
    [[gnu::always_inline]] Line &s(const char *t) { while (*t && n < 190) b[n++] = *t++; return *this; }
    [[gnu::always_inline]] Line &d(uint32_t v) {
        char t[12]; int k = 0;
        do { t[k++] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
        while (k && n < 190) b[n++] = t[--k];
        return *this;
    }
    [[gnu::always_inline]] Line &x(uint64_t v, int digits) {
        if (n < 180) { b[n++] = '0'; b[n++] = 'x'; }
        for (int i = (digits - 1) * 4; i >= 0 && n < 190; i -= 4) b[n++] = hexdig[(v >> i) & 15];
        return *this;
    }
    [[gnu::always_inline]] const char *z() { b[n] = 0; return b; }
};

}  // namespace

namespace {
template <typename R, typename... A> R game_call(uintptr_t address, A... args) { return reinterpret_cast<R (*)(A...)>(address)(args...); }
template <typename T> T &at(uintptr_t address) { return *reinterpret_cast<T *>(address); }
// x87 float helpers (this part is built without SSE, like the C): constants load from an immediate so nothing lands in
// .rodata.cst4
[[gnu::always_inline]] inline float x87_const(uint32_t u) {
    float r;
    __asm__("flds %1" : "=t"(r) : "m"(u));
    return r;
}
[[gnu::always_inline]] inline float x87_float(uint32_t u) {
    float r;
    __builtin_memcpy(&r, &u, 4);
    return r;
}
[[gnu::always_inline]] inline uint32_t x87_bits(float f) {
    uint32_t u;
    __builtin_memcpy(&u, &f, 4);
    return u;
}
}  // namespace

// A game function at image offset OFF is called as game_call<Result>(base + OFF, args...); game data at image offset
// OFF is at<Type>(base + OFF); a field is rd<Type>(pointer, offset) to read and
// *reinterpret_cast<Type *>(pointer + offset) = value to write. The helper's C part is reached through the exports
// the spec names (u3h_...). Log lines: Line l; l.s(text).d(number).x(value, digits); log(base, l.z());

// ---- more shared tools (single-precision operations on 32-bit patterns: each one rounds to float like the game's
// own SSE code; the helper's diagnostics area; the block for calls that need the game's float registers)
[[gnu::always_inline]] inline uint32_t f_from_int(int32_t v) {
    uint32_t r;
    __asm__("cvtsi2ss %1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(v) : "xmm0");
    return r;
}
[[gnu::always_inline]] inline int32_t f_to_int(uint32_t a) {
    int32_t r;
    __asm__("movd %1,%%xmm0\n\tcvttss2si %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}
[[gnu::always_inline]] inline bool f_less(uint32_t a, uint32_t b) {   // a < b as floats (false when either is NaN)
    int r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\txorl %0,%0\n\tucomiss %%xmm0,%%xmm1\n\tseta %b0" : "=&r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1", "cc");
    return r != 0;
}
[[gnu::always_inline]] inline uint32_t f_div(uint32_t a, uint32_t b) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\tdivss %%xmm1,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a), "r"(b) : "xmm0", "xmm1");
    return r;
}
[[gnu::always_inline]] inline uint32_t f_madd(uint32_t a, uint32_t b, uint32_t c) {   // a * b + c, one rounding to float
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tmovd %2,%%xmm1\n\tmovd %3,%%xmm2\n\tcvtss2sd %%xmm0,%%xmm0\n\tcvtss2sd %%xmm1,%%xmm1\n\t"
            "cvtss2sd %%xmm2,%%xmm2\n\tmulsd %%xmm1,%%xmm0\n\taddsd %%xmm2,%%xmm0\n\tcvtsd2ss %%xmm0,%%xmm0\n\tmovd %%xmm0,%0"
            : "=r"(r) : "r"(a), "r"(b), "r"(c) : "xmm0", "xmm1", "xmm2");
    return r;
}
[[gnu::always_inline]] inline uint32_t f_root(uint32_t a) {
    uint32_t r;
    __asm__("movd %1,%%xmm0\n\tsqrtss %%xmm0,%%xmm0\n\tmovd %%xmm0,%0" : "=r"(r) : "r"(a) : "xmm0");
    return r;
}
extern "C" uint8_t *u3h_ext(void);   // the helper's extension state (C part)
inline uint32_t *ext_eb() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x4050); }   // switch / diagnostic words eb[]
// A call that passes float registers: fn = address, i[] = the six integer arguments, x[] = five float registers (bit
// patterns in the low 32 bits), s[] = two stack words, al = number of float registers used; result = rax.
struct GameCall { uint64_t fn, i[6], x[5], s[2], al; };
extern "C" uint64_t u3x_k26_call(void *game_call_block);
inline uint32_t *ext_ea() { return reinterpret_cast<uint32_t *>(u3h_ext() + 0x3c50); }   // diagnostic words ea[]
