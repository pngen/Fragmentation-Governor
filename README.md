# Fragmentation Governor

**Fragmentation Governor is an open-source, vendor-neutral C++20 runtime for detecting, quantifying, explaining, and governing stranded accelerator, host-memory, allocator, reservation, and placement capacity across heterogeneous AI infrastructure.**

It answers one systems question:

**How much capacity is stranded rather than truly usable, why is it fragmented, which workloads are prevented from fitting, and what safe policy action would recover the most useful capacity without violating current authority, reservations, or workload guarantees?**

The defining thesis:

**Fragmentation is not free-space percentage. It is the gap between capacity that exists and capacity that a real workload can actually use under current shape, ownership, topology, reservation, and authority constraints.**

A system may report substantial free VRAM, pinned host memory, device-local pools, reservation capacity, accelerator slots, topology-compatible resources, bandwidth, model residency, or temporal availability while still being unable to admit a workload because the available resource is split into unusable pieces. Fragmentation Governor makes that stranded-capacity problem explicit and governable.

## Systems boundary

Fragmentation Governor sits beside, and does not replace, several other fabrics:

- **Capacity Fabric** models usable and future capacity.
- **Reservation Fabric** governs advance commitments.
- **Resource Broker** governs current scarce-resource arbitration.
- **Preemption Fabric** governs safe interruption.
- **Workload Fabric** governs workload lifecycle.
- **Execution Fabric** governs execution-attempt authority.
- topology / NUMA / PCIe / fleet runtimes provide physical evidence.
- allocator / runtime layers perform actual allocation mechanics.

Fragmentation Governor detects stranded capacity, explains its cause, selects safe remediation intent, and verifies whether useful capacity was actually recovered. It does not own generic arbitration, forecasting, reservation mutation, scheduling, workload lifecycle, execution authority, preemption, allocator implementation, topology discovery, or migration.

## Core doctrine

Free capacity is not usable capacity. Aggregate capacity is not contiguous capacity. Contiguous capacity is not topology-compatible capacity. Topology-compatible capacity is not reservation-compatible capacity. Reservation-compatible capacity is not immediately movable capacity. A defragmentation suggestion is not an executed remediation. An executed remediation is not successful unless stranded capacity actually decreases or target workload feasibility improves.

Fragmentation is modeled as a relationship between **resource shape / workload shape / current ownership / authority / topology / time / policy**. The runtime distinguishes nominal capacity, free capacity, largest usable block, workload-specific usable capacity, stranded capacity, reclaimable capacity, movable capacity, protected capacity, committed capacity, unusable capacity, and unknown capacity.

## What is implemented (1.0.0)

