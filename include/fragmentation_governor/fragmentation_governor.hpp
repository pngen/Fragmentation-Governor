#ifndef FRAGMENTATION_GOVERNOR_FRAGMENTATION_GOVERNOR_HPP
#define FRAGMENTATION_GOVERNOR_FRAGMENTATION_GOVERNOR_HPP

// Umbrella public header for the Fragmentation Governor C++20 runtime.
//
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
// No telemetry transmission.

#include "fragmentation_governor/version.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/domain/generations.hpp"
#include "fragmentation_governor/resource/movability.hpp"
#include "fragmentation_governor/resource/allocation.hpp"
#include "fragmentation_governor/resource/resource.hpp"
#include "fragmentation_governor/resource/layout.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"
#include "fragmentation_governor/snapshot/reservation.hpp"
#include "fragmentation_governor/metrics/metrics.hpp"
#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/fit/outcome.hpp"
#include "fragmentation_governor/fit/fit.hpp"
#include "fragmentation_governor/policy/policy.hpp"
#include "fragmentation_governor/planner/action.hpp"
#include "fragmentation_governor/planner/plan_state.hpp"
#include "fragmentation_governor/planner/plan.hpp"
#include "fragmentation_governor/planner/planner.hpp"
#include "fragmentation_governor/verification/outcome.hpp"
#include "fragmentation_governor/verification/verification.hpp"
#include "fragmentation_governor/persistence/state.hpp"
#include "fragmentation_governor/persistence/codec.hpp"
#include "fragmentation_governor/protocol/protocol.hpp"
#include "fragmentation_governor/coordinator/coordinator.hpp"
#include "fragmentation_governor/coordinator/tcp_transport.hpp"

#endif  // FRAGMENTATION_GOVERNOR_FRAGMENTATION_GOVERNOR_HPP