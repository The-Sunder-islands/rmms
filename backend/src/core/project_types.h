#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rmms_generated.h"

namespace rmms::backend::core {

// Plain C++ data structs (not FlatBuffers-generated). Shared between the
// in-memory reference implementation and adapters onto a real engine.

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

}  // namespace rmms::backend::core
