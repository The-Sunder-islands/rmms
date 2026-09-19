#include "rmms/lmms_project_state.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QFileInfo>
#include <QString>

#include "AudioEngine.h"
#include "Clip.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms::rmms {

namespace core = ::rmms::backend::core;

namespace {

std::string toStd(const QString& s) {
    const QByteArray u = s.toUtf8();
    return std::string(u.constData(), static_cast<size_t>(u.size()));
}

// Pointer-derived opaque ids, stable for the lifetime of the object. Good
// enough for M1 reads; M2 will introduce real persistent ids.
std::string pointerId(const void* p) {
    return "x" + std::to_string(reinterpret_cast<quintptr>(p));
}

std::optional<TrackType> mapTrackType(Track::Type type) {
    switch (type) {
        case Track::Type::Instrument: return ::rmms::TrackType_INSTRUMENT;
        case Track::Type::Sample:     return ::rmms::TrackType_AUDIO;
        case Track::Type::Pattern:    return ::rmms::TrackType_INSTRUMENT;
        default:
            // Automation/Event/Video tracks are engine plumbing, not user
            // tracks in the RMMS model; skip them for now.
            return std::nullopt;
    }
}

ClipType mapClipType(const Clip* clip) {
    if (dynamic_cast<const MidiClip*>(clip)) return ::rmms::ClipType_MIDI;
    // This fork stores pattern notes in a shared PatternStore; PatternClips
    // are the song-editor placeholders and carry MIDI-like content.
    if (dynamic_cast<const PatternClip*>(clip)) return ::rmms::ClipType_MIDI;
    if (dynamic_cast<const SampleClip*>(clip)) return ::rmms::ClipType_AUDIO;
    return ::rmms::ClipType_AUTOMATION;  // AutomationClip and anything unknown
}

uint8_t encodePan(double pan) {
    // LMMS note panning: -1 (left) .. 1 (right) .. 0 center;
    // RMMS protocol: 0 (left) .. 64 (center) .. 127 (right).
    const double v = std::round((std::clamp(pan, -1.0, 1.0) + 1.0) * 63.5);
    return static_cast<uint8_t>(std::clamp(v, 0.0, 127.0));
}

}  // namespace

LmmsProjectState::LmmsProjectState(
    std::shared_ptr<core::IMainThreadDispatcher> dispatch)
    : m_dispatch(std::move(dispatch))
{
}

void LmmsProjectState::refresh() const {
    m_dispatch->call([this] {
        auto* song = Engine::getSong();
        if (song == nullptr) return;

        m_transport.state = song->isPlaying()
            ? ::rmms::TransportState_PLAYING
            : (song->isPaused() ? ::rmms::TransportState_PAUSED
                                : ::rmms::TransportState_STOPPED);
        m_transport.position = static_cast<uint64_t>(song->getPlayPos().getTicks());
        m_transport.bpm = song->getTempo();
        m_transport.time_sig_num = static_cast<uint32_t>(song->getTimeSigModel().getNumerator());
        m_transport.time_sig_den = static_cast<uint32_t>(song->getTimeSigModel().getDenominator());

        const QString projectFile = song->projectFileName();
        m_project.file_path = toStd(projectFile);
        m_project.name = projectFile.isEmpty()
            ? std::string("Untitled")
            : toStd(QFileInfo(projectFile).completeBaseName());
        m_project.modified = song->isModified();
        if (auto* ae = Engine::audioEngine())
            m_project.sample_rate = ae->outputSampleRate();
    });
}

core::TransportState& LmmsProjectState::transport() {
    refresh();
    return m_transport;
}

const core::TransportState& LmmsProjectState::transport() const {
    refresh();
    return m_transport;
}

core::ProjectInfo& LmmsProjectState::project() {
    refresh();
    return m_project;
}

const core::ProjectInfo& LmmsProjectState::project() const {
    refresh();
    return m_project;
}

void LmmsProjectState::transport_play() {
    m_dispatch->call([] {
        if (auto* song = Engine::getSong())
            song->playSong();
    });
}

void LmmsProjectState::transport_stop() {
    m_dispatch->call([] {
        if (auto* song = Engine::getSong())
            song->stop();
    });
}

