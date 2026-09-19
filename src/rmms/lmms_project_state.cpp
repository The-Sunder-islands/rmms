#include "rmms/lmms_project_state.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QFileInfo>
#include <QString>

#include "AudioEngine.h"
#include "Clip.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "Song.h"
#include "Timeline.h"
#include "Track.h"
#include "TrackContainer.h"

#include <QColor>

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

// LMMS note panning: -100 (left) .. 0 (center) .. 100 (right);
// RMMS protocol: 0 (left) .. 64 (center) .. 127 (right).
uint8_t encodePan(panning_t pan) {
    const double v = std::lround(static_cast<double>(pan) * 64.0 / 100.0 + 64.0);
    return static_cast<uint8_t>(std::clamp(v, 0.0, 127.0));
}

panning_t decodePan(uint8_t pan) {
    const double v = std::lround((static_cast<double>(pan) - 64.0) * 100.0 / 64.0);
    return static_cast<panning_t>(std::clamp(v, -100.0, 100.0));
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

        QString projectFile = song->projectFileName();
        if (projectFile.isEmpty() && !m_savedPath.empty())
            projectFile = QString::fromStdString(m_savedPath);
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
            if (auto* it = dynamic_cast<InstrumentTrack*>(track)) {
                // Engine models: volume 0..200 (%), panning -100..100 (%).
                d.volume = it->volumeModel()->value() / 100.0f;
                d.pan = static_cast<float>((it->panningModel()->value() + 100.0) / 200.0);
            }
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
            if (auto* sc = dynamic_cast<SampleClip*>(clip))
                d.audio_url = toStd(sc->sampleFile());

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
                if (auto* sc = dynamic_cast<SampleClip*>(clip))
                    d.audio_url = toStd(sc->sampleFile());
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
                (void)index;
                d.id = pointerId(note);
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
            d.volume = ch->m_volumeModel.value() / 100.0f;
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

std::string LmmsProjectState::trackIdFor(const lmms::Track* track) const {
    return pointerId(track);
}

// ── Resolve helpers ─────────────────────────────────────────────────────────

lmms::Track* LmmsProjectState::resolveTrack(std::string_view id) const {
    track_list();
    auto it = m_track_ptrs.find(std::string(id));
    return it == m_track_ptrs.end() ? nullptr : it->second;
}

lmms::Clip* LmmsProjectState::resolveClip(std::string_view id) const {
    clip_get(id);  // scans every user track and fills m_clip_ptrs
    auto it = m_clip_ptrs.find(std::string(id));
    return it == m_clip_ptrs.end() ? nullptr : it->second;
}

lmms::MixerChannel* LmmsProjectState::resolveChannel(std::string_view id) const {
    channel_list();
    const std::string key(id);
    if (key.rfind("ch", 0) != 0) return nullptr;
    int index = 0;
    try {
        index = std::stoi(key.substr(2));
    } catch (...) {
        return nullptr;
    }
    auto* mixer = Engine::mixer();
    if (mixer == nullptr || index < 0 || index >= mixer->numChannels())
        return nullptr;
    return mixer->mixerChannel(index);
}

lmms::Note* LmmsProjectState::findNote(std::string_view id,
                                       lmms::MidiClip** owner) const {
    lmms::Note* found = nullptr;
    lmms::MidiClip* foundOwner = nullptr;
    m_dispatch->call([&] {
        auto check = [&](Clip* clip) {
            if (found != nullptr) return;
            auto* midi = dynamic_cast<MidiClip*>(clip);
            if (midi == nullptr) return;
            for (Note* note : midi->notes()) {
                if (note != nullptr && pointerId(note) == id) {
                    found = note;
                    foundOwner = midi;
                    return;
                }
            }
        };
        if (auto* song = Engine::getSong())
            for (Track* t : song->tracks())
                for (Clip* c : t->getClips())
                    check(c);
        if (auto* store = Engine::patternStore())
            for (Track* t : store->tracks())
                for (Clip* c : t->getClips())
                    check(c);
    });
    if (owner) *owner = foundOwner;
    return found;
}

// ── Track writes ────────────────────────────────────────────────────────────

std::string LmmsProjectState::track_add(TrackType type, std::string_view name) {
    std::string id;
    m_dispatch->call([&] {
        auto* song = Engine::getSong();
        if (song == nullptr) return;
        const Track::Type tt = (type == ::rmms::TrackType_AUDIO)
            ? Track::Type::Sample
            : Track::Type::Instrument;
        Track* t = Track::create(tt, song);
        if (t == nullptr) return;
        if (!name.empty())
            t->setName(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
        // New instrument tracks default to a built-in synth so that added
        // notes are audible without a plugin picker.
        if (auto* it = dynamic_cast<InstrumentTrack*>(t))
            it->loadInstrument("tripleoscillator");
        song->addTrack(t);
        id = pointerId(t);
    });
    return id;
}

bool LmmsProjectState::track_remove(std::string_view id) {
    Track* track = resolveTrack(id);
    if (track == nullptr) return false;
    bool ok = false;
    m_dispatch->call([&] {
        if (auto* song = Engine::getSong()) {
            song->removeTrack(track);
            delete track;
            ok = true;
        }
    });
    if (ok) {
        m_track_ptrs.erase(std::string(id));
        m_tracks.erase(std::string(id));
    }
    return ok;
}

bool LmmsProjectState::track_set_name(std::string_view id, std::string_view name) {
    Track* track = resolveTrack(id);
    if (track == nullptr) return false;
    m_dispatch->call([&] {
        track->setName(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    });
    return true;
}

bool LmmsProjectState::track_set_volume(std::string_view id, float volume) {
    Track* track = resolveTrack(id);
    auto* it = dynamic_cast<InstrumentTrack*>(track);
    if (it == nullptr) return false;
    m_dispatch->call([&] { it->volumeModel()->setValue(volume * 100.0f); });
    return true;
}

bool LmmsProjectState::track_set_pan(std::string_view id, float pan) {
    Track* track = resolveTrack(id);
    auto* it = dynamic_cast<InstrumentTrack*>(track);
    if (it == nullptr) return false;
    m_dispatch->call([&] { it->panningModel()->setValue(pan * 200.0f - 100.0f); });
    return true;
}

bool LmmsProjectState::track_set_mute(std::string_view id, bool mute) {
    Track* track = resolveTrack(id);
    if (track == nullptr) return false;
    m_dispatch->call([&] { track->setMuted(mute); });
    return true;
}

bool LmmsProjectState::track_set_solo(std::string_view id, bool solo) {
    Track* track = resolveTrack(id);
    if (track == nullptr) return false;
    m_dispatch->call([&] { track->setSolo(solo); });
    return true;
}

bool LmmsProjectState::track_set_color(std::string_view id, uint32_t color) {
    Track* track = resolveTrack(id);
    if (track == nullptr) return false;
    m_dispatch->call([&] { track->setColor(QColor::fromRgba(color)); });
    return true;
}

// ── Clip writes ─────────────────────────────────────────────────────────────

std::string LmmsProjectState::clip_add(std::string_view track_id, ClipType type,
                                       uint64_t start_tick, uint64_t length_ticks) {
    (void)type;  // the track decides the clip type in LMMS
    Track* track = resolveTrack(track_id);
    if (track == nullptr) return {};
    std::string id;
    m_dispatch->call([&] {
        // Clip's constructor already registers the clip with its track; adding
        // it again would put the same pointer in the track's list twice.
        Clip* clip = track->createClip(TimePos(static_cast<int>(start_tick)));
        if (clip == nullptr) return;
        // Beat clips auto-resize to their content unless we opt out, which
        // would silently overwrite the requested length on the next edit.
        clip->setAutoResize(false);
        if (length_ticks > 0)
            clip->changeLength(TimePos(static_cast<int>(length_ticks)));
        id = pointerId(clip);
    });
    return id;
}

bool LmmsProjectState::clip_remove(std::string_view id) {
    Clip* clip = resolveClip(id);
    if (clip == nullptr) return false;
    m_dispatch->call([&] {
        if (Track* track = clip->getTrack())
            track->removeClip(clip);
        delete clip;
    });
    m_clip_ptrs.erase(std::string(id));
    m_clips.erase(std::string(id));
    return true;
}

bool LmmsProjectState::clip_move(std::string_view id, std::string_view track_id,
                                 uint64_t start_tick) {
    Clip* clip = resolveClip(id);
    Track* target = resolveTrack(track_id);
    if (clip == nullptr || target == nullptr || clip->getTrack() != target)
        return false;  // cross-track moves are not supported by the engine
    m_dispatch->call([&] {
        clip->movePosition(TimePos(static_cast<int>(start_tick)));
    });
    return true;
}

bool LmmsProjectState::clip_resize(std::string_view id, uint64_t length_ticks) {
    Clip* clip = resolveClip(id);
    if (clip == nullptr) return false;
    m_dispatch->call([&] {
        clip->changeLength(TimePos(static_cast<int>(length_ticks)));
    });
    return true;
}

bool LmmsProjectState::clip_set_audio_url(std::string_view id, std::string_view path) {
    auto* clip = dynamic_cast<SampleClip*>(resolveClip(id));
    if (clip == nullptr) return false;
    const QString file = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    m_dispatch->call([&] {
        // Loads the buffer synchronously and resizes the clip to the sample.
        clip->setSampleFile(file);
    });
    return true;
}

// ── Note writes ─────────────────────────────────────────────────────────────

std::string LmmsProjectState::note_add(std::string_view clip_id, uint8_t key,
                                       uint64_t start_tick, uint64_t length_ticks,
                                       uint8_t velocity, uint8_t pan) {
    auto* clip = dynamic_cast<MidiClip*>(resolveClip(clip_id));
    if (clip == nullptr) return {};
    std::string id;
    m_dispatch->call([&] {
        const int absolute = clip->startPosition().getTicks() +
                             static_cast<int>(start_tick);
        Note note(TimePos(static_cast<int>(length_ticks)), TimePos(absolute),
                  static_cast<int>(key), static_cast<volume_t>(velocity),
                  decodePan(pan));
        if (Note* added = clip->addNote(note, false))
            id = pointerId(added);
    });
    return id;
}

bool LmmsProjectState::note_remove(std::string_view id) {
    MidiClip* owner = nullptr;
    Note* note = findNote(id, &owner);
    if (note == nullptr || owner == nullptr) return false;
    m_dispatch->call([&] { owner->removeNote(note); });
    m_notes.erase(std::string(id));
    m_note_order.erase(std::remove(m_note_order.begin(), m_note_order.end(),
                                   std::string(id)), m_note_order.end());
    return true;
}

bool LmmsProjectState::note_move(std::string_view id, uint8_t key,
                                 uint64_t start_tick) {
    MidiClip* owner = nullptr;
    Note* note = findNote(id, &owner);
    if (note == nullptr || owner == nullptr) return false;
    m_dispatch->call([&] {
        note->setKey(static_cast<int>(key));
        note->setPos(TimePos(owner->startPosition().getTicks() +
                             static_cast<int>(start_tick)));
    });
    return true;
}

bool LmmsProjectState::note_set_length(std::string_view id, uint64_t length_ticks) {
    Note* note = findNote(id, nullptr);
    if (note == nullptr) return false;
    m_dispatch->call([&] { note->setLength(TimePos(static_cast<int>(length_ticks))); });
    return true;
}

bool LmmsProjectState::note_set_velocity(std::string_view id, uint8_t velocity) {
    Note* note = findNote(id, nullptr);
    if (note == nullptr) return false;
    m_dispatch->call([&] { note->setVolume(static_cast<volume_t>(velocity)); });
    return true;
}

// ── Mixer writes ────────────────────────────────────────────────────────────

std::string LmmsProjectState::channel_add(std::string_view name) {
    std::string id;
    m_dispatch->call([&] {
        auto* mixer = Engine::mixer();
        if (mixer == nullptr) return;
        const int index = mixer->createChannel();
        if (MixerChannel* ch = mixer->mixerChannel(index)) {
            if (!name.empty())
                ch->m_name = QString::fromUtf8(name.data(), static_cast<int>(name.size()));
            id = "ch" + std::to_string(index);
        }
    });
    return id;
}

bool LmmsProjectState::channel_remove(std::string_view id) {
    MixerChannel* ch = resolveChannel(id);
    if (ch == nullptr) return false;
    const int index = ch->index();
    m_dispatch->call([&] {
        if (auto* mixer = Engine::mixer())
            mixer->deleteChannel(index);
    });
    return true;
}

bool LmmsProjectState::channel_set_volume(std::string_view id, float volume) {
    MixerChannel* ch = resolveChannel(id);
    if (ch == nullptr) return false;
    m_dispatch->call([&] { ch->m_volumeModel.setValue(volume * 100.0f); });
    return true;
}

bool LmmsProjectState::channel_set_route(std::string_view src_id,
                                         std::string_view dst_id, float gain) {
    MixerChannel* src = resolveChannel(src_id);
    MixerChannel* dst = resolveChannel(dst_id);
    if (src == nullptr || dst == nullptr || src == dst) return false;
    m_dispatch->call([&] {
        if (auto* mixer = Engine::mixer())
            mixer->createChannelSend(src->index(), dst->index(), gain);
    });
    return true;
}

// ── Project writes ──────────────────────────────────────────────────────────

bool LmmsProjectState::project_save(std::string_view path) {
    bool ok = false;
    m_dispatch->call([&] {
        auto* song = Engine::getSong();
        if (song == nullptr) return;
        const QString target = path.empty()
            ? song->projectFileName()
            : QString::fromUtf8(path.data(), static_cast<int>(path.size()));
        if (target.isEmpty()) return;
        ok = song->saveProjectFile(target, false);
        if (ok)
            m_savedPath = toStd(target);
    });
    return ok;
}

}  // namespace lmms::rmms
