// G19, part 2: list upkeep. Players met, the new players of a match and their history, the players-met fetch and
// the friend events. Written from the behaviour description spec/G19_friends_rows.md (sections 5.1 to 5.6).

extern "C" {
// the C part and group G18 (declared, not implemented here)
const char *u3h_text(uintptr_t base, void *s, uint32_t id);
uint8_t *u3h_fr_find(uintptr_t base, const uint8_t *name);
int u3h_fr_strcmp(uintptr_t base, const void *a, const void *b);
int32_t u3h_pm_add(uintptr_t base, const uint8_t *name, const char *title);
int32_t u3h_pm_count(uintptr_t base);
int32_t u3h_pm_get(uintptr_t base, uint32_t i, uint8_t *id);
int32_t u3h_fr_event(uintptr_t base, uint8_t *id);
int u3h_take_rebuild(void);
void u3x_friend_list_thread(uintptr_t base);
void u3x_friend_add(uintptr_t base, const uint8_t *id, const uint8_t *presence);

// this part
void u3x_history_thread(uintptr_t base, void *s);
void u3x_players_met_add(uintptr_t base, void *s, const uint8_t *names, uint32_t n, int flag);
void u3x_players_met_fetch(uintptr_t base, void *s);
void u3x_match_players(uintptr_t base, void *s);
void u3x_friend_events(uintptr_t base);
}

namespace g19p2 {

constexpr uint32_t ID_SIZE = 0x24;      // a player id
constexpr uint32_t MET_SIZE = 0x28;     // a players-met entry: the id, then a u32
// The byte offset of a players-met entry or of a new-player slot is a 32-bit product (index x size, wrapping at
// 2^32) that is added to the 64-bit list pointer.

// the friend object (image offsets of its fields)
constexpr uintptr_t NF = 0x17caeb0;
constexpr uintptr_t NF_COUNT = NF + 0x00;         // u32 number of friends
constexpr uintptr_t NF_ENTRIES = NF + 0x08;       // pointer, null = not set up
constexpr uintptr_t NF_MET_COUNT = NF + 0x10;     // u32 number of players met
constexpr uintptr_t NF_MET_ROOM = NF + 0x14;      // u32 capacity of the players-met list
constexpr uintptr_t NF_MET = NF + 0x18;           // pointer to the players-met entries
constexpr uintptr_t NF_CHANGED = NF + 0x20;       // u8 "history changed"
constexpr uintptr_t NF_LIST_BUSY = NF + 0xc8;     // u64 the list fetch still has to run
constexpr uintptr_t NF_FETCH_BUSY = NF + 0xd0;    // u64 the players-met fetch runs
constexpr uintptr_t NF_UPLOAD_BUSY = NF + 0xe0;   // u64 the history upload runs
constexpr uintptr_t NF_NEW_ROOM = NF + 0xe8;      // u32 capacity of the new-players list
constexpr uintptr_t NF_NEW_COUNT = NF + 0xec;     // u32 number of new players
constexpr uintptr_t NF_NEW = NF + 0xf0;           // pointer to the new players' ids

// the net state and the net game manager
constexpr uintptr_t NET_LAN = 0x17d5c60;          // u8 non-zero = LAN / not online
constexpr uintptr_t NET_MULTIPLAYER = 0x17d5c61;  // u8 non-zero = multiplayer is active
constexpr uintptr_t NGM = 0x17cb060;
constexpr uintptr_t NGM_LOCAL_A = NGM + 0x370;    // pointer: player record of a local player
constexpr uintptr_t NGM_LOCAL_B = NGM + 0x378;    // pointer: player record of the other local player
constexpr uintptr_t NGM_PLAYERS = NGM + 0x390;    // the player records
constexpr uint32_t PLAYER_RECORDS = 10;
constexpr uintptr_t PLAYER_SIZE = 0x9c0;
constexpr uintptr_t PLAYER_ID = 0x5b8;            // the id text inside a player record

constexpr uint32_t TEXT_TITLE = 0xfd2de693;       // the game's title
constexpr uint32_t FRIENDS_OFF = 0x20;            // bit of ea[1]
constexpr int MAX_EVENTS = 64;                    // friend events taken per call

// the helper's switch word and counters
constexpr int EA_SWITCHES = 1, EA_HISTORY_ADDED = 72, EA_HISTORY_FAILED = 73, EA_FETCHES = 74, EA_MATCH_LISTS = 75,
              EA_EVENTS = 76;

U3X_FN void count_up(int word) { ext_ea()[word]++; }

U3X_FN void copy_id(uint8_t *to, const uint8_t *from) {
    for (uint32_t i = 0; i < ID_SIZE; i++) to[i] = from[i];
}

U3X_FN void clear_id(uint8_t *id) {
    for (uint32_t i = 0; i < ID_SIZE; i++) id[i] = 0;
}

U3X_FN bool list_busy(uintptr_t base) { return at<uint64_t>(base + NF_LIST_BUSY) != 0; }

// is this id a friend (nobody is while the friend list is busy: the list is not asked then)
U3X_FN bool is_friend(uintptr_t base, const uint8_t *id) {
    return !list_busy(base) && u3h_fr_find(base, id) != nullptr;
}

// is this id in the players-met list
U3X_FN bool met_before(uintptr_t base, const uint8_t *id) {
    const uint32_t number = at<uint32_t>(base + NF_MET_COUNT);
    for (uint32_t i = 0; i < number; i++) {
        const uint8_t *entry = at<uint8_t *>(base + NF_MET) + i * MET_SIZE;
        if (u3h_fr_strcmp(base, entry, id) == 0) return true;
    }
    return false;
}

// the tests every id has to pass before it is listed, in the order that is to keep: not empty, not a friend, room
// in the list it goes to, not met before
U3X_FN bool is_new(uintptr_t base, const uint8_t *id, uintptr_t number_field, uintptr_t room_field) {
    if (id[0] == 0) return false;
    if (is_friend(base, id)) return false;
    if (at<uint32_t>(base + number_field) >= at<uint32_t>(base + room_field)) return false;
    return !met_before(base, id);
}

// ids go into the players-met list
U3X_FN void add_met(uintptr_t base, const uint8_t *names, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *id = names + uintptr_t(i) * ID_SIZE;
        if (!is_new(base, id, NF_MET_COUNT, NF_MET_ROOM)) continue;
        uint32_t &number = at<uint32_t>(base + NF_MET_COUNT);
        uint8_t *entry = at<uint8_t *>(base + NF_MET) + number * MET_SIZE;
        copy_id(entry, id);
        *reinterpret_cast<uint32_t *>(entry + ID_SIZE) = 0;
        number++;
    }
}

