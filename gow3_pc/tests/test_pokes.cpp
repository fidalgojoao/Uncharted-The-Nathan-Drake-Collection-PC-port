// GOW3_POKE parser (src/pokes.cpp): the entry forms the launcher writes, one-byte and 32-bit values.
#include "pokes.h"

#include <cassert>
#include <cstdio>

int main() {
    using pokes::parse;
    assert(parse(nullptr).empty());
    assert(parse("").empty());

    // the cheats' form: an offset, value 1
    auto a = parse("17faa91");
    assert(a.size() == 1 && !a[0].deref && a[0].offset == 0x17faa91 && a[0].value == 1 && a[0].width == 1);

    // a pointer and an offset past it, an explicit byte value
    auto b = parse("*16e3078+1b9=0,1247cfd=0");
    assert(b.size() == 2);
    assert(b[0].deref && b[0].offset == 0x16e3078 && b[0].add == 0x1b9 && b[0].value == 0 && b[0].width == 1);
    assert(!b[1].deref && b[1].offset == 0x1247cfd && b[1].value == 0 && b[1].width == 1);
    auto c = parse("*16e2f80+a7");
    assert(c.size() == 1 && c[0].deref && c[0].add == 0xa7 && c[0].value == 1 && c[0].width == 1);

    // eight hex digits: a 32-bit word (here the bits of 0.5f)
    auto d = parse("17baf64,17b4ab4=3f000000,ff=1");
    assert(d.size() == 3);
    assert(d[0].offset == 0x17baf64 && d[0].value == 1 && d[0].width == 1);
    assert(d[1].offset == 0x17b4ab4 && d[1].value == 0x3f000000u && d[1].width == 4);
    assert(d[2].offset == 0xff && d[2].value == 1 && d[2].width == 1);
    auto e = parse("10=ff,20=3e4ccccd");
    assert(e[0].value == 0xff && e[0].width == 1 && e[1].value == 0x3e4ccccdu && e[1].width == 4);

    std::puts("pokes-test: ok");
    return 0;
}
