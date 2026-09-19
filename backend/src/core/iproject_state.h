#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/project_types.h"

namespace rmms::backend::core {

// Transport snapshot, shared by all implementations.
struct TransportState {
    rmms::TransportState state = rmms::TransportState_STOPPED;
    uint64_t       position = 0;
    float          bpm = 120.0f;
    uint32_t       time_sig_num = 4;
    uint32_t       time_sig_den = 4;
    uint64_t       loop_start = 0;
    uint64_t       loop_end = 0;
};

struct ProjectInfo {
    std::string name;
    std::string file_path;
    float       sample_rate = 44100.0f;
    bool        modified = false;
};

// Abstraction over the project data the protocol handlers operate on.
//
// Implementations:
//   - InMemoryProjectState: standalone reference store (mock backend, CI).
//   - LmmsProjectState (LMMS side): adapter onto a live lmms::Song, all
//     access marshalled to the GUI thread via IMainThreadDispatcher.
//
// Getters returning pointers may point into implementation-owned storage;
// callers must not hold them across handler invocations. Handlers execute
// serialized by the protocol server, which is what makes that safe.
class IProjectState {
public:
    virtual ~IProjectState() = default;

    virtual TransportState& transport() = 0;
    virtual const TransportState& transport() const = 0;
    virtual ProjectInfo& project() = 0;
    virtual const ProjectInfo& project() const = 0;

    // Transport commands. Engines execute them (e.g. Song::playSong()); the
    // in-memory implementation just updates its snapshot fields. Handlers must
    // go through these instead of writing transport() fields directly,
    // otherwise an engine-backed adapter can never observe the command.
    virtual void transport_play() = 0;
    virtual void transport_stop() = 0;
    virtual void transport_pause() = 0;
    virtual void transport_set_position(uint64_t tick) = 0;
    virtual void transport_set_tempo(float bpm) = 0;

    // ── Track ───────────────────────────────────────────────────────────────
    virtual std::string track_add(TrackType type, std::string_view name) = 0;
    virtual bool        track_remove(std::string_view id) = 0;
    virtual TrackData*  track_get(std::string_view id) = 0;
    virtual const TrackData* track_get(std::string_view id) const = 0;
    virtual std::vector<const TrackData*> track_list() const = 0;

    // Track mutations. Engines implement these; handlers must never write
    // through track_get()'s pointer, or an engine adapter cannot observe it.
    virtual bool track_set_name(std::string_view id, std::string_view name) = 0;
    virtual bool track_set_volume(std::string_view id, float volume) = 0;
    virtual bool track_set_pan(std::string_view id, float pan) = 0;
    virtual bool track_set_mute(std::string_view id, bool mute) = 0;
    virtual bool track_set_solo(std::string_view id, bool solo) = 0;
    virtual bool track_set_arm(std::string_view id, bool arm) = 0;
    virtual bool track_set_color(std::string_view id, uint32_t color) = 0;

    // ── Clip ────────────────────────────────────────────────────────────────
    virtual std::string clip_add(std::string_view track_id, ClipType type,
                                 uint64_t start_tick, uint64_t length_ticks) = 0;
    virtual bool        clip_remove(std::string_view id) = 0;
    virtual ClipData*   clip_get(std::string_view id) = 0;
    virtual const ClipData* clip_get(std::string_view id) const = 0;
    virtual std::vector<const ClipData*> clip_list(std::string_view track_id) const = 0;
    virtual std::string clip_split(std::string_view id, uint64_t split_tick) = 0;
    virtual bool clip_move(std::string_view id, std::string_view track_id,
                           uint64_t start_tick) = 0;
    virtual bool clip_resize(std::string_view id, uint64_t length_ticks) = 0;
    virtual bool clip_set_loop(std::string_view id, uint64_t loop_start,
                               uint64_t loop_end) = 0;

