// dctools/ps4actor.hpp: PS3 -> PS4 Uncharted 3 foreground actor paks (characters, accessories, props); the actor
// toolset's ps4actor.py (and the post passes of its worker) and the actor-materials variant ps4actor_am.py in C++.
//
// The geometry comes from the PS3 pak (FOREGROUND_SECTION_2 prototypes, blocks merged per material run); materials,
// compiled effects (copied as one span at the template's page offset) and the joint-name table come from a PS4 template
// pak of the same skeleton. One material per template material in the template's order (then a copy of the template
// material for every further PS3 material on a shared one), each re-bound to the converted PS3 textures (by texture
// kind: -color, -normal, -spec-mask, -trans, ...; of two cooks of one texture the material's own, the larger; donor
// paks for textures the PS3 pak lacks; else a neutral flat texture of the slot's kind, never the template's own). A
// PS3 material with a cut-out map takes a template material with a cut-out map slot. PS3 textures keep every mip
// unless a retail texture dictionary streams them. Props with their own skeleton get the PS3 JOINT_HIERARCHY, ANIMs
// and COLLISION_DATA_FOREGROUND converted (own_skel) or the template's grafted (graft).
//
// The older actor-materials rules (Options::am, the character paks of steps actor-materials / hat-colour /
// airport-igc2): one material per PS3 material, named '<path>:<name>,00000000-0000-0000-0000-<string id>,<shader>',
// on the template material am_choice() picked; textures from the PS3 material table (plus their -normal / -spec-mask
// siblings); the effects after the texture table (page phase only); a page limit of 0x1c0000 and no entry cap; the
// texture cap for 2D textures only; no stream filling, no vertex colour format, prototype cbuffer capacities 2 / 1.
#pragma once
#include "ndpak.hpp"
#include "ps4writer.hpp"

#include <set>

