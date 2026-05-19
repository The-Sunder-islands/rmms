#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rmms_generated.h"

namespace rmms::backend::core {

// ── Plain C++ data structs (not FlatBuffers-generated) ──────────────────────

struct NoteData {
    std::string id;
    std::string clip_id;
    uint8_t     key = 0;
    uint64_t    start_tick = 0;
    uint64_t    length_ticks = 0;
    uint8_t     velocity = 0;
    uint8_t     pan = 64;
};

struct ClipData {
    std::string id;
    std::string track_id;
    ClipType    type = ClipType_MIDI;
    uint64_t    start_tick = 0;
    uint64_t    length_ticks = 0;
    uint64_t    loop_start = 0;
    uint64_t    loop_end = 0;
    std::vector<NoteData> notes;
};

struct TrackData {
    std::string id;
    std::string name;
    TrackType   type = TrackType_INSTRUMENT;
    float       volume = 0.8f;
    float       pan = 0.5f;
    bool        mute = false;
    bool        solo = false;
    bool        arm = false;
    uint32_t    color = 0x808080FF;
    std::string plugin_id;
    std::string audio_url;
    std::vector<std::string> child_ids;
};

struct MixerChannelData {
    std::string id;
    std::string name;
    float       volume = 0.8f;
    float       pan = 0.5f;
    bool        mute = false;
    bool        solo = false;
};

struct ChordData {
    std::string id;
    uint64_t    tick = 0;
    uint8_t     root = 0;
    ChordType   type = ChordType_MAJOR;
    uint64_t    duration = 0;
};

struct ArrangerSectionData {
    std::string id;
    std::string name;
    uint64_t    start_tick = 0;
    uint64_t    length_ticks = 0;
    uint32_t    color = 0;
    uint32_t    repeat_count = 0;
};

struct MarkerData {
    std::string id;
    std::string name;
    uint64_t    tick = 0;
    MarkerType  type = MarkerType_POINT;
};

struct TempoPointData {
    std::string id;
    uint64_t    tick = 0;
    float       bpm = 120.0f;
};

struct HybridClipData {
    ClipData            base;
    std::string         waveform_url;
    std::vector<float>  freq_trajectories;
    std::vector<float>  amp_trajectories;
    std::vector<float>  pan_trajectories;
    std::vector<uint8_t> notes_raw;     // serialized NoteDescriptor vector
    std::vector<uint8_t> harmonics_raw; // serialized HarmonicEntry per note
};

struct PluginData {
    std::string id;
    std::string name;
    PluginCategory category = PluginCategory_EMBED;
    std::string path;
    std::string entry;
};

// ── Serialization helpers (build FlatBuffers from plain structs) ────────────

flatbuffers::Offset<Track> build_track(flatbuffers::FlatBufferBuilder& fbb,
                                       const TrackData& t);

flatbuffers::Offset<Clip> build_clip(flatbuffers::FlatBufferBuilder& fbb,
                                     const ClipData& c);

flatbuffers::Offset<Note> build_note(flatbuffers::FlatBufferBuilder& fbb,
                                     const NoteData& n);

flatbuffers::Offset<MixerChannel> build_channel(flatbuffers::FlatBufferBuilder& fbb,
                                                const MixerChannelData& ch);

flatbuffers::Offset<ChordEvent> build_chord(flatbuffers::FlatBufferBuilder& fbb,
                                            const ChordData& cd);

flatbuffers::Offset<ArrangerSection> build_arranger(flatbuffers::FlatBufferBuilder& fbb,
                                                    const ArrangerSectionData& a);

flatbuffers::Offset<Marker> build_marker(flatbuffers::FlatBufferBuilder& fbb,
                                         const MarkerData& m);

flatbuffers::Offset<TempoPoint> build_tempo_point(flatbuffers::FlatBufferBuilder& fbb,
                                                  const TempoPointData& tp);

// ── ProjectState (the single in-memory data store) ──────────────────────────

class ProjectState {
public:
    ProjectState() = default;

    struct Transport {
        TransportState state = TransportState_STOPPED;
        uint64_t       position = 0;
        float          bpm = 120.0f;
        uint32_t       time_sig_num = 4;
        uint32_t       time_sig_den = 4;
        uint64_t       loop_start = 0;
        uint64_t       loop_end = 0;
    } transport;

    struct Project {
        std::string name;
        std::string file_path;
        float       sample_rate = 44100.0f;
        bool        modified = false;
    } project;

    std::mutex mutex;