    // ── Note ────────────────────────────────────────────────────────────────
    virtual std::string note_add(std::string_view clip_id, uint8_t key,
                                 uint64_t start_tick, uint64_t length_ticks,
                                 uint8_t velocity, uint8_t pan) = 0;
    virtual bool        note_remove(std::string_view id) = 0;
    virtual NoteData*   note_get(std::string_view id) = 0;
    virtual std::vector<const NoteData*> note_list(std::string_view clip_id) const = 0;
    virtual bool note_move(std::string_view id, uint8_t key, uint64_t start_tick) = 0;
    virtual bool note_set_length(std::string_view id, uint64_t length_ticks) = 0;
    virtual bool note_set_velocity(std::string_view id, uint8_t velocity) = 0;

    // ── Mixer ───────────────────────────────────────────────────────────────
    virtual std::string channel_add(std::string_view name) = 0;
    virtual bool        channel_remove(std::string_view id) = 0;
    virtual MixerChannelData* channel_get(std::string_view id) = 0;
    virtual std::vector<const MixerChannelData*> channel_list() const = 0;
    virtual bool channel_set_volume(std::string_view id, float volume) = 0;
    virtual bool channel_set_pan(std::string_view id, float pan) = 0;
    virtual bool channel_set_route(std::string_view src_id, std::string_view dst_id,
                                   float gain) = 0;

    // ── Plugin ──────────────────────────────────────────────────────────────
    virtual void plugin_set(const std::string& id, const std::string& name,
                            PluginCategory category, const std::string& path,
                            const std::string& entry) = 0;
    virtual std::vector<const PluginData*> plugin_list() const = 0;
    virtual const PluginData* plugin_get(std::string_view id) const = 0;
    virtual void plugin_set_param(std::string_view id, std::string_view key,
                                  std::string_view value) = 0;
    virtual std::string plugin_get_params(std::string_view id) const = 0;
    virtual void plugin_set_enabled(std::string_view id, bool enabled) = 0;
    virtual bool plugin_is_enabled(std::string_view id) const = 0;

    // ── Chord ───────────────────────────────────────────────────────────────
    virtual std::string chord_add(std::string_view track_id, uint64_t tick,
                                  uint8_t root, ChordType type, uint64_t duration) = 0;
    virtual bool        chord_remove(std::string_view id) = 0;
    virtual ChordData*  chord_get(std::string_view id) = 0;
    virtual std::vector<const ChordData*> chord_list() const = 0;

    // ── Arranger ────────────────────────────────────────────────────────────
    virtual std::string arranger_add(std::string_view track_id, std::string_view name,
                                     uint64_t start_tick, uint64_t length_ticks,
                                     uint32_t color, uint32_t repeat_count) = 0;
    virtual bool        arranger_remove(std::string_view id) = 0;
    virtual ArrangerSectionData* arranger_get(std::string_view id) = 0;
    virtual std::vector<const ArrangerSectionData*> arranger_list() const = 0;

    // ── Marker ──────────────────────────────────────────────────────────────
    virtual std::string marker_add(std::string_view track_id, std::string_view name,
                                   uint64_t tick, MarkerType type) = 0;
    virtual bool        marker_remove(std::string_view id) = 0;
    virtual MarkerData* marker_get(std::string_view id) = 0;
    virtual std::vector<const MarkerData*> marker_list() const = 0;

    // ── Tempo ───────────────────────────────────────────────────────────────
    virtual std::string tempo_add(std::string_view track_id, uint64_t tick, float bpm) = 0;
    virtual bool        tempo_remove(std::string_view id) = 0;
    virtual TempoPointData* tempo_get(std::string_view id) = 0;
    virtual std::vector<const TempoPointData*> tempo_list() const = 0;

    // ── Hybrid ──────────────────────────────────────────────────────────────
    virtual HybridClipData* hybrid_get(std::string_view clip_id) = 0;
    virtual const HybridClipData* hybrid_get(std::string_view clip_id) const = 0;
    virtual void hybrid_ensure(std::string_view clip_id) = 0;
    virtual void hybrid_remove(std::string_view clip_id) = 0;

    // ── Project commands ────────────────────────────────────────────────────
    // Empty path = save to the current project file.
    virtual bool project_save(std::string_view path) = 0;
};

}  // namespace rmms::backend::core
