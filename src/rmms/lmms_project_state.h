#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/iproject_state.h"
#include "core/main_thread_dispatcher.h"

namespace lmms {
class Clip;
class MidiClip;
class MixerChannel;
class Note;
class Track;
}  // namespace lmms

namespace lmms::rmms {

// Bring the RMMS model types into this scope (the project's own namespace is
// the unrelated global `rmms`, so unqualified lookup would not find them).
using ::rmms::ChordType;
using ::rmms::ClipType;
using ::rmms::MarkerType;
using ::rmms::PluginCategory;
using ::rmms::TrackType;
using ::rmms::backend::core::ArrangerSectionData;
using ::rmms::backend::core::ChordData;
using ::rmms::backend::core::HybridClipData;
using ::rmms::backend::core::MarkerData;
using ::rmms::backend::core::TempoPointData;

// M1 adapter: read-only view of a live lmms::Song exposed to the RMMS protocol.
//
// Every access is marshalled to the GUI thread through IMainThreadDispatcher;
// protocol reader threads never touch lmms objects directly. Mutations are
// stubbed here and must stay unregistered in the embedder until M2 (see
// rmms_server.cpp) — a stub that silently succeeds would lie to clients.
class LmmsProjectState : public ::rmms::backend::core::IProjectState {
public:
    explicit LmmsProjectState(
        std::shared_ptr<::rmms::backend::core::IMainThreadDispatcher> dispatch);

    ::rmms::backend::core::TransportState& transport() override;
    const ::rmms::backend::core::TransportState& transport() const override;
    ::rmms::backend::core::ProjectInfo& project() override;
    const ::rmms::backend::core::ProjectInfo& project() const override;

    // ── Transport commands (M1, executed on the real engine) ───────────────
    void transport_play() override;
    void transport_stop() override;
    void transport_pause() override;
    void transport_set_position(uint64_t tick) override;
    void transport_set_tempo(float bpm) override;

    // ── Read path (M1, implemented) ─────────────────────────────────────────
    std::vector<const ::rmms::backend::core::TrackData*> track_list() const override;
    const ::rmms::backend::core::TrackData* track_get(std::string_view id) const override;
    ::rmms::backend::core::TrackData* track_get(std::string_view id) override;

    std::vector<const ::rmms::backend::core::ClipData*> clip_list(std::string_view track_id) const override;
    const ::rmms::backend::core::ClipData* clip_get(std::string_view id) const override;
    ::rmms::backend::core::ClipData* clip_get(std::string_view id) override;

    std::vector<const ::rmms::backend::core::NoteData*> note_list(std::string_view clip_id) const override;
    ::rmms::backend::core::NoteData* note_get(std::string_view id) override;

    std::vector<const ::rmms::backend::core::MixerChannelData*> channel_list() const override;
    ::rmms::backend::core::MixerChannelData* channel_get(std::string_view id) override;

    // ── Write path (M2, executed on the real engine) ────────────────────────
    std::string track_add(TrackType type, std::string_view name) override;
    bool track_remove(std::string_view id) override;
    bool track_set_name(std::string_view id, std::string_view name) override;
    bool track_set_volume(std::string_view id, float volume) override;
    bool track_set_pan(std::string_view id, float pan) override;
    bool track_set_mute(std::string_view id, bool mute) override;
    bool track_set_solo(std::string_view id, bool solo) override;
    bool track_set_color(std::string_view id, uint32_t color) override;

    std::string clip_add(std::string_view track_id, ClipType type,
                         uint64_t start_tick, uint64_t length_ticks) override;
    bool clip_remove(std::string_view id) override;
    bool clip_move(std::string_view id, std::string_view track_id,
                   uint64_t start_tick) override;
    bool clip_resize(std::string_view id, uint64_t length_ticks) override;

    std::string note_add(std::string_view clip_id, uint8_t key,
                         uint64_t start_tick, uint64_t length_ticks,
                         uint8_t velocity, uint8_t pan) override;
    bool note_remove(std::string_view id) override;
    bool note_move(std::string_view id, uint8_t key, uint64_t start_tick) override;
    bool note_set_length(std::string_view id, uint64_t length_ticks) override;
    bool note_set_velocity(std::string_view id, uint8_t velocity) override;

    std::string channel_add(std::string_view name) override;
    bool channel_remove(std::string_view id) override;
    bool channel_set_volume(std::string_view id, float volume) override;
    bool channel_set_route(std::string_view src_id, std::string_view dst_id,
                           float gain) override;

    bool project_save(std::string_view path) override;

