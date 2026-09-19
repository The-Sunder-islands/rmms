#include "core/in_memory_project_state.h"

#include <algorithm>
#include <array>
#include <flatbuffers/flatbuffers.h>
#include <iomanip>
#include <random>
#include <sstream>

namespace rmms::backend::core {

void InMemoryProjectState::transport_play() {
    m_transport.state = rmms::TransportState_PLAYING;
}

void InMemoryProjectState::transport_stop() {
    m_transport.state = rmms::TransportState_STOPPED;
}

void InMemoryProjectState::transport_pause() {
    m_transport.state = rmms::TransportState_PAUSED;
}

void InMemoryProjectState::transport_set_position(uint64_t tick) {
    m_transport.position = tick;
}

void InMemoryProjectState::transport_set_tempo(float bpm) {
    m_transport.bpm = bpm;
}

std::string InMemoryProjectState::uuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    std::array<uint64_t, 2> p = { dis(gen), dis(gen) };
    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8) << ((p[0] >> 32) & 0xFFFFFFFF) << '-'
        << std::setw(4) << ((p[0] >> 16) & 0xFFFF) << '-'
        << std::setw(4) << (0x4000 | ((p[0] >> 0) & 0x0FFF)) << '-'
        << std::setw(4) << (0x8000 | ((p[1] >> 48) & 0x3FFF)) << '-'
        << std::setw(12) << (p[1] & 0xFFFFFFFFFFFF);
    return oss.str();
}

flatbuffers::Offset<Track> build_track(flatbuffers::FlatBufferBuilder& fbb, const TrackData& t) {
    auto id = fbb.CreateString(t.id);
    auto name = fbb.CreateString(t.name);
    auto plugin_id = fbb.CreateString(t.plugin_id);
    auto audio_url = fbb.CreateString(t.audio_url);
    std::vector<flatbuffers::Offset<flatbuffers::String>> child_offsets;
    for (auto& c : t.child_ids) child_offsets.push_back(fbb.CreateString(c));
    auto child_ids = fbb.CreateVector(child_offsets);
    return CreateTrack(fbb, id, name, t.type, t.volume, t.pan,
                       t.mute, t.solo, t.arm, t.color,
                       plugin_id, audio_url, child_ids);
}

flatbuffers::Offset<Clip> build_clip(flatbuffers::FlatBufferBuilder& fbb, const ClipData& c) {
    auto id = fbb.CreateString(c.id);
    auto track_id = fbb.CreateString(c.track_id);
    return CreateClip(fbb, id, track_id, c.type,
                      c.start_tick, c.length_ticks, c.loop_start, c.loop_end);
}

flatbuffers::Offset<Note> build_note(flatbuffers::FlatBufferBuilder& fbb, const NoteData& n) {
    auto id = fbb.CreateString(n.id);
    auto clip_id = fbb.CreateString(n.clip_id);
    return CreateNote(fbb, id, clip_id, n.key,
                      n.start_tick, n.length_ticks, n.velocity, n.pan);
}

flatbuffers::Offset<MixerChannel> build_channel(flatbuffers::FlatBufferBuilder& fbb,
                                                const MixerChannelData& ch) {
    auto id = fbb.CreateString(ch.id);
    auto name = fbb.CreateString(ch.name);
    std::vector<flatbuffers::Offset<flatbuffers::String>> fx;
    auto effects = fbb.CreateVector(fx);
    return CreateMixerChannel(fbb, id, name, ch.volume, ch.pan,
                              ch.mute, ch.solo, effects);
}

flatbuffers::Offset<ChordEvent> build_chord(flatbuffers::FlatBufferBuilder& fbb,
                                            const ChordData& cd) {
    auto id = fbb.CreateString(cd.id);
    return CreateChordEvent(fbb, id, cd.tick, cd.root, cd.type, cd.duration);
}

flatbuffers::Offset<ArrangerSection> build_arranger(flatbuffers::FlatBufferBuilder& fbb,
                                                    const ArrangerSectionData& a) {
    auto id = fbb.CreateString(a.id);
    auto name = fbb.CreateString(a.name);
    return CreateArrangerSection(fbb, id, name, a.start_tick, a.length_ticks,
                                 a.color, a.repeat_count);
}

