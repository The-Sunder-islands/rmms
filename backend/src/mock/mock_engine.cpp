#include "mock/mock_engine.h"
#include "core/iproject_state.h"
#include "protocol/server.h"
#include "rmms_generated.h"
#include <flatbuffers/flatbuffers.h>
#include <algorithm>
#include <random>

namespace rmms::backend::mock {

MockEngine::MockEngine(std::shared_ptr<core::IProjectState> state,
                       protocol::ProtocolServer* server)
    : m_state(std::move(state))
    , m_server(server)
    , m_running(false)
    , m_last_tick(0)
{
}

MockEngine::~MockEngine() { stop(); }

void MockEngine::start() {
    m_running = true;
    m_last_tick = 0;
    m_thread = std::thread(&MockEngine::run, this);
}

void MockEngine::stop() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void MockEngine::run() {
    int level_counter = 0;
    int level_every = kPositionHz / kLevelHz;

    while (m_running) {
        auto start = std::chrono::steady_clock::now();

        if (m_state->transport().state == TransportState_PLAYING) {
            tick_position();
            if (++level_counter >= level_every) {
                tick_levels();
                level_counter = 0;
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto remaining = std::chrono::microseconds(kTickIntervalUs) - elapsed;
        if (remaining > std::chrono::microseconds(0))
            std::this_thread::sleep_for(remaining);
    }
}

void MockEngine::tick_position() {
    static constexpr uint64_t kTicksPerBeat = 960;
    float beats_per_tick = (m_state->transport().bpm / 60.0f) / kPositionHz;
    m_state->transport().position += static_cast<uint64_t>(beats_per_tick * kTicksPerBeat);

    flatbuffers::FlatBufferBuilder fbb(32);
    auto event = CreateEventPositionChanged(fbb, m_state->transport().position);
    fbb.Finish(event);
    m_server->push_event("transport.position_changed",
                         fbb.GetBufferPointer(), fbb.GetSize());
}

void MockEngine::tick_levels() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_real_distribution<float> dis(-18.0f, -3.0f);

    auto channels = m_state->channel_list();
    if (channels.empty()) return;

    for (size_t i = 0; i < channels.size(); ++i) {
        float level_l = dis(gen);
        float level_r = level_l + ((dis(gen) + 18.0f) / 15.0f - 0.5f);

        flatbuffers::FlatBufferBuilder fbb(64);
        auto id = fbb.CreateString(channels[i]->id);
        float levels[] = { level_l, level_r };
        auto vec = fbb.CreateVector(levels, 2);
        auto event = CreateEventMeterLevel(fbb, id, vec);
        fbb.Finish(event);
        m_server->push_event("meter.level_changed",
                             fbb.GetBufferPointer(), fbb.GetSize());
    }
}

}  // namespace rmms::backend::mock
