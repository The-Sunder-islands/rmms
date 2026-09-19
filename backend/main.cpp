#include "core/in_memory_project_state.h"
#include "protocol/handler.h"
#include "protocol/server.h"
#include "protocol/subscription.h"
#include "mock/mock_engine.h"
#include "handlers/ai_bridge_handler.h"
#include "rmms_generated.h"

#include <flatbuffers/flatbuffers.h>
#include <cstdio>
#include <memory>
#include <signal.h>

using namespace rmms::backend;
using namespace rmms;

// Forward declarations for handler registration (defined in respective .cpp)
extern void register_transport_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_track_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_clip_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_note_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_mixer_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_project_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_plugin_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_hybrid_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_chord_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_arranger_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_marker_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_tempo_handlers(protocol::HandlerRegistry&, std::shared_ptr<core::IProjectState>);
extern void register_subscription_handlers(protocol::HandlerRegistry&, std::shared_ptr<protocol::SubscriptionManager>);

static std::atomic<bool> g_running(true);
static protocol::ProtocolServer* g_server = nullptr;

static void signal_handler(int) {
    g_running = false;
    if (g_server)
        g_server->signal_stop();
}

static void add_mock_data(std::shared_ptr<core::InMemoryProjectState> s) {
    auto t1 = s->track_add(TrackType_INSTRUMENT, "Synth Lead");
    auto t2 = s->track_add(TrackType_AUDIO, "Drums");
    auto t3 = s->track_add(TrackType_CHORD, "Chords");
    auto t4 = s->track_add(TrackType_HYBRID, "Hybrid Vocal");

    auto c1 = s->clip_add(t1, ClipType_MIDI, 0, 1920);
    auto c2 = s->clip_add(t2, ClipType_AUDIO, 0, 3840);
    s->clip_add(t3, ClipType_AUTOMATION, 0, 960);

    s->note_add(c1, 60, 0, 240, 100, 64);
    s->note_add(c1, 64, 240, 120, 90, 64);
    s->note_add(c1, 67, 360, 480, 80, 64);
    s->note_add(c1, 72, 960, 240, 95, 64);

    s->channel_add("Master");
    s->channel_add("Synth Bus");
    s->channel_add("Drum Bus");

    s->plugin_set("rmms.eq.1", "4-Band EQ", PluginCategory_EMBED, "/usr/lib/rmms/eq.so", "eq_entry");
    s->plugin_set("rmms.comp.1", "Compressor", PluginCategory_EMBED, "/usr/lib/rmms/comp.so", "comp_entry");

    s->transport().bpm = 128.0f;
    s->project().name = "Mock Project";
    s->project().sample_rate = 44100.0f;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto state = std::make_shared<core::InMemoryProjectState>();
    auto subs = std::make_shared<protocol::SubscriptionManager>();
    auto registry = std::make_shared<protocol::HandlerRegistry>();

    add_mock_data(state);

    register_transport_handlers(*registry, state);
    register_track_handlers(*registry, state);
    register_clip_handlers(*registry, state);
    register_note_handlers(*registry, state);
    register_mixer_handlers(*registry, state);
    register_project_handlers(*registry, state);
    register_plugin_handlers(*registry, state);
    register_hybrid_handlers(*registry, state);
    register_chord_handlers(*registry, state);
    register_arranger_handlers(*registry, state);
    register_marker_handlers(*registry, state);
    register_tempo_handlers(*registry, state);
    register_subscription_handlers(*registry, subs);

    protocol::ProtocolServer server("/tmp/rmms.sock", registry, subs);
    mock::MockEngine engine(state, &server);
    handlers::AiBridge ai_bridge(&server);

    g_server = &server;

    register_ai_bridge_handlers(*registry, ai_bridge);

    printf("RMMS Mock Backend listening on /tmp/rmms.sock\n");
    printf("  Methods registered: %zu\n", registry->registered_methods().size());
    printf("  Mock data: 4 tracks, 3 clips, 4 notes, 3 channels, 2 plugins\n");
    printf("  Press Ctrl+C to stop\n");

    ai_bridge.start();
    engine.start();
    server.start();

    engine.stop();
    ai_bridge.stop();
    printf("Shutting down.\n");
    return 0;
}
