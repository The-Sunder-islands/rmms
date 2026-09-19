#pragma once

#include <memory>

#include "core/iproject_state.h"

namespace rmms::backend::protocol {
class HandlerRegistry;
}  // namespace rmms::backend::protocol

// Handler registration entry points. Defined by the individual handler
// translation units; declared here so embedders do not repeat externs.

void register_transport_handlers(rmms::backend::protocol::HandlerRegistry&,
                                 std::shared_ptr<rmms::backend::core::IProjectState>);
void register_track_handlers(rmms::backend::protocol::HandlerRegistry&,
                             std::shared_ptr<rmms::backend::core::IProjectState>);
void register_clip_handlers(rmms::backend::protocol::HandlerRegistry&,
                            std::shared_ptr<rmms::backend::core::IProjectState>);
void register_note_handlers(rmms::backend::protocol::HandlerRegistry&,
                            std::shared_ptr<rmms::backend::core::IProjectState>);
void register_mixer_handlers(rmms::backend::protocol::HandlerRegistry&,
                             std::shared_ptr<rmms::backend::core::IProjectState>);
void register_project_handlers(rmms::backend::protocol::HandlerRegistry&,
                               std::shared_ptr<rmms::backend::core::IProjectState>);
void register_plugin_handlers(rmms::backend::protocol::HandlerRegistry&,
                              std::shared_ptr<rmms::backend::core::IProjectState>);
void register_hybrid_handlers(rmms::backend::protocol::HandlerRegistry&,
                              std::shared_ptr<rmms::backend::core::IProjectState>);
void register_chord_handlers(rmms::backend::protocol::HandlerRegistry&,
                             std::shared_ptr<rmms::backend::core::IProjectState>);
void register_arranger_handlers(rmms::backend::protocol::HandlerRegistry&,
                                std::shared_ptr<rmms::backend::core::IProjectState>);
void register_marker_handlers(rmms::backend::protocol::HandlerRegistry&,
                              std::shared_ptr<rmms::backend::core::IProjectState>);
void register_tempo_handlers(rmms::backend::protocol::HandlerRegistry&,
                             std::shared_ptr<rmms::backend::core::IProjectState>);
