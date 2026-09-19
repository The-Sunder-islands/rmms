#include "rmms/rmms_server.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <flatbuffers/flatbuffers.h>

#include "Engine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"
#include "core/main_thread_dispatcher.h"
#include "handlers/handlers.h"
#include "protocol/handler.h"
#include "protocol/server.h"
#include "protocol/subscription.h"
#include "rmms/lmms_project_state.h"
#include "rmms_generated.h"

namespace lmms::rmms {

namespace {

namespace backend = ::rmms::backend;

// Runs functors on the thread that created it (the model/GUI thread).
// invokeMethod with a functor + connection type needs no Q_OBJECT/moc.
class QtDispatcher : public QObject, public backend::core::IMainThreadDispatcher {
public:
    using QObject::QObject;

    void post(std::function<void()> fn) override {
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); },
                                  Qt::QueuedConnection);
    }

    void call(std::function<void()> fn) override {
        if (QThread::currentThread() == thread()) {
            fn();  // already on the model thread: run inline (no self-deadlock)
            return;
        }
        QMetaObject::invokeMethod(this, [fn = std::move(fn)]() { fn(); },
                                  Qt::BlockingQueuedConnection);
    }
};

// Forwards engine changes to subscribed protocol clients. It is created on
// the GUI thread and runs there, so it may talk to Song directly;
// ProtocolServer::push_event is thread-safe and filters by subscription.
class RmmsEventBridge : public QObject {
public:
    RmmsEventBridge(backend::protocol::ProtocolServer* server,
                    std::shared_ptr<LmmsProjectState> state,
                    QObject* parent = nullptr)
        : QObject(parent), m_server(server), m_state(std::move(state))
    {
        if (auto* song = Engine::getSong()) {
            connect(song, &TrackContainer::trackAdded, this,
                    [this](Track* track) { pushTrackAdded(track); });
            connect(song, &Song::playbackStateChanged, this,
                    [this] { pushState(); });
        }
        m_positionTimer.setInterval(16);  // ~60 Hz
        connect(&m_positionTimer, &QTimer::timeout, this,
                [this] { pushPosition(); });
        m_positionTimer.start();
    }

private:
    void pushPosition() {
        auto* song = Engine::getSong();
        if (song == nullptr || !song->isPlaying()) return;
        flatbuffers::FlatBufferBuilder fbb(64);
        auto ev = ::rmms::CreateEventPositionChanged(
            fbb, static_cast<uint64_t>(song->getPlayPos().getTicks()));
        fbb.Finish(ev);
        m_server->push_event("transport.position_changed",
                             fbb.GetBufferPointer(), fbb.GetSize());
    }

    void pushState() {
        auto* song = Engine::getSong();
        if (song == nullptr) return;
        const ::rmms::TransportState state = song->isPlaying()
            ? ::rmms::TransportState_PLAYING
            : (song->isPaused() ? ::rmms::TransportState_PAUSED
                                : ::rmms::TransportState_STOPPED);
        flatbuffers::FlatBufferBuilder fbb(64);
        auto ev = ::rmms::CreateEventStateChanged(fbb, state);
        fbb.Finish(ev);
        m_server->push_event("transport.state_changed",
                             fbb.GetBufferPointer(), fbb.GetSize());
    }

    void pushTrackAdded(Track* track) {
        if (track == nullptr) return;
        const std::string id = m_state->trackIdFor(track);
        const auto* dto = m_state->track_get(id);
        if (dto == nullptr) return;
        flatbuffers::FlatBufferBuilder fbb(512);
        auto t = ::rmms::backend::core::build_track(fbb, *dto);
        auto ev = ::rmms::CreateEventTrackAdded(fbb, t);
        fbb.Finish(ev);
        m_server->push_event("track.added",
                             fbb.GetBufferPointer(), fbb.GetSize());
    }

    backend::protocol::ProtocolServer* m_server;
    std::shared_ptr<LmmsProjectState> m_state;
    QTimer m_positionTimer;
};

std::shared_ptr<QtDispatcher> g_dispatcher;
std::shared_ptr<LmmsProjectState> g_state;
std::unique_ptr<RmmsEventBridge> g_events;
std::shared_ptr<backend::protocol::ProtocolServer> g_server;
std::thread g_thread;

// Methods that are registered by the handler groups but not backed by the
// LMMS adapter yet. Unregistering keeps clients honest: unknown method instead
// of a silently ignored write. Shrink this list only after the adapter method
// and its e2e coverage exist.
constexpr const char* kUnsupported[] = {
    // No engine equivalent (recording arm, per-clip loop, engine-level split).
    "track.set_arm", "clip.set_loop", "clip.split",
    // Mixer channels have no pan model in LMMS (panning is an effect).
    "mixer.set_pan",
    // Session lifecycle is not exposed yet.
    "project.new", "project.open", "project.close",
};

std::string socketPath() {
    const char* env = std::getenv("RMMS_SOCKET");
    return (env != nullptr && *env != '\0') ? std::string(env)
                                            : std::string("/tmp/rmms.sock");
}

}  // namespace

void startServer() {
    if (g_server != nullptr) return;

    g_dispatcher = std::make_shared<QtDispatcher>();
    auto state = std::make_shared<LmmsProjectState>(g_dispatcher);
    auto subs = std::make_shared<backend::protocol::SubscriptionManager>();
    auto registry = std::make_shared<backend::protocol::HandlerRegistry>();

    register_transport_handlers(*registry, state);
    register_project_handlers(*registry, state);
    register_track_handlers(*registry, state);
    register_clip_handlers(*registry, state);
    register_note_handlers(*registry, state);
    register_mixer_handlers(*registry, state);
    register_subscription_handlers(*registry, subs);

    for (const char* method : kUnsupported)
        registry->unregister_handler(method);

    const std::string path = socketPath();
    g_server = std::make_shared<backend::protocol::ProtocolServer>(
        path, registry, subs);
    g_thread = std::thread([] { g_server->start(); });

    g_state = state;
    g_events = std::make_unique<RmmsEventBridge>(g_server.get(), state);

    std::fprintf(stderr, "[rmms] control server listening on %s (%zu methods)\n",
                 path.c_str(), registry->registered_methods().size());
}

void stopServer() {
    if (g_server == nullptr) return;

    g_events.reset();  // stop the position timer before tearing down the server

    // Protocol threads may be blocked in QtDispatcher::call waiting for the
    // GUI thread. Stop on a helper thread while pumping the event loop here,
    // otherwise stop() would deadlock against the very thread it needs.
    std::thread stopper([] { g_server->stop(); });
    while (g_server->is_running())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    stopper.join();

    if (g_thread.joinable())
        g_thread.join();

    g_server.reset();
    g_dispatcher.reset();
    g_state.reset();
}

}  // namespace lmms::rmms
