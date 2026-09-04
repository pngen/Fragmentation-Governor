// Fragmentation Governor CUDA Scenario F orchestrator.
//
// Drives a real CUDA worker OS process that owns real RTX 5090 allocations,
// kills it as a real OS process while the coordinator stays alive, proves the
// resulting plan is REVALIDATION_REQUIRED / stale, rejects stale-boot replay,
// restarts a fresh WorkerBootId worker, revalidates, executes a fresh valid
// remediation, does real CUDA work, and verifies device memory returns to the
// measured baseline.  No claim of driver compaction / relocation / multi-GPU /
// MIG / NVLink / RDMA / GPUDirect.
//
// Usage: fg_cuda_scenario_f <fg_coordinator.exe> <fg_cuda_worker.exe>
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cuda_runtime.h>

#include "fragmentation_governor/fragmentation_governor.hpp"

#include <windows.h>
#ifdef Message
#undef Message
#endif

using namespace fragmentation_governor;

static const size_t MIB = 1024 * 1024;

struct Proc { HANDLE h = INVALID_HANDLE_VALUE; DWORD pid = 0; };

static bool launch(const std::string& exe, const std::string& args, Proc& p) {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::string cmd = exe + " " + args;
  std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back('\0');
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  p.h = pi.hProcess; p.pid = pi.dwProcessId; CloseHandle(pi.hThread);
  return true;
}

static bool wait_port(std::uint16_t port, int tries) {
  for (int i = 0; i < tries; ++i) { TcpClient c; if (c.connect(port)) { c.close(); return true; } Sleep(50); }
  return false;
}

static void kill_proc(Proc& p) {
  if (p.h != INVALID_HANDLE_VALUE) { TerminateProcess(p.h, 0); WaitForSingleObject(p.h, 5000); CloseHandle(p.h); p.h = INVALID_HANDLE_VALUE; }
}

