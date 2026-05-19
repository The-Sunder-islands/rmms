#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <flatbuffers/flatbuffers.h>

namespace rmms::backend::core { class ProjectState; }
namespace rmms::backend::protocol { class ProtocolServer; }

namespace rmms::backend::mock {

class MockEngine {
public:
    MockEngine(std::shared_ptr<core::ProjectState> state,
               protocol::ProtocolServer* server);
    ~MockEngine();

    MockEngine(const MockEngine&) = delete;
    MockEngine& operator=(const MockEngine&) = delete;

    void start();
    void stop();

private:
    void run();
    void tick_position();
    void tick_levels();

    std::shared_ptr<core::ProjectState> m_state;
    protocol::ProtocolServer*           m_server;
    std::thread                         m_thread;
    std::atomic<bool>                   m_running;
    uint64_t                            m_last_tick;

    static constexpr int kPositionHz = 60;
    static constexpr int kLevelHz = 30;
    static constexpr int kTickIntervalUs = 1'000'000 / kPositionHz;
};

}  // namespace rmms::backend::mock
