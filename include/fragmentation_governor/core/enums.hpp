#ifndef FRAGMENTATION_GOVERNOR_CORE_ENUMS_HPP
#define FRAGMENTATION_GOVERNOR_CORE_ENUMS_HPP

#include <cstdint>
#include <string_view>

namespace fragmentation_governor {

/// Fragmentation domain taxonomy.  A domain is the scope in which fragmentation
/// is measured (a device's memory, an accelerator memory pool, a reservation
/// timeline, a placement topology, ...).  Not every domain is remediable by the
/// same mechanism.
enum class Domain : std::uint8_t {
  CUDA_DEVICE_MEMORY,
  ACCELERATOR_MEMORY_POOL,
  PINNED_HOST_MEMORY,
  HOST_MEMORY,
  NUMA_NODE_MEMORY,
  SHARED_MEMORY_POOL,
  STORAGE_CAPACITY,
  STORAGE_BANDWIDTH,
  PCIe_BANDWIDTH,
  NETWORK_BANDWIDTH,
  ACCELERATOR_SLOT,
  MODEL_RESIDENCY,
  ADAPTER_RESIDENCY,
  KV_OR_TENSOR_RESIDENCY,
  RESERVATION_TIMELINE,
  PLACEMENT_TOPOLOGY,
  COMPOSITE_RESOURCE,
  UNKNOWN,
};

inline constexpr std::string_view to_string(Domain d) noexcept {
  switch (d) {
    case Domain::CUDA_DEVICE_MEMORY: return "CUDA_DEVICE_MEMORY";
    case Domain::ACCELERATOR_MEMORY_POOL: return "ACCELERATOR_MEMORY_POOL";
    case Domain::PINNED_HOST_MEMORY: return "PINNED_HOST_MEMORY";
    case Domain::HOST_MEMORY: return "HOST_MEMORY";
    case Domain::NUMA_NODE_MEMORY: return "NUMA_NODE_MEMORY";
    case Domain::SHARED_MEMORY_POOL: return "SHARED_MEMORY_POOL";
    case Domain::STORAGE_CAPACITY: return "STORAGE_CAPACITY";
    case Domain::STORAGE_BANDWIDTH: return "STORAGE_BANDWIDTH";
    case Domain::PCIe_BANDWIDTH: return "PCIe_BANDWIDTH";
    case Domain::NETWORK_BANDWIDTH: return "NETWORK_BANDWIDTH";
    case Domain::ACCELERATOR_SLOT: return "ACCELERATOR_SLOT";
    case Domain::MODEL_RESIDENCY: return "MODEL_RESIDENCY";
    case Domain::ADAPTER_RESIDENCY: return "ADAPTER_RESIDENCY";
    case Domain::KV_OR_TENSOR_RESIDENCY: return "KV_OR_TENSOR_RESIDENCY";
    case Domain::RESERVATION_TIMELINE: return "RESERVATION_TIMELINE";
    case Domain::PLACEMENT_TOPOLOGY: return "PLACEMENT_TOPOLOGY";
    case Domain::COMPOSITE_RESOURCE: return "COMPOSITE_RESOURCE";
    case Domain::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// Fragmentation category.  Different categories have different remediability
/// and causality.  Fragmentation is never collapsed into a single ratio.
enum class FragmentationCategory : std::uint8_t {
  SPATIAL,
  CONTIGUITY,
  SIZE_CLASS,
  ALLOCATOR,
  INTERNAL,
  EXTERNAL,
  TOPOLOGY,
  LOCALITY,
  RESERVATION,
  TEMPORAL,
  PLACEMENT,
  CAPABILITY,
  OWNERSHIP,
  PINNING,
  IMMOBILITY,
  RESIDENCY,
  BANDWIDTH,
  COMPOSITE,
  POLICY,
  UNKNOWN,
};

inline constexpr std::string_view to_string(FragmentationCategory c) noexcept {
  switch (c) {
    case FragmentationCategory::SPATIAL: return "SPATIAL";
    case FragmentationCategory::CONTIGUITY: return "CONTIGUITY";
    case FragmentationCategory::SIZE_CLASS: return "SIZE_CLASS";
    case FragmentationCategory::ALLOCATOR: return "ALLOCATOR";
    case FragmentationCategory::INTERNAL: return "INTERNAL";
    case FragmentationCategory::EXTERNAL: return "EXTERNAL";
    case FragmentationCategory::TOPOLOGY: return "TOPOLOGY";
    case FragmentationCategory::LOCALITY: return "LOCALITY";
    case FragmentationCategory::RESERVATION: return "RESERVATION";
    case FragmentationCategory::TEMPORAL: return "TEMPORAL";
    case FragmentationCategory::PLACEMENT: return "PLACEMENT";
    case FragmentationCategory::CAPABILITY: return "CAPABILITY";
    case FragmentationCategory::OWNERSHIP: return "OWNERSHIP";
    case FragmentationCategory::PINNING: return "PINNING";
    case FragmentationCategory::IMMOBILITY: return "IMMOBILITY";
    case FragmentationCategory::RESIDENCY: return "RESIDENCY";
    case FragmentationCategory::BANDWIDTH: return "BANDWIDTH";
    case FragmentationCategory::COMPOSITE: return "COMPOSITE";
    case FragmentationCategory::POLICY: return "POLICY";
    case FragmentationCategory::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// Evidence provenance.  Every fragmentation fact carries provenance;
/// UNKNOWN must remain UNKNOWN and never imply defragmentability.
enum class Provenance : std::uint8_t {
  MEASURED,
  REPORTED,
  DERIVED,
  ESTIMATED,
  RECONSTRUCTED,
  SYNTHETIC,
  UNKNOWN,
};

inline constexpr std::string_view to_string(Provenance p) noexcept {
  switch (p) {
    case Provenance::MEASURED: return "MEASURED";
    case Provenance::REPORTED: return "REPORTED";
    case Provenance::DERIVED: return "DERIVED";
    case Provenance::ESTIMATED: return "ESTIMATED";
    case Provenance::RECONSTRUCTED: return "RECONSTRUCTED";
    case Provenance::SYNTHETIC: return "SYNTHETIC";
    case Provenance::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

/// Real-world evidence classification used by the CUDA proofs and synthetic
/// scenarios (REAL / DERIVED / ESTIMATED / SYNTHETIC / UNKNOWN / UNSUPPORTED).
enum class EvidenceKind : std::uint8_t {
  REAL,
  DERIVED,
  ESTIMATED,
  SYNTHETIC,
  UNKNOWN,
  UNSUPPORTED,
};

inline constexpr std::string_view to_string(EvidenceKind k) noexcept {
  switch (k) {
    case EvidenceKind::REAL: return "REAL";
    case EvidenceKind::DERIVED: return "DERIVED";
    case EvidenceKind::ESTIMATED: return "ESTIMATED";
    case EvidenceKind::SYNTHETIC: return "SYNTHETIC";
    case EvidenceKind::UNKNOWN: return "UNKNOWN";
    case EvidenceKind::UNSUPPORTED: return "UNSUPPORTED";
  }
  return "UNKNOWN";
}

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_CORE_ENUMS_HPP