flatbuffers::Offset<Marker> build_marker(flatbuffers::FlatBufferBuilder& fbb,
                                         const MarkerData& m) {
    auto id = fbb.CreateString(m.id);
    auto name = fbb.CreateString(m.name);
    return CreateMarker(fbb, id, name, m.tick, m.type);
}

flatbuffers::Offset<TempoPoint> build_tempo_point(flatbuffers::FlatBufferBuilder& fbb,
                                                  const TempoPointData& tp) {
    auto id = fbb.CreateString(tp.id);
    return CreateTempoPoint(fbb, id, tp.tick, tp.bpm);
}

// Track
std::string InMemoryProjectState::track_add(TrackType type, std::string_view name) {
    auto t = std::make_unique<TrackData>();
    t->id = uuid(); t->name = std::string(name); t->type = type;
    auto id = t->id;
    m_tracks[id] = std::move(t);
    return id;
}
bool InMemoryProjectState::track_remove(std::string_view id) {
    std::string sid(id);
    m_clips.erase(sid); m_notes.erase(sid);
    return m_tracks.erase(sid) > 0;
}
TrackData* InMemoryProjectState::track_get(std::string_view id) {
    auto it = m_tracks.find(std::string(id));
    return it != m_tracks.end() ? it->second.get() : nullptr;
}
const TrackData* InMemoryProjectState::track_get(std::string_view id) const {
    auto it = m_tracks.find(std::string(id));
    return it != m_tracks.end() ? it->second.get() : nullptr;
}
std::vector<const TrackData*> InMemoryProjectState::track_list() const {
    std::vector<const TrackData*> list;
    for (auto& [id, t] : m_tracks) list.push_back(t.get());
    return list;
}

// Clip
std::string InMemoryProjectState::clip_add(std::string_view track_id, ClipType type,
                                   uint64_t start_tick, uint64_t length_ticks) {
    if (!track_get(track_id)) return {};
    auto c = std::make_unique<ClipData>();
    c->id = uuid(); c->track_id = std::string(track_id); c->type = type;
    c->start_tick = start_tick; c->length_ticks = length_ticks;
    auto id = c->id;
    m_clips[id] = std::move(c);
    return id;
}
bool InMemoryProjectState::clip_remove(std::string_view id) {
    std::string sid(id);
    m_notes.erase(sid); m_hybrids.erase(sid);
    return m_clips.erase(sid) > 0;
}
ClipData* InMemoryProjectState::clip_get(std::string_view id) {
    auto it = m_clips.find(std::string(id));
    return it != m_clips.end() ? it->second.get() : nullptr;
}
const ClipData* InMemoryProjectState::clip_get(std::string_view id) const {
    auto it = m_clips.find(std::string(id));
    return it != m_clips.end() ? it->second.get() : nullptr;
}
std::vector<const ClipData*> InMemoryProjectState::clip_list(std::string_view track_id) const {
    std::vector<const ClipData*> list;
    for (auto& [id, c] : m_clips)
        if (c->track_id == track_id) list.push_back(c.get());
    return list;
}
std::string InMemoryProjectState::clip_split(std::string_view id, uint64_t split_tick) {
    auto* c = clip_get(id);
    if (!c || split_tick <= c->start_tick ||
        split_tick >= c->start_tick + c->length_ticks) return {};
    uint64_t left_len = split_tick - c->start_tick;
    uint64_t right_len = c->length_ticks - left_len;
    c->length_ticks = left_len;
    auto nc = std::make_unique<ClipData>();
    nc->id = uuid(); nc->track_id = c->track_id; nc->type = c->type;
    nc->start_tick = split_tick; nc->length_ticks = right_len;
    auto new_id = nc->id;
    m_clips[new_id] = std::move(nc);
    return new_id;
}