- **Strongly typed identities and generations.** Every identity (FragmentationDomainId, ResourceId, AllocationId, ReservationId, WorkloadDemandId, DeviceId, ...) and every generation (FragmentationPlanGeneration, ResourceGeneration, AllocationGeneration, ReservationGeneration, PolicyGeneration, WorkerBootId, CoordinatorEpoch, ...) is a distinct, non-interconvertible type.
- **Fragmentation domains.** CUDA_DEVICE_MEMORY, ACCELERATOR_MEMORY_POOL, PINNED_HOST_MEMORY, HOST_MEMORY, NUMA_NODE_MEMORY, SHARED_MEMORY_POOL, STORAGE_CAPACITY, STORAGE_BANDWIDTH, PCIe_BANDWIDTH, NETWORK_BANDWIDTH, ACCELERATOR_SLOT, MODEL_RESIDENCY, ADAPTER_RESIDENCY, KV_OR_TENSOR_RESIDENCY, RESERVATION_TIMELINE, PLACEMENT_TOPOLOGY, COMPOSITE_RESOURCE, UNKNOWN.
- **Evidence provenance.** MEASURED / REPORTED / DERIVED / ESTIMATED / RECONSTRUCTED / SYNTHETIC / UNKNOWN. UNKNOWN never implies defragmentability.
- **Canonical resource-shape model.** A ResourceLayout is an exact interval map of the resource so that used + free == total at all times, with an indexed free-block length set for O(log n) largest-free-block queries.
- **Shape-aware fit.** Deterministic outcome taxonomy: FIT_NOW, NO_FIT_RAW_CAPACITY, NO_FIT_FRAGMENTATION, NO_FIT_CONTIGUITY, NO_FIT_TOPOLOGY, NO_FIT_LOCALITY, NO_FIT_RESERVATION, NO_FIT_TEMPORAL, NO_FIT_PROTECTED_STATE, FIT_AFTER_RECLAIM, FIT_AFTER_RELOCATION, FIT_AFTER_RESERVATION_CHANGE, FIT_AFTER_PREEMPTION, FIT_AFTER_REVALIDATION, REVALIDATION_REQUIRED, INSUFFICIENT_EVIDENCE, UNKNOWN.
- **Fragmentation metrics.** Free, usable, largest-usable-block, stranded, reclaimable, protected, movable, external-fragmentation ratio, reclaimable opportunity, and workload-specific stranded capacity.
- **Movability semantics and protection.** IMMOVABLE, MOVABLE_LIVE, MOVABLE_AFTER_QUIESCE, MOVABLE_AFTER_CHECKPOINT, RECOMPUTABLE, EVICTABLE, RELEASEABLE, RESERVATION_PROTECTED, POLICY_PROTECTED, UNKNOWN. may_move(UNKNOWN) is false by design.
- **Deterministic remediation planning.** PlanActions with target resource, affected owner, source/destination, required authority, preconditions, proof obligations, expected capacity, fit improvement, cost, risk, disruption, reversibility, and reservation impact; ranked by named, transparent factors.
- **Guarded lifecycle.** DETECTED, ASSESSING, PLAN_READY, BLOCKED, APPROVED, EXECUTING, QUIESCING, PRESERVING_STATE, MOVING, REBINDING, VERIFYING, COMPLETED, PARTIALLY_COMPLETED, FAILED, ABORTED, CANCELLED, SUPERSEDED, REVALIDATION_REQUIRED, RETIRED.
- **Mandatory verification.** IMPROVED, TARGET_FIT_ACHIEVED, PARTIAL_IMPROVEMENT, NO_CHANGE, REGRESSION, VERIFICATION_FAILED, REVALIDATION_REQUIRED.
- **Reservation and temporal fragmentation.** Interval-index analysis computing the earliest continuous fit and stranded short windows.
- **Topology fragmentation.** Devices outside the required topology group are excluded; aggregate capacity split across incompatible groups yields NO_FIT_TOPOLOGY.
- **Versioned persistence with integrity.** Magic/versioned binary codec, CRC32, bounded lengths, enum validation, duplicate-id / generation-regression / completed-without-verification rejection, and conservative recovery.
- **Framed protocol.** Checksummed frame format with bounded payloads, invalid-enum rejection, trailing-garbage rejection, and a streaming decoder tolerant of partial reads/writes.
- **Coordinator.** Generation-fenced in-process coordinator plus a real loopback TCP server driving registration, publication, snapshot assembly, fit, planning, execution, verification, worker-death handling, and recovery.
- **Real multiprocess proof.** Separate coordinator and two worker OS processes over framed TCP: primary distributed scenario, real OS-process worker death (plan degrades to REVALIDATION_REQUIRED, stale WorkerBootId publish rejected), and coordinator restart (durable state recovered, dynamic evidence revalidated).
- **CUDA proof on RTX 5090 / sm_120.** Scenarios A (controlled fragmentation, DERIVED), B (release remediation with kernel CPU parity), C (governed application-level relocation), D (protected refusal / NO_BENEFICIAL_ACTION), E (stale authority rejection), F (real CUDA worker OS-process death and revalidation), G (coordinator restart + revalidation). Device memory verified to return to baseline. No claim of CUDA-internal free-list state (not observable), driver compaction, or driver memory relocation.

## Repository layout

- include/fragmentation_governor/ - public headers.
- src/ - library implementation.
- tests/ - unit, property, adversarial, concurrency, and multiprocess proof tests.
- benchmarks/ - 1k / 10k / 100k allocation-scale benchmarks.
- examples/ - runnable examples.
- tools/ - fg_coordinator, fg_worker, fg_inspect.
- cuda/ - the RTX 5090 / sm_120 proof.
- cmake/ - install / package-config machinery.

## Build and test

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

The library installs and exports FragmentationGovernor::FragmentationGovernor; a downstream consumer locates it with find_package(FragmentationGovernor CONFIG REQUIRED).

## Known limitations

- CUDA Scenario F (worker death with CUDA allocations) is validated in the multiprocess proof via real OS-process termination; the CUDA proof does not spawn a separate CUDA worker subprocess.
- No physical multi-GPU, MIG, NVLink, NVSwitch, RDMA, GPUDirect, hardware allocator compaction, driver-level memory relocation, or device reset is claimed or measured.
- UNKNOWN physical facts are never promoted to current evidence.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.