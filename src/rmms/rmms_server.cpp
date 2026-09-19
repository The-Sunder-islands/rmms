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

#include "core/main_thread_dispatcher.h"
#include "handlers/handlers.h"
#include "protocol/handler.h"
#include "protocol/server.h"
#include "protocol/subscription.h"
#include "rmms/lmms_project_state.h"

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

std::shared_ptr<QtDispatcher> g_dispatcher;
std::shared_ptr<backend::protocol::ProtocolServer> g_server;
std::thread g_thread;

// Methods that are registered by the handler groups but not yet backed by the
// LMMS adapter (M1 is read-only + transport). Unregistering keeps clients
// honest: unknown method instead of a silently ignored write.
constexpr const char* kM1Unsupported[] = {
    "transport.set_time_sig", "transport.set_loop",
    "project.new", "project.open", "project.save",
    "project.save_as", "project.close",
    "track.add", "track.remove", "track.set_arm", "track.set_color",
    "track.set_mute", "track.set_name", "track.set_pan", "track.set_solo",
    "track.set_volume",
    "clip.add", "clip.remove", "clip.move", "clip.resize",
    "clip.set_loop", "clip.split",
    "note.add", "note.remove", "note.move", "note.set_length",
    "note.set_velocity",
    "mixer.add_channel", "mixer.remove_channel", "mixer.set_pan",
    "mixer.set_route", "mixer.set_volume",
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

    for (const char* method : kM1Unsupported)
        registry->unregister_handler(method);

    const std::string path = socketPath();
    g_server = std::make_shared<backend::protocol::ProtocolServer>(
        path, registry, subs);
    g_thread = std::thread([] { g_server->start(); });

    std::fprintf(stderr, "[rmms] control server listening on %s (%zu methods)\n",
                 path.c_str(), registry->registered_methods().size());
}

void stopServer() {
    if (g_server == nullptr) return;

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
}

}  // namespace lmms::rmms
