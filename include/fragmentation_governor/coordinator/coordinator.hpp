#ifndef FRAGMENTATION_GOVERNOR_COORDINATOR_COORDINATOR_HPP
#define FRAGMENTATION_GOVERNOR_COORDINATOR_COORDINATOR_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/persistence/state.hpp"
#include "fragmentation_governor/planner/plan.hpp"
#include "fragmentation_governor/protocol/protocol.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"

namespace fragmentation_governor {

/// A worker's authoritative claim over a managed resource.
struct WorkerResourceRecord {
  Resource resource;
  ResourceLayout layout;
  WorkerId worker;
  WorkerBootId boot;
  bool alive = true;
  Provenance provenance = Provenance::UNKNOWN;
};

/// The in-process Fragmentation Governor coordinator state machine.  It is the
/// current-authority arbiter for the governed logical pool of fragmentation
/// evidence.  It never claims to be a scheduler / allocator / broker; it only
/// measures, explains, plans, and verifies.
class Coordinator {
 public:
  explicit Coordinator(CoordinatorEpoch epoch);

  /// Process one inbound message and produce outbound responses (usually one).
  /// Returns an empty vector when no response is appropriate (e.g. broadcast).
  std::vector<Message> handle(const Message& in);

  // ---- state access (tests / tooling) --------------------------------------
  CoordinatorEpoch epoch() const { return epoch_; }
  const std::map<ResourceId, WorkerResourceRecord>& resources() const { return resources_; }
  const std::map<FragmentationPlanId, RemediationPlan>& plans() const { return plans_; }
  FragmentationSnapshot assemble_snapshot() const;
  const PersistedState& persisted() const { return persisted_; }
  PolicyGeneration policy_generation() const { return policy_generation_; }

  // ---- recovery helpers ----------------------------------------------------
  /// Reconstruct from a persisted state after restart, then mark all dynamic
  /// evidence as requiring revalidation.
  void recover(const PersistedState& state);
  void advance_policy_generation() { policy_generation_ = policy_generation_.next(); }

  // ---- worker liveness -----------------------------------------------------
  void mark_worker_dead(WorkerId worker);

 private:
  CoordinatorEpoch epoch_;
  PolicyGeneration policy_generation_;
  std::map<ResourceId, WorkerResourceRecord> resources_;
  std::map<FragmentationPlanId, RemediationPlan> plans_;
  std::map<WorkerId, WorkerBootId> worker_boot_;
  std::map<WorkerId, bool> worker_alive_;
  PersistedState persisted_;
  std::uint64_t next_plan_id_ = 1;
  FragmentationSnapshotGeneration snapshot_generation_;
  KnownResourceGenerations generations_;
  std::map<FragmentationPlanId, FragmentationSnapshot> before_snapshots_;

  // plan lifecycle helpers
  bool can_execute(const RemediationPlan& p, const Message& in) const;
  WorkloadDemand last_demand_;
  Message ack() const { Message m; m.type = MsgType::ERROR; return m; }
  Message fail(std::string why) const;
  void advance_resource_generation();
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_COORDINATOR_COORDINATOR_HPP