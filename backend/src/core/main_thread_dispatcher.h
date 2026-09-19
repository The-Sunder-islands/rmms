#pragma once

#include <functional>

namespace rmms::backend::core {

// Marshals work onto the thread that owns the project model (the GUI thread
// in LMMS). Adapters that wrap an engine must not touch engine objects from
// protocol reader threads, so every access goes through one of these.
//
// Implementations are provided by the embedder (see LMMS: QtDispatcher).
class IMainThreadDispatcher {
public:
    virtual ~IMainThreadDispatcher() = default;

    // Fire-and-forget: queues fn, returns immediately.
    virtual void post(std::function<void()> fn) = 0;

    // Blocking: runs fn on the model thread and waits for completion. Must be
    // callable from the model thread itself (then it runs inline) to avoid
    // self-deadlock.
    virtual void call(std::function<void()> fn) = 0;
};

}  // namespace rmms::backend::core
