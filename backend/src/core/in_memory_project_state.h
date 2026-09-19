#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/iproject_state.h"

namespace rmms::backend::core {

// Standalone reference implementation of IProjectState: a plain in-memory
// store with no engine dependency. Used by the mock backend and the protocol
// test-suite; production builds use the LMMS adapter instead.
class InMemoryProjectState : public IProjectState {
public:
    InMemoryProjectState() = default;

    TransportState& transport() override { return m_transport; }
    const TransportState& transport() const override { return m_transport; }
    ProjectInfo& project() override { return m_project; }
    const ProjectInfo& project() const override { return m_project; }

    void transport_play() override;
    void transport_stop() override;
    void transport_pause() override;
    void transport_set_position(uint64_t tick) override;
    void transport_set_tempo(float bpm) override;

    // ── Track ───────────────────────────────────────────────────────────────
    std::string track_add(TrackType type, std::string_view name) override;
    bool        track_remove(std::string_view id) override;
    TrackData*  track_get(std::string_view id) override;
    const TrackData* track_get(std::string_view id) const override;
    std::vector<const TrackData*> track_list() const override;

    // ── Clip ────────────────────────────────────────────────────────────────
    std::string clip_add(std::string_view track_id, ClipType type,
                         uint64_t start_tick, uint64_t length_ticks) override;
    bool        clip_remove(std::string_view id) override;
    ClipData*   clip_get(std::string_view id) override;
    const ClipData* clip_get(std::string_view id) const override;
    std::vector<const ClipData*> clip_list(std::string_view track_id) const override;
    std::string clip_split(std::string_view id, uint64_t split_tick) override;

    // ── Note ────────────────────────────────────────────────────────────────
    std::string note_add(std::string_view clip_id, uint8_t key,
                         uint64_t start_tick, uint64_t length_ticks,
                         uint8_t velocity, uint8_t pan) override;
    bool        note_remove(std::string_view id) override;
    NoteData*   note_get(std::string_view id) override;
    std::vector<const NoteData*> note_list(std::string_view clip_id) const override;

    // ── Mixer ───────────────────────────────────────────────────────────────
    std::string channel_add(std::string_view name) override;
    bool        channel_remove(std::string_view id) override;
    MixerChannelData* channel_get(std::string_view id) override;
    std::vector<const MixerChannelData*> channel_list() const override;

    // ── Plugin ──────────────────────────────────────────────────────────────
    void plugin_set(const std::string& id, const std::string& name,
                    PluginCategory category, const std::string& path,
                    const std::string& entry) override;
    std::vector<const PluginData*> plugin_list() const override;
    const PluginData* plugin_get(std::string_view id) const override;
    void plugin_set_param(std::string_view id, std::string_view key,
                          std::string_view value) override;
    std::string plugin_get_params(std::string_view id) const override;
    void plugin_set_enabled(std::string_view id, bool enabled) override;
    bool plugin_is_enabled(std::string_view id) const override;

    // ── Chord ───────────────────────────────────────────────────────────────
    std::string chord_add(std::string_view track_id, uint64_t tick,
                          uint8_t root, ChordType type, uint64_t duration) override;
    bool        chord_remove(std::string_view id) override;
    ChordData*  chord_get(std::string_view id) override;
    std::vector<const ChordData*> chord_list() const override;

    // ── Arranger ────────────────────────────────────────────────────────────
    std::string arranger_add(std::string_view track_id, std::string_view name,
                             uint64_t start_tick, uint64_t length_ticks,
                             uint32_t color, uint32_t repeat_count) override;
    bool        arranger_remove(std::string_view id) override;
    ArrangerSectionData* arranger_get(std::string_view id) override;
    std::vector<const ArrangerSectionData*> arranger_list() const override;

    // ── Marker ──────────────────────────────────────────────────────────────
    std::string marker_add(std::string_view track_id, std::string_view name,
                           uint64_t tick, MarkerType type) override;
    bool        marker_remove(std::string_view id) override;
    MarkerData* marker_get(std::string_view id) override;
    std::vector<const MarkerData*> marker_list() const override;

    // ── Tempo ───────────────────────────────────────────────────────────────
    std::string tempo_add(std::string_view track_id, uint64_t tick, float bpm) override;
    bool        tempo_remove(std::string_view id) override;
    TempoPointData* tempo_get(std::string_view id) override;
    std::vector<const TempoPointData*> tempo_list() const override;

    // ── Hybrid ──────────────────────────────────────────────────────────────
    HybridClipData* hybrid_get(std::string_view clip_id) override;
    const HybridClipData* hybrid_get(std::string_view clip_id) const override;
    void hybrid_ensure(std::string_view clip_id) override;
    void hybrid_remove(std::string_view clip_id) override;

    // ── UUID ────────────────────────────────────────────────────────────────
    static std::string uuid();

private:
    template<typename T>
    using Store = std::unordered_map<std::string, std::unique_ptr<T>>;

    TransportState m_transport;
    ProjectInfo    m_project;

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
