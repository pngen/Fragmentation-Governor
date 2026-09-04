#ifndef FRAGMENTATION_GOVERNOR_CORE_TYPES_HPP
#define FRAGMENTATION_GOVERNOR_CORE_TYPES_HPP

#include <cstdint>
#include <functional>
#include <ostream>
#include <type_traits>

namespace fragmentation_governor {

// ---------------------------------------------------------------------------
// Strongly typed identities and generations.
//
// A distinct authority domain is never collapsed into a bare integer.  Every
// identity and every generation is a distinct type so that, for example, an
// AllocationId cannot be silently compared with a ResourceId, and a stale
// AllocationGeneration cannot be treated as a current ReservationGeneration.
// ---------------------------------------------------------------------------

/// Strongly typed identity.  The Tag type parameter makes each identity a
/// distinct, non-interconvertible type.  The null identity is 0.
template <typename Tag>
class TypedId {
 public:
  using value_type = std::uint64_t;

  constexpr TypedId() noexcept = default;
  constexpr explicit TypedId(value_type v) noexcept : value_(v) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool is_null() const noexcept { return value_ == 0; }
  constexpr explicit operator bool() const noexcept { return !is_null(); }

  constexpr bool operator==(const TypedId&) const noexcept = default;
  constexpr auto operator<=>(const TypedId&) const noexcept = default;

  friend std::ostream& operator<<(std::ostream& os, const TypedId& id) {
    os << id.value_;
    return os;
  }

 private:
  value_type value_ = 0;
};

/// Strongly typed, monotonically increasing generation (version) for a domain
/// of authority.  Generation 0 is the null/never-issued generation.
template <typename Tag>
class Generation {
 public:
  using value_type = std::uint64_t;

  constexpr Generation() noexcept = default;
  constexpr explicit Generation(value_type v) noexcept : value_(v) {}

  constexpr value_type value() const noexcept { return value_; }
  constexpr bool is_null() const noexcept { return value_ == 0; }
  constexpr bool is_valid() const noexcept { return value_ != 0; }
  constexpr explicit operator bool() const noexcept { return is_valid(); }

  /// Advance to the next generation (with max-guard against wraparound).
  constexpr Generation next() const noexcept {
    if (value_ == UINT64_MAX) return *this;
    return Generation(value_ + 1);
  }

  constexpr bool operator==(const Generation&) const noexcept = default;
  constexpr auto operator<=>(const Generation&) const noexcept = default;

  friend std::ostream& operator<<(std::ostream& os, const Generation& g) {
    os << g.value_;
    return os;
  }