    // Same id scheme as the read/write paths; used by the event bridge to tag
    // engine objects that arrive through Qt signals.
    std::string trackIdFor(const lmms::Track* track) const;

    // ── Stubs (unregistered by the embedder; engine has no equivalent yet) ──
    bool track_set_arm(std::string_view, bool) override { return false; }
    std::string clip_split(std::string_view, uint64_t) override { return {}; }
    bool clip_set_loop(std::string_view, uint64_t, uint64_t) override { return false; }
    bool channel_set_pan(std::string_view, float) override { return false; }

    void plugin_set(const std::string&, const std::string&, PluginCategory,
                    const std::string&, const std::string&) override {}
    std::vector<const ::rmms::backend::core::PluginData*> plugin_list() const override { return {}; }
    const ::rmms::backend::core::PluginData* plugin_get(std::string_view) const override { return nullptr; }
    void plugin_set_param(std::string_view, std::string_view, std::string_view) override {}
    std::string plugin_get_params(std::string_view) const override { return "{}"; }
    void plugin_set_enabled(std::string_view, bool) override {}
    bool plugin_is_enabled(std::string_view) const override { return false; }

    std::string chord_add(std::string_view, uint64_t, uint8_t, ChordType, uint64_t) override { return {}; }
    bool chord_remove(std::string_view) override { return false; }
    ChordData* chord_get(std::string_view) override { return nullptr; }
    std::vector<const ::rmms::backend::core::ChordData*> chord_list() const override { return {}; }

    std::string arranger_add(std::string_view, std::string_view, uint64_t, uint64_t,
                             uint32_t, uint32_t) override { return {}; }
    bool arranger_remove(std::string_view) override { return false; }
    ArrangerSectionData* arranger_get(std::string_view) override { return nullptr; }
    std::vector<const ::rmms::backend::core::ArrangerSectionData*> arranger_list() const override { return {}; }

    std::string marker_add(std::string_view, std::string_view, uint64_t, MarkerType) override { return {}; }
    bool marker_remove(std::string_view) override { return false; }
    MarkerData* marker_get(std::string_view) override { return nullptr; }
    std::vector<const ::rmms::backend::core::MarkerData*> marker_list() const override { return {}; }

    std::string tempo_add(std::string_view, uint64_t, float) override { return {}; }
    bool tempo_remove(std::string_view) override { return false; }
    TempoPointData* tempo_get(std::string_view) override { return nullptr; }
    std::vector<const ::rmms::backend::core::TempoPointData*> tempo_list() const override { return {}; }

    ::rmms::backend::core::HybridClipData* hybrid_get(std::string_view) override { return nullptr; }
    const ::rmms::backend::core::HybridClipData* hybrid_get(std::string_view) const override { return nullptr; }
    void hybrid_ensure(std::string_view) override {}
    void hybrid_remove(std::string_view) override {}

private:
    // Rebuilds the transport/project snapshot on the GUI thread.
    void refresh() const;

    // Resolves a stable note id (pointer-derived) to its LMMS objects. Must be
    // called with no cache guarantees; scans the pattern store and clips.
    lmms::Note* findNote(std::string_view id, lmms::MidiClip** owner) const;

    // Resolve protocol ids to engine objects (refresh the relevant cache).
    lmms::Track* resolveTrack(std::string_view id) const;
    lmms::Clip* resolveClip(std::string_view id) const;
    lmms::MixerChannel* resolveChannel(std::string_view id) const;

    std::shared_ptr<::rmms::backend::core::IMainThreadDispatcher> m_dispatch;

    mutable ::rmms::backend::core::TransportState m_transport;
    mutable ::rmms::backend::core::ProjectInfo    m_project;
    // Song::saveProjectFile() does not update the stored file name, so track
    // the last saved path ourselves and report it until the project reloads.
    mutable std::string m_savedPath;

    mutable std::map<std::string, ::rmms::backend::core::TrackData> m_tracks;
    mutable std::vector<std::string> m_track_order;
    mutable std::map<std::string, lmms::Track*> m_track_ptrs;

    mutable std::map<std::string, ::rmms::backend::core::ClipData> m_clips;
    mutable std::map<std::string, lmms::Clip*> m_clip_ptrs;
    mutable std::vector<std::string> m_clip_order;

    mutable std::map<std::string, ::rmms::backend::core::NoteData> m_notes;
    mutable std::vector<std::string> m_note_order;

    mutable std::map<std::string, ::rmms::backend::core::MixerChannelData> m_channels;
    mutable std::vector<std::string> m_channel_order;
};

}  // namespace lmms::rmms