void LmmsProjectState::transport_pause() {
    m_dispatch->call([] {
        if (auto* song = Engine::getSong())
            song->togglePause();
    });
}

void LmmsProjectState::transport_set_position(uint64_t tick) {
    m_dispatch->call([tick] {
        if (auto* song = Engine::getSong())
            song->setPlayPos(static_cast<tick_t>(tick));
    });
}

void LmmsProjectState::transport_set_tempo(float bpm) {
    m_dispatch->call([bpm] {
        if (auto* song = Engine::getSong())
            song->setTempo(bpm);
    });
}

std::vector<const core::TrackData*> LmmsProjectState::track_list() const {
    m_tracks.clear();
    m_track_order.clear();
    m_track_ptrs.clear();

    m_dispatch->call([this] {
        auto* song = Engine::getSong();
        if (song == nullptr) return;

        for (Track* track : song->tracks()) {
            if (track == nullptr) continue;
            const auto type = mapTrackType(track->type());
            if (!type) continue;

            core::TrackData d;
            d.id = pointerId(track);
            d.name = toStd(track->name());
            d.type = *type;
            d.mute = track->isMuted();
            d.solo = track->isSolo();
            if (const auto& color = track->color())
                d.color = static_cast<uint32_t>(color->rgba());

            m_track_ptrs[d.id] = track;
            m_track_order.push_back(d.id);
            m_tracks[d.id] = std::move(d);
        }
    });

    std::vector<const core::TrackData*> out;
    out.reserve(m_track_order.size());
    for (const auto& id : m_track_order)
        out.push_back(&m_tracks[id]);
    return out;
}

const core::TrackData* LmmsProjectState::track_get(std::string_view id) const {
    track_list();  // refresh cache
    auto it = m_tracks.find(std::string(id));
    return it == m_tracks.end() ? nullptr : &it->second;
}

core::TrackData* LmmsProjectState::track_get(std::string_view id) {
    track_list();
    auto it = m_tracks.find(std::string(id));
    return it == m_tracks.end() ? nullptr : &it->second;
}

std::vector<const core::ClipData*> LmmsProjectState::clip_list(
    std::string_view track_id) const
{
    m_clips.clear();
    m_clip_ptrs.clear();
    m_clip_order.clear();

    if (m_track_ptrs.empty())
        track_list();

    m_dispatch->call([this, track_id] {
        auto it = m_track_ptrs.find(std::string(track_id));
        if (it == m_track_ptrs.end()) return;

        for (Clip* clip : it->second->getClips()) {
            if (clip == nullptr) continue;
            core::ClipData d;
            d.id = pointerId(clip);
            d.track_id = std::string(track_id);
            d.type = mapClipType(clip);
            d.start_tick = static_cast<uint64_t>(clip->startPosition().getTicks());
            d.length_ticks = static_cast<uint64_t>(clip->length().getTicks());

            m_clip_ptrs[d.id] = clip;
            m_clip_order.push_back(d.id);
            m_clips[d.id] = std::move(d);
        }
    });

    std::vector<const core::ClipData*> out;
    out.reserve(m_clip_order.size());
    for (const auto& id : m_clip_order)
        out.push_back(&m_clips[id]);
    return out;
}

const core::ClipData* LmmsProjectState::clip_get(std::string_view id) const {
    m_clips.clear();
    m_clip_ptrs.clear();
    m_clip_order.clear();

    if (m_track_ptrs.empty())
        track_list();

    m_dispatch->call([this, id] {
        for (const auto& [track_id, track] : m_track_ptrs) {
            for (Clip* clip : track->getClips()) {
                if (clip == nullptr || pointerId(clip) != id) continue;
                core::ClipData d;
                d.id = std::string(id);
                d.track_id = track_id;
                d.type = mapClipType(clip);
                d.start_tick = static_cast<uint64_t>(clip->startPosition().getTicks());
                d.length_ticks = static_cast<uint64_t>(clip->length().getTicks());
                m_clip_ptrs[d.id] = clip;
                m_clip_order.push_back(d.id);
                m_clips[d.id] = std::move(d);
                return;
            }
        }
    });

    auto it = m_clips.find(std::string(id));
    return it == m_clips.end() ? nullptr : &it->second;
}