 private:
  value_type value_ = 0;
};

// ---------------------------------------------------------------------------
// Identity tags.  Each tag is an empty, complete type that disambiguates the
// identity and generation kinds below.
// ---------------------------------------------------------------------------
#define FG_DECLARE_TAG(NAME) struct NAME##Tag {};

FG_DECLARE_TAG(FragmentationDomainId)
FG_DECLARE_TAG(FragmentationDomainGeneration)
FG_DECLARE_TAG(FragmentationSnapshotId)
FG_DECLARE_TAG(FragmentationSnapshotGeneration)
FG_DECLARE_TAG(FragmentationAssessmentId)
FG_DECLARE_TAG(FragmentationAssessmentGeneration)
FG_DECLARE_TAG(FragmentationPlanId)
FG_DECLARE_TAG(FragmentationPlanGeneration)
FG_DECLARE_TAG(RemediationId)
FG_DECLARE_TAG(RemediationGeneration)
FG_DECLARE_TAG(ResourceId)
FG_DECLARE_TAG(ResourceGeneration)
FG_DECLARE_TAG(ResourcePoolId)
FG_DECLARE_TAG(ResourcePoolGeneration)
FG_DECLARE_TAG(AllocationId)
FG_DECLARE_TAG(AllocationGeneration)
FG_DECLARE_TAG(AllocationOwnerId)
FG_DECLARE_TAG(ReservationId)
FG_DECLARE_TAG(ReservationGeneration)
FG_DECLARE_TAG(CapacityModelGeneration)
FG_DECLARE_TAG(WorkloadDemandId)
FG_DECLARE_TAG(WorkloadDemandGeneration)
FG_DECLARE_TAG(WorkloadId)
FG_DECLARE_TAG(WorkloadGeneration)
FG_DECLARE_TAG(ExecutionId)
FG_DECLARE_TAG(ExecutionGeneration)
FG_DECLARE_TAG(DeviceId)
FG_DECLARE_TAG(DeviceGeneration)
FG_DECLARE_TAG(NodeId)
FG_DECLARE_TAG(NodeGeneration)
FG_DECLARE_TAG(MemoryDomainId)
FG_DECLARE_TAG(MemoryDomainGeneration)
FG_DECLARE_TAG(TopologyGeneration)
FG_DECLARE_TAG(CapabilityGeneration)
FG_DECLARE_TAG(PlacementGeneration)
FG_DECLARE_TAG(PolicyGeneration)
FG_DECLARE_TAG(PriorityGeneration)
FG_DECLARE_TAG(HealthGeneration)
FG_DECLARE_TAG(CoordinatorEpoch)
FG_DECLARE_TAG(WorkerId)
FG_DECLARE_TAG(WorkerBootId)
FG_DECLARE_TAG(AuthorityGeneration)
FG_DECLARE_TAG(ActionGeneration)
FG_DECLARE_TAG(VerificationGeneration)

#undef FG_DECLARE_TAG

using FragmentationDomainId = TypedId<FragmentationDomainIdTag>;
using FragmentationDomainGeneration = Generation<FragmentationDomainGenerationTag>;
using FragmentationSnapshotId = TypedId<FragmentationSnapshotIdTag>;
using FragmentationSnapshotGeneration = Generation<FragmentationSnapshotGenerationTag>;
using FragmentationAssessmentId = TypedId<FragmentationAssessmentIdTag>;
using FragmentationAssessmentGeneration = Generation<FragmentationAssessmentGenerationTag>;
using FragmentationPlanId = TypedId<FragmentationPlanIdTag>;
using FragmentationPlanGeneration = Generation<FragmentationPlanGenerationTag>;
using RemediationId = TypedId<RemediationIdTag>;
using RemediationGeneration = Generation<RemediationGenerationTag>;
using ResourceId = TypedId<ResourceIdTag>;
using ResourceGeneration = Generation<ResourceGenerationTag>;
using ResourcePoolId = TypedId<ResourcePoolIdTag>;
using ResourcePoolGeneration = Generation<ResourcePoolGenerationTag>;
using AllocationId = TypedId<AllocationIdTag>;
using AllocationGeneration = Generation<AllocationGenerationTag>;
using AllocationOwnerId = TypedId<AllocationOwnerIdTag>;
using ReservationId = TypedId<ReservationIdTag>;
using ReservationGeneration = Generation<ReservationGenerationTag>;
using CapacityModelGeneration = Generation<CapacityModelGenerationTag>;
using WorkloadDemandId = TypedId<WorkloadDemandIdTag>;
using WorkloadDemandGeneration = Generation<WorkloadDemandGenerationTag>;
using WorkloadId = TypedId<WorkloadIdTag>;
using WorkloadGeneration = Generation<WorkloadGenerationTag>;
using ExecutionId = TypedId<ExecutionIdTag>;
using ExecutionGeneration = Generation<ExecutionGenerationTag>;
using DeviceId = TypedId<DeviceIdTag>;
using DeviceGeneration = Generation<DeviceGenerationTag>;
using NodeId = TypedId<NodeIdTag>;
using NodeGeneration = Generation<NodeGenerationTag>;
using MemoryDomainId = TypedId<MemoryDomainIdTag>;
using MemoryDomainGeneration = Generation<MemoryDomainGenerationTag>;
using TopologyGeneration = Generation<TopologyGenerationTag>;
using CapabilityGeneration = Generation<CapabilityGenerationTag>;
using PlacementGeneration = Generation<PlacementGenerationTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;
using PriorityGeneration = Generation<PriorityGenerationTag>;
using HealthGeneration = Generation<HealthGenerationTag>;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;
using WorkerId = TypedId<WorkerIdTag>;
using WorkerBootId = Generation<WorkerBootIdTag>;
using AuthorityGeneration = Generation<AuthorityGenerationTag>;
using ActionGeneration = Generation<ActionGenerationTag>;
using VerificationGeneration = Generation<VerificationGenerationTag>;

}  // namespace fragmentation_governor

namespace std {
// Hash support for strongly typed identities and generations.
template <typename Tag>
struct hash<::fragmentation_governor::TypedId<Tag>> {
  size_t operator()(const ::fragmentation_governor::TypedId<Tag>& id) const noexcept {
    return hash<uint64_t>()(id.value());
  }
};
template <typename Tag>
struct hash<::fragmentation_governor::Generation<Tag>> {
  size_t operator()(const ::fragmentation_governor::Generation<Tag>& g) const noexcept {
    return hash<uint64_t>()(g.value());
  }
};
}  // namespace std

#endif  // FRAGMENTATION_GOVERNOR_CORE_TYPES_HPP
