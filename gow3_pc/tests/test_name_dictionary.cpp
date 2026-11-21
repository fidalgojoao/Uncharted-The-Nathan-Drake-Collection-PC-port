// name ids back to names (src/name_dictionary.cpp), on made-up data: the hash, the pieces a run of name characters
// gives, the first name winning a shared id, and the folder reader (only *.bin and *.dci, directly in the folder).
#include "name_dictionary.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static uint32_t reference(const std::string &s) {   // bit by bit: polynomial 0x04c11db7, MSB first, start 0
    uint32_t c = 0;
    for (unsigned char ch : s) {
        c ^= static_cast<uint32_t>(ch) << 24;
        for (int k = 0; k < 8; ++k) c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1;
    }
    return c;
}

static bool has(const name_ids::Dictionary &d, const char *name) {
    const char *got = d.find(reference(name));
    return got && std::strcmp(got, name) == 0;
}

int main() {
    for (const char *s : {"a", "zork-lantern", "1234567890-abc_def?"})
        assert(name_ids::hash(s, std::strlen(s)) == reference(s));
    assert(name_ids::hash("", 0) == 0);

    name_ids::Dictionary d;
    const char data[] = "\x01\x02zork-lantern\0\xff(export *brass-key-art* *grue*)\x7fpath/to/lamp-oil.bin plain";
    d.add_text(data, sizeof data - 1);
    for (const char *name : {"zork-lantern", "zork", "brass-key-art", "brass-key", "brass", "grue", "path", "to",
                             "lamp-oil", "lamp", "lamp-oil.bin", "plain", "path/to/lamp-oil.bin"})
        assert(has(d, name));
    for (const char *name : {"lantern", "key-art", "*grue*", "grue*", "oil", "export *brass-key-art*"})
        assert(!d.find(reference(name)) || !has(d, name));
    assert(!d.find(0));

    const size_t before = d.size();
    d.add_text("zork-lantern", 12);   // already known: nothing new
    assert(d.size() == before);

    const auto folder = std::filesystem::temp_directory_path() / "gow3-name-dictionary-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "sub");
    std::ofstream(folder / "a.bin", std::ios::binary) << "frob-widget";
    std::ofstream(folder / "b.dci", std::ios::binary) << "(export *quux*)";
    std::ofstream(folder / "c.txt", std::ios::binary) << "ignored-name";
    std::ofstream(folder / "sub" / "d.bin", std::ios::binary) << "nested-name";
    name_ids::Dictionary f;
    assert(f.add_folder(folder.string()) == 2);
    assert(has(f, "frob-widget") && has(f, "quux"));
    assert(!f.find(reference("ignored-name")) && !f.find(reference("nested-name")));
    assert(name_ids::Dictionary().add_folder((folder / "missing").string()) == 0);
    std::filesystem::remove_all(folder);

    std::puts("name-dictionary-test: ok");
    return 0;
}