core::ClipData* LmmsProjectState::clip_get(std::string_view id) {
    const auto* c = static_cast<const LmmsProjectState*>(this)->clip_get(id);
    if (c == nullptr) return nullptr;
    return &m_clips[std::string(id)];
}

std::vector<const core::NoteData*> LmmsProjectState::note_list(
    std::string_view clip_id) const
{
    m_notes.clear();
    m_note_order.clear();

    if (m_clip_ptrs.count(std::string(clip_id)) == 0)
        clip_get(clip_id);

    m_dispatch->call([this, clip_id] {
        auto it = m_clip_ptrs.find(std::string(clip_id));
        if (it == m_clip_ptrs.end()) return;

        int index = 0;
        auto addNotes = [&](Clip* owner, const NoteVector& notes) {
            if (owner == nullptr) return;
            const int clipStart = owner->startPosition().getTicks();
            for (const Note* note : notes) {
                if (note == nullptr) continue;
                core::NoteData d;
                d.id = std::string(clip_id) + ":n" + std::to_string(index++);
                d.clip_id = std::string(clip_id);
                d.key = static_cast<uint8_t>(std::clamp(note->key(), 0, 127));
                // Protocol note positions are relative to the clip.
                d.start_tick = static_cast<uint64_t>(
                    std::max(0, note->pos().getTicks() - clipStart));
                d.length_ticks = static_cast<uint64_t>(note->length().getTicks());
                d.velocity = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(note->getVolume()), 0.0f, 255.0f));
                d.pan = encodePan(note->getPanning());

                m_note_order.push_back(d.id);
                m_notes[d.id] = std::move(d);
            }
        };

        Clip* clip = it->second;
        if (auto* midiClip = dynamic_cast<MidiClip*>(clip)) {
            addNotes(midiClip, midiClip->notes());
            return;
        }
        if (auto* patternClip = dynamic_cast<PatternClip*>(clip)) {
            // Pattern notes live in the shared PatternStore: one clip per
            // track at the pattern's column index.
            auto* patternTrack = dynamic_cast<PatternTrack*>(patternClip->getTrack());
            auto* store = Engine::patternStore();
            if (patternTrack == nullptr || store == nullptr) return;
            const int pattern = patternTrack->patternIndex();
            for (Track* track : store->tracks()) {
                if (pattern < track->numOfClips()) {
                    if (auto* storeClip = dynamic_cast<MidiClip*>(track->getClip(pattern)))
                        addNotes(storeClip, storeClip->notes());
                }
            }
        }
    });

    std::vector<const core::NoteData*> out;
    out.reserve(m_note_order.size());
    for (const auto& id : m_note_order)
        out.push_back(&m_notes[id]);
    return out;
}

core::NoteData* LmmsProjectState::note_get(std::string_view id) {
    const std::string key(id);
    const auto pos = key.rfind(":n");
    if (pos == std::string::npos) return nullptr;
    note_list(key.substr(0, pos));
    auto it = m_notes.find(key);
    return it == m_notes.end() ? nullptr : &it->second;
}

std::vector<const core::MixerChannelData*> LmmsProjectState::channel_list() const {
    m_channels.clear();
    m_channel_order.clear();

    m_dispatch->call([this] {
        auto* mixer = Engine::mixer();
        if (mixer == nullptr) return;

        for (int i = 0; i < mixer->numChannels(); ++i) {
            MixerChannel* ch = mixer->mixerChannel(i);
            if (ch == nullptr) continue;
            core::MixerChannelData d;
            d.id = "ch" + std::to_string(i);
            d.name = toStd(ch->m_name);
            d.volume = ch->m_volumeModel.value();
            d.mute = ch->m_muteModel.value();
            d.solo = ch->m_soloModel.value();

            m_channel_order.push_back(d.id);
            m_channels[d.id] = std::move(d);
        }
    });

    std::vector<const core::MixerChannelData*> out;
    out.reserve(m_channel_order.size());
    for (const auto& id : m_channel_order)
        out.push_back(&m_channels[id]);
    return out;
}

core::MixerChannelData* LmmsProjectState::channel_get(std::string_view id) {
    channel_list();
    auto it = m_channels.find(std::string(id));
    return it == m_channels.end() ? nullptr : &it->second;
}

}  // namespace lmms::rmms