// ids are the new players of a match: they are listed and sent to the history
U3X_FN void add_new(uintptr_t base, void *s, const uint8_t *names, uint32_t n) {
    if (at<uint64_t>(base + NF_UPLOAD_BUSY) != 0) return;
    uint32_t &number = at<uint32_t>(base + NF_NEW_COUNT);
    number = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *id = names + uintptr_t(i) * ID_SIZE;
        if (!is_new(base, id, NF_NEW_COUNT, NF_NEW_ROOM)) continue;
        copy_id(at<uint8_t *>(base + NF_NEW) + number * ID_SIZE, id);
        number++;
        at<uint8_t>(base + NF_CHANGED) = 1;
    }
    if (number == 0) return;
    at<uint64_t>(base + NF_UPLOAD_BUSY) = 1;
    u3x_history_thread(base, s);
}

}  // namespace g19p2

extern "C" {

// 5.2: the new players of a match go to the account service's players history
U3X_FN void u3x_history_thread(uintptr_t base, void *s) {
    using namespace g19p2;
    const uint8_t *ids = at<uint8_t *>(base + NF_NEW);   // read once, before the title is fetched
    const char *title = u3h_text(base, s, TEXT_TITLE);   // fetched also when there is nobody to add
    // the number of new players is read again before every turn
    for (uint32_t i = 0; i < at<uint32_t>(base + NF_NEW_COUNT); i++) {
        const uint8_t *id = ids + i * ID_SIZE;
        if (id[0] == 0 || is_friend(base, id)) continue;
        count_up(u3h_pm_add(base, id, title) < 0 ? EA_HISTORY_FAILED : EA_HISTORY_ADDED);
    }
    at<uint64_t>(base + NF_UPLOAD_BUSY) = 0;
    at<uint32_t>(base + NF_NEW_COUNT) = 0;
}

// 5.3: n ids go into the players-met list (flag 0) or are the new players of a match (any other flag)
U3X_FN void u3x_players_met_add(uintptr_t base, void *s, const uint8_t *names, uint32_t n, int flag) {
    using namespace g19p2;
    if (n == 0) return;
    if (flag == 0)
        add_met(base, names, n);
    else
        add_new(base, s, names, n);
}

// 5.4: the players-met list is read back from the players history
U3X_FN void u3x_players_met_fetch(uintptr_t base, void *s) {
    using namespace g19p2;
    if (at<uint64_t>(base + NF_FETCH_BUSY) != 0 || at<uint8_t>(base + NF_CHANGED) == 0) return;
    at<uint32_t>(base + NF_MET_COUNT) = 0;
    at<uint64_t>(base + NF_FETCH_BUSY) = 1;

    const int32_t available = u3h_pm_count(base);
    uint32_t number = available < 0 ? 0 : uint32_t(available);
    const uint32_t room = at<uint32_t>(base + NF_MET_ROOM);
    if (number > room) number = room;

    for (uint32_t i = 0; i < number; i++) {
        uint8_t id[ID_SIZE];
        clear_id(id);
        u3h_pm_get(base, i, id);   // a failed read leaves the id empty, and an empty id is not listed
        u3x_players_met_add(base, s, id, 1, 0);
    }

    at<uint64_t>(base + NF_FETCH_BUSY) = 0;
    at<uint8_t>(base + NF_CHANGED) = 0;
    count_up(EA_FETCHES);
}

// 5.5: a match starts: its remote players are the new players
U3X_FN void u3x_match_players(uintptr_t base, void *s) {
    using namespace g19p2;
    if ((ext_ea()[EA_SWITCHES] & FRIENDS_OFF) != 0 || at<uint8_t *>(base + NF_ENTRIES) == nullptr) return;

    // every player record that is not a local player's gives its id, used or not
    uint8_t ids[PLAYER_RECORDS * ID_SIZE];
    uint32_t n = 0;
    if (at<uint8_t>(base + NET_MULTIPLAYER) != 0) {
        for (uint32_t i = 0; i < PLAYER_RECORDS; i++) {
            const uint8_t *record = reinterpret_cast<const uint8_t *>(base + NGM_PLAYERS + i * PLAYER_SIZE);
            if (record == at<const uint8_t *>(base + NGM_LOCAL_A) || record == at<const uint8_t *>(base + NGM_LOCAL_B))
                continue;
            copy_id(ids + uintptr_t(n) * ID_SIZE, record + PLAYER_ID);
            n++;
        }
    }

    if (at<uint8_t>(base + NET_LAN) != 0) return;
    if (list_busy(base)) u3x_friend_list_thread(base);
    u3x_players_met_add(base, s, ids, n, 1);
    count_up(EA_MATCH_LISTS);
}

// 5.6: a rebuild request and the waiting friend events of the account service
U3X_FN void u3x_friend_events(uintptr_t base) {
    using namespace g19p2;
    // the friend list is to be built again: empty it and mark it busy (a request that comes while busy is dropped)
    if (u3h_take_rebuild() != 0 && !list_busy(base)) {
        at<uint32_t>(base + NF_COUNT) = 0;
        at<uint64_t>(base + NF_LIST_BUSY) = 1;
    }

    for (int turn = 0; turn < MAX_EVENTS; turn++) {
        uint8_t id[ID_SIZE];
        clear_id(id);
        const int32_t kind = u3h_fr_event(base, id);
        if (kind < 0) return;
        if (kind > 1) continue;   // an unknown kind only uses up its turn
        if (list_busy(base)) u3x_friend_list_thread(base);
        u3x_friend_add(base, id, nullptr);   // offline (0) or presence changed (1): the friend is read again
        count_up(EA_EVENTS);
    }
}

}  // extern "C"