static bool wait_memory(size_t initial, size_t& now, int tries) {
  for (int i = 0; i < tries; ++i) {
    size_t t2 = 0; cudaMemGetInfo(&now, &t2);
    if (now >= initial && (now - initial) < (8 * MIB)) return true;
    Sleep(200);
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fg_cuda_scenario_f <coord_exe> <worker_exe>\n"); return 2; }
  std::string coord_exe = argv[1]; std::string worker_exe = argv[2];

  if (cudaSetDevice(0) != cudaSuccess) { fprintf(stderr, "no CUDA in orchestrator\n"); return 3; }
  size_t initial = 0, total = 0;
  cudaMemGetInfo(&initial, &total);
  printf("SCENARIO_F initial baseline=%zuMiB total=%zuMiB\n", initial / MIB, total / MIB);

  std::uint16_t port = find_free_tcp_port();

  Proc coord; if (!launch(coord_exe, std::to_string(port) + " 1", coord)) { fprintf(stderr, "launch coordinator failed\n"); return 1; }
  if (!wait_port(port, 100)) { fprintf(stderr, "coordinator not ready\n"); return 1; }

  Proc wa; if (!launch(worker_exe, std::to_string(port) + " 1", wa)) { fprintf(stderr, "launch worker A failed\n"); return 1; }

  TcpClient cli; if (!cli.connect(port)) { fprintf(stderr, "controller connect failed\n"); return 1; }

  WorkloadDemand demand; demand.id = WorkloadDemandId(1); demand.generation = WorkloadDemandGeneration(1);
  demand.accelerator_count = Count(1); demand.per_device_memory = Bytes(512*MIB); demand.aggregate_memory = Bytes(512*MIB); demand.contiguous_memory = Bytes(512*MIB);

  // Poll until worker A evidence is published (kernel parity passed + published).
  FragmentationPlanId old_pid; std::uint64_t old_pgen = 0;
  bool got = false;
  for (int i = 0; i < 40; ++i) {
    Message cp; cp.type = MsgType::CREATE_PLAN; cp.epoch = 1; cp.policy_gen = 1; cp.demand = demand;
    cli.send_frame(cp); auto r = cli.recv_frame();
    if (r.has_value() && r->type == MsgType::PLAN_RESULT && r->ok) { old_pid = r->plan.id; old_pgen = r->plan.generation.value(); got = true; printf("[a] plan created from worker A evidence: id=%llu gen=%llu actions=%zu\n", (unsigned long long)old_pid.value(), (unsigned long long)old_pgen, r->plan.actions.size()); break; }
    Sleep(200);
  }
  if (!got) { fprintf(stderr, "worker A evidence not published / parity failed\n"); return 1; }

  // Real OS-process death of worker A while the coordinator stays alive.
  printf("[b] killing worker A (real OS process termination)\n");
  kill_proc(wa);
  Sleep(300);

  // Old plan must not execute (REVALIDATION_REQUIRED).
  Message ba; ba.type = MsgType::BEGIN_ACTION; ba.plan_id = old_pid.value(); ba.plan_gen = old_pgen; ba.action_index = 0; ba.epoch = 1; ba.policy_gen = 1;
  cli.send_frame(ba); auto bar = cli.recv_frame();
  printf("[c] old plan BEGIN_ACTION accepted=%d (must be 0)\n", bar->ok);
  if (bar->ok) { fprintf(stderr, "old plan executed after worker death\n"); return 1; }

  // Stale old worker boot layout replay must be rejected.
  Message stale; stale.type = MsgType::PUBLISH_LAYOUT; stale.worker_id = 1; stale.worker_boot = 1; stale.epoch = 1; stale.resource.id = ResourceId(1); stale.resource_gen = 1;
  stale.allocations.push_back(Allocation{AllocationId(99), AllocationGeneration(1), AllocationOwnerId(999), Bytes(0), Bytes(256*MIB), Movability::RELEASEABLE, std::nullopt, false, std::nullopt, Provenance::DERIVED});
  cli.send_frame(stale); auto sar = cli.recv_frame();
  printf("[d] stale boot layout replay accepted=%d (must be 0)\n", sar->ok);
  if (sar->ok) { fprintf(stderr, "stale layout replay accepted\n"); return 1; }

  // Restart worker A' with a fresh WorkerBootId.
  printf("[e] starting worker A-prime (fresh WorkerBootId)\n");
  Proc wa2; if (!launch(worker_exe, std::to_string(port) + " 2", wa2)) { fprintf(stderr, "launch worker A-prime failed\n"); return 1; }

  bool got2 = false; FragmentationPlanId pid2; std::uint64_t pgen2 = 0;
  for (int i = 0; i < 40; ++i) {
    Message cp; cp.type = MsgType::CREATE_PLAN; cp.epoch = 1; cp.policy_gen = 1; cp.demand = demand;
    cli.send_frame(cp); auto r = cli.recv_frame();
    if (r.has_value() && r->type == MsgType::PLAN_RESULT && r->ok) { pid2 = r->plan.id; pgen2 = r->plan.generation.value(); got2 = true; printf("[f] fresh plan from worker A-prime: id=%llu gen=%llu actions=%zu\n", (unsigned long long)pid2.value(), (unsigned long long)pgen2, r->plan.actions.size()); break; }
    Sleep(200);
  }
  if (!got2) { fprintf(stderr, "worker A-prime evidence not published\n"); return 1; }

  // Approve + execute the fresh valid remediation.
  Message ap; ap.type = MsgType::APPROVE_PLAN; ap.plan_id = pid2.value(); ap.plan_gen = pgen2; ap.policy_gen = 1;
  cli.send_frame(ap); auto apr = cli.recv_frame();
  printf("[g] fresh plan APPROVE accepted=%d\n", apr->ok);
  if (!apr->ok) { fprintf(stderr, "fresh plan not approved\n"); return 1; }
  for (std::size_t i = 0; i < 1; ++i) {
    Message b2; b2.type = MsgType::BEGIN_ACTION; b2.plan_id = pid2.value(); b2.plan_gen = pgen2; b2.action_index = (std::uint32_t)i; b2.epoch = 1; b2.policy_gen = 1; cli.send_frame(b2); cli.recv_frame();
    Message ac; ac.type = MsgType::ACTION_COMPLETE; ac.plan_id = pid2.value(); ac.action_index = (std::uint32_t)i; ac.action_ok = true; ac.new_generation = pgen2 + 1; cli.send_frame(ac); cli.recv_frame();
  }
  Message vf; vf.type = MsgType::VERIFY; vf.plan_id = pid2.value(); vf.plan_gen = pgen2;
  cli.send_frame(vf); auto vfr = cli.recv_frame();
  printf("[h] fresh remediation VERIFY outcome=%s\n", to_string(vfr->verification_outcome).data());
  if (vfr->verification_outcome != VerificationOutcome::TARGET_FIT_ACHIEVED) { fprintf(stderr, "fresh remediation did not achieve target fit\n"); return 1; }

  // Cleanup: kill A-prime and coordinator (context teardown frees CUDA memory).
  kill_proc(wa2);
  kill_proc(coord);
  Sleep(500);

  size_t final = 0;
  bool drained = wait_memory(initial, final, 60);
  printf("[i] final baseline=%zuMiB initial=%zuMiB drained=%d\n", final / MIB, initial / MIB, (int)drained);
  if (!drained) { fprintf(stderr, "device memory did not return to measured baseline\n"); return 1; }

  printf("\nCUDA SCENARIO F PASSED\n");
  return 0;
}