namespace u3::actor {

struct Options {   // the converter's PS4ACTOR_* settings
    bool sb_exact = false, sb_drop = false, fill_streams = true, spread_mats = false, no_tmpl_tex = false;
    int cb3_min = 4;
    std::string drop_mat;
    std::vector<fs::path> donors;
    std::optional<fs::path> skel3;
    fs::path texdict;   // the retail TextureDictionaries.csv: only the textures it streams lose their top mips to max_tex
    // the actor-materials rules
    bool am = false;
    std::map<std::string, std::string> mat_choice;                       // PS3 material -> part of a template material name
    std::map<std::pair<uint32_t, size_t>, std::string> basis_targets;   // blocks (vertices, triangles) whose lost normals and
                                                                        // tangents are rebuilt; every one must be met
};
struct Kw {   // convert()'s keyword arguments
    std::optional<std::string> rename;
    int stream_flags = 3, max_tex = 0;
    bool lod0_only = false, graft = false, mat_by_file = false, match_streams = false, own_skel = false;
    std::optional<fs::path> skel_tmpl;
};
Bytes convert(const fs::path &ps3, const fs::path &tmpl, const Kw &kw, const Options &o);

// whether a retail PS4 texture dictionary (TextureDictionaries.csv) streams the texture of this full cook path (with or
// without '.ndb'): such a texture gets its top mips from the dictionary, any other one is drawn only with what the pak holds
bool texdict_streams(const fs::path &csv, const std::string &full);

// The template for a PS3 pak one of whose drawn materials has a cut-out map (-trans / -alpha / -opacity) when the job's
// template has no template material that takes one (a transparency or hair alpha feature with a transparency / hair
// alpha map slot, no second UV set): the retail actor of retail_dir on the same skeleton (FOREGROUND_SECTION_2 skeleton
// and hierarchy ids; own skeleton props: the same hierarchy id) that has one and keeps what the job's template offers
// the PS3 materials (an instance tint material for tint masks). Of those: a shadow blocker as the template had, the most
// of the template's materials (by effect hash), a cut-out material of the kind the PS3 ones are (hair / other), an
// opaque material for opaque PS3 ones, the fewest materials, in that order (name order breaks ties). Otherwise tmpl.
fs::path transparency_template(const fs::path &ps3, const fs::path &tmpl, const fs::path &retail_dir, bool own_skel,
                               const std::optional<fs::path> &skel_tmpl = std::nullopt);

// the worker's post passes: cubefix (PS3 cube maps on retail-copied descs), cb3fix (prototype scope-3 cbuffer
// capacity), flatfix (the generated flat-color texture sRGB), cubetype (retail cube descs type 2)
Bytes cb3fix(const Bytes &d, uint32_t cap_min = 8);
Bytes vdesc_fix(const Bytes &d, const std::string &what);

// a retail PS4 actor pak under another name (MP twins whose PS3 data equals a PS4 campaign actor): every page is kept,
// renamed strings are appended to the page that held the original and the pointers to them redirected. Renamed: names
// without '/' or ',' in which OLD stands as a whole token (followed by the end, '.' or '-lod'), except inside sound bank,
// texture and effect tables; the PACKAGE_ITEM_LOOKUP id and the ARTGROUP_DATA buckets (re-hashed) follow.
Bytes rename(const fs::path &ps4, const std::string &old_name, const std::string &new_name);

// the actor-materials choice of template materials for the PS3 materials without an exact match: by role (eyes, mouth,
// hair, head / arm skin, cloth), then the most texture kinds in common with the fewest missing; tint: a PS3 material
// with a tint mask gets a template material that applies the instance tint, one without does not; overlay: no
// template materials with OVERLAY / UV_3 features (they need a second UV set)
std::map<std::string, std::string> am_choice(const fs::path &ps3, const fs::path &tmpl, bool tint, bool overlay);

// EFFECT_TABLE and every item it reaches (EFFECTs, RAW_DATA) of a retail PS4 pak, as one RawSpan to copy (page-0 phase
// kept modulo 0x1000), and those items in page order
std::pair<std::shared_ptr<struct RawSpan>, std::vector<PakItem>> extract_effect_span(const Pak &ref);


// ---- pieces shared with the other actor builders (wall-emblem/make_emblem.cpp)
inline constexpr uint32_t PAD32 = 0xaaaaaaaa;
inline constexpr size_t PAGE_LIMIT = 0x180000;   // (a margin: tables filled after item() grow past the estimate)

// a template material of a PS4 pak's GEOMETRY_1 (name, shader, features, constants, texture bindings)
struct TMat {
    std::string name, shader, hash;
    std::vector<std::string> feats;
    struct Param {
        std::string name;
        uint32_t cnt;
        Bytes vals;
    };
    std::vector<Param> params;
    struct Bind {
        std::string pname, shrt, full;
    };
    std::vector<Bind> binds;
    uint32_t x3, tail;
};
std::vector<TMat> tmpl_materials(const Pak &q);

// BC1 texels (PS3 linear order, all mips) of a constant colour (clear: alpha 0), and the mip count
std::pair<Bytes, uint32_t> bc1_flat(uint32_t w, uint32_t h, const uint32_t rgb[3], bool clear = false);
// numpy float64 -> float16 (round half to even, overflow to inf, NaN payload kept)
uint16_t f16b(double v);
// template item data as a Blob; its pointers must stay inside the item
std::shared_ptr<Blob> copy_item_blob(const Pak &T, const PakItem &it);

// a Page.item / span look-alike that starts a new page when the current one fills up; the effect span is pinned to
// the template's page-0 offset, so page 0 may not get a longer entry table than the room before it
struct AutoPages {
    PakWriter &w;
    Page *cur;
    size_t size = 0x200;
    std::optional<size_t> max_entries;
    size_t nent = 0, limit;
    AutoPages(PakWriter &w_, std::optional<size_t> me, size_t lim = PAGE_LIMIT) : w(w_), cur(&w_.page()), max_entries(me), limit(lim) {}
    void new_page() {
        cur = &w.page();
        size = 0x200;
        max_entries.reset();
        nent = 0;
    }
    void room(size_t n, size_t ne) {
        if (size + n > limit && size > 0x200) new_page();
        if (max_entries && nent + ne > *max_entries) new_page();
        size += n;
        nent += ne;
    }
    std::shared_ptr<PakWItem> item(const std::string &type, const std::shared_ptr<Blob> &data = nullptr, const std::string &name = "", bool listed = true) {
        size_t est = (data ? data->size() : 0) + 0x40 + type.size() + name.size();
        if (!data || data->size() < 0x100) est += 0x400;   // items filled in later (tables) stay small
        room((est + 15) & ~size_t(15), listed ? 1 : 0);
        return cur->item(type, data, name, listed);
    }
    void span(const std::shared_ptr<RawSpan> &s) {
        room(s->data.size() + 0x1000, s->entries.size());
        cur->span(s);
    }
};

}  // namespace u3::actor
