#pragma once

namespace lmms::rmms {

// Starts the RMMS control-plane server on RMMS_SOCKET (default
// /tmp/rmms.sock). Must be called from the model/GUI thread after the engine
// is initialised; the server itself runs on background threads and marshals
// model access back to the caller's thread.
void startServer();

// Stops the server and joins its threads. Safe to call if not started.
void stopServer();

}  // namespace lmms::rmms
