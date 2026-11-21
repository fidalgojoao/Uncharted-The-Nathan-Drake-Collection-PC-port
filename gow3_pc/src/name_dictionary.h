// name_dictionary: text names for the 32-bit name ids of Uncharted 2 and 3, read from the player's own game files.
//
// The retail games keep only the ids (a CRC-32 of the name: polynomial 0x04c11db7, most significant bit first, start
// value 0, no final xor). Their data files still spell many names out (string lists, and the exported symbols of the
// *.dci module interfaces), so the names that hash to an id can be found there again. Nothing of the games is stored
// in this source; the names come from the files of the copy the runtime runs (sid_names.cpp).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_map>

namespace name_ids {

uint32_t hash(const char *text, size_t length);

class Dictionary {
public:
    // every name the bytes spell out (see name_dictionary.cpp)
    void add_text(const char *data, size_t size);
    // the data files (*.bin, *.dci) directly in folder (host path, UTF-8), in name order; returns how many were read
    size_t add_folder(const std::string &folder);
    // the name, or nullptr; valid as long as the dictionary
    const char *find(uint32_t id) const;
    size_t size() const { return ids_.size(); }

private:
    void add(const char *name, size_t length);
    std::unordered_map<uint32_t, size_t> ids_;   // id -> offset of its name in pool_ (the first name read wins)
    std::string pool_;                           // the names, each ended by a 0
};

}  // namespace name_ids