// Note
std::string InMemoryProjectState::note_add(std::string_view clip_id, uint8_t key,
                                   uint64_t start_tick, uint64_t length_ticks,
                                   uint8_t velocity, uint8_t pan) {
    if (!clip_get(clip_id)) return {};
    auto n = std::make_unique<NoteData>();
    n->id = uuid(); n->clip_id = std::string(clip_id); n->key = key;
    n->start_tick = start_tick; n->length_ticks = length_ticks;
    n->velocity = velocity; n->pan = pan;
    auto id = n->id;
    m_notes[id] = std::move(n);
    return id;
}
bool InMemoryProjectState::note_remove(std::string_view id) { return m_notes.erase(std::string(id)) > 0; }
NoteData* InMemoryProjectState::note_get(std::string_view id) {
    auto it = m_notes.find(std::string(id));
    return it != m_notes.end() ? it->second.get() : nullptr;
}
std::vector<const NoteData*> InMemoryProjectState::note_list(std::string_view clip_id) const {
    std::vector<const NoteData*> list;
    for (auto& [id, n] : m_notes) if (n->clip_id == clip_id) list.push_back(n.get());
    return list;
}

// Mixer
std::string InMemoryProjectState::channel_add(std::string_view name) {
    auto ch = std::make_unique<MixerChannelData>();
    ch->id = uuid(); ch->name = std::string(name);
    auto id = ch->id;
    m_channels[id] = std::move(ch);
    return id;
}
bool InMemoryProjectState::channel_remove(std::string_view id) { return m_channels.erase(std::string(id)) > 0; }
MixerChannelData* InMemoryProjectState::channel_get(std::string_view id) {
    auto it = m_channels.find(std::string(id));
    return it != m_channels.end() ? it->second.get() : nullptr;
}
std::vector<const MixerChannelData*> InMemoryProjectState::channel_list() const {
    std::vector<const MixerChannelData*> list;
    for (auto& [id, ch] : m_channels) list.push_back(ch.get());
    return list;
}

// Plugin
void InMemoryProjectState::plugin_set(const std::string& id, const std::string& name,
                              PluginCategory category, const std::string& path,
                              const std::string& entry) {
    auto p = std::make_unique<PluginData>();
    p->id = id; p->name = name; p->category = category;
    p->path = path; p->entry = entry;
    m_plugins[id] = std::move(p);
}
std::vector<const PluginData*> InMemoryProjectState::plugin_list() const {
    std::vector<const PluginData*> list;
    for (auto& [id, p] : m_plugins) list.push_back(p.get());
    return list;
}
const PluginData* InMemoryProjectState::plugin_get(std::string_view id) const {
    auto it = m_plugins.find(std::string(id));
    return it != m_plugins.end() ? it->second.get() : nullptr;
}
void InMemoryProjectState::plugin_set_param(std::string_view id, std::string_view key, std::string_view val) {
    m_plugin_params[std::string(id)][std::string(key)] = std::string(val);
}
std::string InMemoryProjectState::plugin_get_params(std::string_view id) const {
    auto it = m_plugin_params.find(std::string(id));
    if (it == m_plugin_params.end()) return "{}";
    std::string json = "{"; bool first = true;
    for (auto& [k, v] : it->second) {
        if (!first) json += ",";
        json += "\"" + k + "\":\"" + v + "\"";
        first = false;
    }
    json += "}"; return json;
}
void InMemoryProjectState::plugin_set_enabled(std::string_view id, bool e) { m_plugin_enabled[std::string(id)] = e; }
bool InMemoryProjectState::plugin_is_enabled(std::string_view id) const {
    auto it = m_plugin_enabled.find(std::string(id));
    return it != m_plugin_enabled.end() ? it->second : false;
}

// Chord
std::string InMemoryProjectState::chord_add(std::string_view track_id, uint64_t tick,
                                    uint8_t root, ChordType type, uint64_t duration) {
    if (!track_get(track_id)) return {};
    auto cd = std::make_unique<ChordData>();
    cd->id = uuid(); cd->tick = tick; cd->root = root;
    cd->type = type; cd->duration = duration;
    auto id = cd->id;
    m_chords[id] = std::move(cd);
    return id;
}
bool InMemoryProjectState::chord_remove(std::string_view id) { return m_chords.erase(std::string(id)) > 0; }
ChordData* InMemoryProjectState::chord_get(std::string_view id) {
    auto it = m_chords.find(std::string(id));
    return it != m_chords.end() ? it->second.get() : nullptr;
}
std::vector<const ChordData*> InMemoryProjectState::chord_list() const {
    std::vector<const ChordData*> list;
    for (auto& [id, cd] : m_chords) list.push_back(cd.get());
    return list;
}