    // ── Track ───────────────────────────────────────────────────────────────
    std::string track_add(TrackType type, std::string_view name);
    bool        track_remove(std::string_view id);
    TrackData*  track_get(std::string_view id);
    const TrackData* track_get(std::string_view id) const;
    std::vector<const TrackData*> track_list() const;

    // ── Clip ────────────────────────────────────────────────────────────────
    std::string clip_add(std::string_view track_id, ClipType type,
                         uint64_t start_tick, uint64_t length_ticks);
    bool        clip_remove(std::string_view id);
    ClipData*   clip_get(std::string_view id);
    const ClipData* clip_get(std::string_view id) const;
    std::vector<const ClipData*> clip_list(std::string_view track_id) const;
    std::string clip_split(std::string_view id, uint64_t split_tick);

    // ── Note ────────────────────────────────────────────────────────────────
    std::string note_add(std::string_view clip_id, uint8_t key,
                         uint64_t start_tick, uint64_t length_ticks,
                         uint8_t velocity, uint8_t pan);
    bool        note_remove(std::string_view id);
    NoteData*   note_get(std::string_view id);
    std::vector<const NoteData*> note_list(std::string_view clip_id) const;

    // ── Mixer ───────────────────────────────────────────────────────────────
    std::string channel_add(std::string_view name);
    bool        channel_remove(std::string_view id);
    MixerChannelData* channel_get(std::string_view id);
    std::vector<const MixerChannelData*> channel_list() const;

    // ── Plugin ──────────────────────────────────────────────────────────────
    void plugin_set(const std::string& id, const std::string& name,
                    PluginCategory category, const std::string& path,
                    const std::string& entry);
    std::vector<const PluginData*> plugin_list() const;
    const PluginData* plugin_get(std::string_view id) const;
    void plugin_set_param(std::string_view id, std::string_view key,
                          std::string_view value);
    std::string plugin_get_params(std::string_view id) const;
    void plugin_set_enabled(std::string_view id, bool enabled);
    bool plugin_is_enabled(std::string_view id) const;

    // ── Chord ───────────────────────────────────────────────────────────────
    std::string chord_add(std::string_view track_id, uint64_t tick,
                          uint8_t root, ChordType type, uint64_t duration);
    bool        chord_remove(std::string_view id);
    ChordData*  chord_get(std::string_view id);
    std::vector<const ChordData*> chord_list() const;

    // ── Arranger ────────────────────────────────────────────────────────────
    std::string arranger_add(std::string_view track_id, std::string_view name,
                             uint64_t start_tick, uint64_t length_ticks,
                             uint32_t color, uint32_t repeat_count);
    bool        arranger_remove(std::string_view id);
    ArrangerSectionData* arranger_get(std::string_view id);
    std::vector<const ArrangerSectionData*> arranger_list() const;

    // ── Marker ──────────────────────────────────────────────────────────────
    std::string marker_add(std::string_view track_id, std::string_view name,
                           uint64_t tick, MarkerType type);
    bool        marker_remove(std::string_view id);
    MarkerData* marker_get(std::string_view id);
    std::vector<const MarkerData*> marker_list() const;

    // ── Tempo ───────────────────────────────────────────────────────────────
    std::string tempo_add(std::string_view track_id, uint64_t tick, float bpm);
    bool        tempo_remove(std::string_view id);
    TempoPointData* tempo_get(std::string_view id);
    std::vector<const TempoPointData*> tempo_list() const;

    // ── Hybrid ──────────────────────────────────────────────────────────────
    HybridClipData* hybrid_get(std::string_view clip_id);
    const HybridClipData* hybrid_get(std::string_view clip_id) const;
    void hybrid_ensure(std::string_view clip_id);
    void hybrid_remove(std::string_view clip_id);

    // ── UUID ────────────────────────────────────────────────────────────────
    static std::string uuid();

private:
    template<typename T>
    using Store = std::unordered_map<std::string, std::unique_ptr<T>>;

    Store<TrackData>              m_tracks;
    Store<ClipData>               m_clips;
    Store<NoteData>               m_notes;
    Store<MixerChannelData>       m_channels;
    Store<PluginData>             m_plugins;
    std::unordered_map<std::string, bool> m_plugin_enabled;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_plugin_params;
    Store<ChordData>              m_chords;
    Store<ArrangerSectionData>    m_arrangers;
    Store<MarkerData>             m_markers;
    Store<TempoPointData>         m_tempos;
    Store<HybridClipData>         m_hybrids;
};

}  // namespace rmms::backend::core
