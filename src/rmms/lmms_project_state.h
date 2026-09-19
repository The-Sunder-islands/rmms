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

    // ── M1 stubs (unregistered by the embedder; M2 implements the write path) ─
    std::string track_add(TrackType, std::string_view) override { return {}; }
    bool track_remove(std::string_view) override { return false; }

    std::string clip_add(std::string_view, ClipType, uint64_t, uint64_t) override { return {}; }
    bool clip_remove(std::string_view) override { return false; }
    std::string clip_split(std::string_view, uint64_t) override { return {}; }

    std::string note_add(std::string_view, uint8_t, uint64_t, uint64_t,
                         uint8_t, uint8_t) override { return {}; }
    bool note_remove(std::string_view) override { return false; }

    std::string channel_add(std::string_view) override { return {}; }
    bool channel_remove(std::string_view) override { return false; }

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

    std::shared_ptr<::rmms::backend::core::IMainThreadDispatcher> m_dispatch;

    mutable ::rmms::backend::core::TransportState m_transport;
    mutable ::rmms::backend::core::ProjectInfo    m_project;

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