// Arranger
std::string InMemoryProjectState::arranger_add(std::string_view track_id, std::string_view name,
                                       uint64_t start_tick, uint64_t length_ticks,
                                       uint32_t color, uint32_t repeat_count) {
    if (!track_get(track_id)) return {};
    auto a = std::make_unique<ArrangerSectionData>();
    a->id = uuid(); a->name = std::string(name);
    a->start_tick = start_tick; a->length_ticks = length_ticks;
    a->color = color; a->repeat_count = repeat_count;
    auto id = a->id;
    m_arrangers[id] = std::move(a);
    return id;
}
bool InMemoryProjectState::arranger_remove(std::string_view id) { return m_arrangers.erase(std::string(id)) > 0; }
ArrangerSectionData* InMemoryProjectState::arranger_get(std::string_view id) {
    auto it = m_arrangers.find(std::string(id));
    return it != m_arrangers.end() ? it->second.get() : nullptr;
}
std::vector<const ArrangerSectionData*> InMemoryProjectState::arranger_list() const {
    std::vector<const ArrangerSectionData*> list;
    for (auto& [id, a] : m_arrangers) list.push_back(a.get());
    return list;
}

// Marker
std::string InMemoryProjectState::marker_add(std::string_view track_id, std::string_view name,
                                     uint64_t tick, MarkerType type) {
    if (!track_get(track_id)) return {};
    auto m = std::make_unique<MarkerData>();
    m->id = uuid(); m->name = std::string(name);
    m->tick = tick; m->type = type;
    auto id = m->id;
    m_markers[id] = std::move(m);
    return id;
}
bool InMemoryProjectState::marker_remove(std::string_view id) { return m_markers.erase(std::string(id)) > 0; }
MarkerData* InMemoryProjectState::marker_get(std::string_view id) {
    auto it = m_markers.find(std::string(id));
    return it != m_markers.end() ? it->second.get() : nullptr;
}
std::vector<const MarkerData*> InMemoryProjectState::marker_list() const {
    std::vector<const MarkerData*> list;
    for (auto& [id, m] : m_markers) list.push_back(m.get());
    return list;
}

// Tempo
std::string InMemoryProjectState::tempo_add(std::string_view track_id, uint64_t tick, float bpm) {
    if (!track_get(track_id)) return {};
    auto tp = std::make_unique<TempoPointData>();
    tp->id = uuid(); tp->tick = tick; tp->bpm = bpm;
    auto id = tp->id;
    m_tempos[id] = std::move(tp);
    return id;
}
bool InMemoryProjectState::tempo_remove(std::string_view id) { return m_tempos.erase(std::string(id)) > 0; }
TempoPointData* InMemoryProjectState::tempo_get(std::string_view id) {
    auto it = m_tempos.find(std::string(id));
    return it != m_tempos.end() ? it->second.get() : nullptr;
}
std::vector<const TempoPointData*> InMemoryProjectState::tempo_list() const {
    std::vector<const TempoPointData*> list;
    for (auto& [id, tp] : m_tempos) list.push_back(tp.get());
    return list;
}

// Hybrid
HybridClipData* InMemoryProjectState::hybrid_get(std::string_view clip_id) {
    auto it = m_hybrids.find(std::string(clip_id));
    return it != m_hybrids.end() ? it->second.get() : nullptr;
}
const HybridClipData* InMemoryProjectState::hybrid_get(std::string_view clip_id) const {
    auto it = m_hybrids.find(std::string(clip_id));
    return it != m_hybrids.end() ? it->second.get() : nullptr;
}
void InMemoryProjectState::hybrid_ensure(std::string_view clip_id) {
    std::string sid(clip_id);
    if (!m_hybrids.count(sid)) {
        auto h = std::make_unique<HybridClipData>();
        if (auto* c = clip_get(clip_id)) h->base = *c;
        m_hybrids[sid] = std::move(h);
    }
}
void InMemoryProjectState::hybrid_remove(std::string_view clip_id) { m_hybrids.erase(std::string(clip_id)); }

}  // namespace rmms::backend::core
