// Name ids back to names (name_dictionary.h).
//
// A name in the data is a run of name characters (letters, digits, '-', '_', '?', '!', '.', '/', '*'). The exported
// symbols of a module interface are written as *name*, and a name often reappears as the start of a longer one
// ("<weapon>-art" for the weapon <weapon>), so each run gives several candidates: every piece that starts at the run's
// start or right after a '*' or '/', and ends at the run's end or right before a '-', '*', '/' or '.' (never on a '*'
// or '/' itself). Each candidate
// is kept under its hash; an id that two different candidates share keeps the first one read. The id 0 is "no name"
// in the games and is never given one.
#include "name_dictionary.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace name_ids {

namespace {

struct Table {
    uint32_t v[256];
    Table() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i << 24;
            for (int k = 0; k < 8; ++k) c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1;
            v[i] = c;
        }
    }
};

bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '?' || c == '!' || c == '.' || c == '/' || c == '*';
}

constexpr size_t kLongest = 96;   // longer pieces are not names

bool read_file(const std::string &path, std::string &out) {
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);
    FILE *f = _wfopen(wide.c_str(), L"rb");
#else
    FILE *f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    const bool ok = out.empty() || std::fread(&out[0], 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool data_file(const std::string &name) {
    auto ends = [&](const char *tail) {
        const size_t n = std::char_traits<char>::length(tail);
        return name.size() > n && name.compare(name.size() - n, n, tail) == 0;
    };
    return ends(".bin") || ends(".dci");
}

}  // namespace

uint32_t hash(const char *text, size_t length) {
    static const Table table;
    uint32_t c = 0;
    for (size_t i = 0; i < length; ++i) c = (c << 8) ^ table.v[(c >> 24) ^ static_cast<uint8_t>(text[i])];
    return c;
}

void Dictionary::add(const char *name, size_t length) {
    if (!ids_.emplace(hash(name, length), pool_.size()).second) return;
    pool_.append(name, length);
    pool_.push_back('\0');
}

void Dictionary::add_text(const char *data, size_t size) {
    size_t i = 0;
    while (i < size) {
        if (!name_char(data[i])) { ++i; continue; }
        size_t end = i;
        while (end < size && name_char(data[end])) ++end;
        for (size_t s = i; s < end; ++s) {
            if (s != i && data[s - 1] != '*' && data[s - 1] != '/') continue;
            if (data[s] == '*' || data[s] == '/') continue;
            for (size_t e = s + 1; e <= end && e - s <= kLongest; ++e) {
                if (e != end && data[e] != '-' && data[e] != '*' && data[e] != '/' && data[e] != '.') continue;
                if (data[e - 1] == '*' || data[e - 1] == '/') continue;
                add(data + s, e - s);
            }
        }
        i = end;
    }
}

size_t Dictionary::add_folder(const std::string &folder) {
    std::vector<std::string> names;
#ifdef _WIN32
    const std::string pattern = folder + "\\*";
    const int n = MultiByteToWideChar(CP_UTF8, 0, pattern.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pattern.c_str(), -1, &wide[0], n);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char name[MAX_PATH * 3];
            if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, nullptr, nullptr) > 0)
                names.push_back(name);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    if (DIR *d = opendir(folder.c_str())) {
        while (dirent *e = readdir(d)) names.push_back(e->d_name);
        closedir(d);
    }
#endif
    std::sort(names.begin(), names.end());   // the same order on every run, so a shared id keeps the same name
    size_t files = 0;
    std::string data;
    for (const std::string &name : names) {
        if (!data_file(name) || !read_file(folder + "/" + name, data)) continue;
        add_text(data.data(), data.size());
        ++files;
    }
    return files;
}

const char *Dictionary::find(uint32_t id) const {
    if (!id) return nullptr;
    auto it = ids_.find(id);
    return it == ids_.end() ? nullptr : pool_.c_str() + it->second;
}

}  // namespace name_ids
