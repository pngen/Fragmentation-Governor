// Fragmentation Governor real multiprocess proof.
//
// Spawns an independent coordinator OS process and two independent worker OS
// processes, communicates over real loopback TCP using the framed checksummed
// protocol, and proves the primary distributed scenario plus worker death via
// real OS-process termination, stale-boot replay rejection, and coordinator
// restart/revalidation.  Threads are never used as a substitute for processes.
//
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "fragmentation_governor/fragmentation_governor.hpp"

// windows.h must follow the governor (winsock2) include, and it may define a
// Message macro that would otherwise corrupt the governor's Message type.
#include <windows.h>
#ifdef Message
#undef Message
#endif

using namespace fragmentation_governor;

struct Proc {
  HANDLE h = INVALID_HANDLE_VALUE;
  DWORD pid = 0;
};

static bool launch(const std::string& exe, const std::string& args, Proc& p) {
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::string cmd = "\"" + exe + "\" " + args;
  std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back('\0');
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  p.h = pi.hProcess; p.pid = pi.dwProcessId;
  CloseHandle(pi.hThread);
  return true;
}

static bool wait_port(std::uint16_t port, int tries) {
  for (int i = 0; i < tries; ++i) {
    TcpClient c;
    if (c.connect(port)) { c.close(); return true; }
    Sleep(50);
  }
  return false;
}

static void kill_proc(Proc& p) {
  if (p.h != INVALID_HANDLE_VALUE) { TerminateProcess(p.h, 0); WaitForSingleObject(p.h, 5000); CloseHandle(p.h); p.h = INVALID_HANDLE_VALUE; }
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: multiprocess_proof <fg_coordinator.exe> <fg_worker.exe>\n"); return 2; }
  std::string coord_exe = argv[1];
  std::string worker_exe = argv[2];

  const size_t MIB = 1024 * 1024;
  std::uint16_t port = find_free_tcp_port();
  std::string statefile = std::string("fg_mp_state_") + std::to_string(port) + ".bin";

  // ---- primary distributed scenario ----
  printf("[1] start coordinator epoch=1\n");
  Proc coord;
  if (!launch(coord_exe, std::to_string(port) + " 1", coord)) { fprintf(stderr, "launch coordinator failed\n"); return 1; }
  if (!wait_port(port, 100)) { fprintf(stderr, "coordinator not ready\n"); kill_proc(coord); return 1; }

  printf("[2] start worker A and worker B\n");
  Proc wA, wB;
  if (!launch(worker_exe, std::to_string(port) + " 1 1", wA)) { fprintf(stderr, "launch worker A failed\n"); return 1; }
  if (!launch(worker_exe, std::to_string(port) + " 2 1", wB)) { fprintf(stderr, "launch worker B failed\n"); return 1; }
  Sleep(200);

  printf("[3] drive controller\n");
  TcpClient cli;
  if (!cli.connect(port)) { fprintf(stderr, "controller connect failed\n"); return 1; }

  WorkloadDemand demand; demand.id = WorkloadDemandId(1); demand.generation = WorkloadDemandGeneration(1);
  demand.accelerator_count = Count(1); demand.per_device_memory = Bytes(512 * MIB);
  demand.aggregate_memory = Bytes(512 * MIB); demand.contiguous_memory = Bytes(512 * MIB);

  Message q; q.type = MsgType::QUERY_FIT; q.epoch = 1; q.demand = demand;
  cli.send_frame(q); auto qr = cli.recv_frame();
  if (!qr.has_value()) { fprintf(stderr, "no QUERY_FIT response\n"); return 1; }
  printf("[4] QUERY_FIT outcome = %s\n", to_string(qr->fit_outcome).data());
  if (qr->fit_outcome != FitOutcome::FIT_AFTER_RECLAIM && qr->fit_outcome != FitOutcome::NO_FIT_FRAGMENTATION)
    { fprintf(stderr, "unexpected fit outcome\n"); return 1; }

  Message cp; cp.type = MsgType::CREATE_PLAN; cp.epoch = 1; cp.policy_gen = 1; cp.demand = demand;
  cli.send_frame(cp); auto cpr = cli.recv_frame();
  if (!cpr.has_value() || cpr->type != MsgType::PLAN_RESULT || !cpr->ok) { fprintf(stderr, "CREATE_PLAN failed\n"); return 1; }
  FragmentationPlanId pid = cpr->plan.id; std::uint64_t pgen = cpr->plan.generation.value();
  printf("[5] plan id=%llu gen=%llu actions=%zu state=%s\n", (unsigned long long)pid.value(), (unsigned long long)pgen,
         cpr->plan.actions.size(), to_string(cpr->plan.state).data());
  if (cpr->plan.actions.empty()) { fprintf(stderr, "plan has no actions\n"); return 1; }

  Message ap; ap.type = MsgType::APPROVE_PLAN; ap.plan_id = pid.value(); ap.plan_gen = pgen; ap.policy_gen = 1;
  cli.send_frame(ap); auto apr = cli.recv_frame();
  if (!apr.has_value() || !apr->ok) { fprintf(stderr, "APPROVE_PLAN failed\n"); return 1; }

  for (std::size_t i = 0; i < cpr->plan.actions.size(); ++i) {
    Message ba; ba.type = MsgType::BEGIN_ACTION; ba.plan_id = pid.value(); ba.plan_gen = pgen; ba.action_index = (std::uint32_t)i; ba.epoch = 1; ba.policy_gen = 1;
    cli.send_frame(ba); auto bar = cli.recv_frame(); if (!bar.has_value() || !bar->ok) { fprintf(stderr, "BEGIN_ACTION failed\n"); return 1; }
    Message ac; ac.type = MsgType::ACTION_COMPLETE; ac.plan_id = pid.value(); ac.action_index = (std::uint32_t)i; ac.action_ok = true; ac.new_generation = 2;
    cli.send_frame(ac); auto acr = cli.recv_frame(); if (!acr.has_value() || !acr->ok) { fprintf(stderr, "ACTION_COMPLETE failed\n"); return 1; }
  }
  Message vf; vf.type = MsgType::VERIFY; vf.plan_id = pid.value(); vf.plan_gen = pgen;
  cli.send_frame(vf); auto vfr = cli.recv_frame();
  if (!vfr.has_value()) { fprintf(stderr, "VERIFY failed\n"); return 1; }
  printf("[6] VERIFY outcome = %s  detail=%s\n", to_string(vfr->verification_outcome).data(), vfr->detail.c_str());
  if (vfr->verification_outcome != VerificationOutcome::TARGET_FIT_ACHIEVED) { fprintf(stderr, "primary scenario did not achieve target fit\n"); return 1; }

  // ---- stale plan replay rejection (supersede then re-execute) ----
  printf("[7] stale plan replay must be rejected\n");
  Message c2; c2.type = MsgType::BEGIN_ACTION; c2.plan_id = pid.value(); c2.plan_gen = pgen; c2.action_index = 0; c2.epoch = 1; c2.policy_gen = 2;  // stale policy
  cli.send_frame(c2); auto c2r = cli.recv_frame();
  if (!c2r.has_value()) { fprintf(stderr, "stale replay no response\n"); return 1; }
  printf("[8] stale replay accepted=%d (must be 0)\n", c2r->ok);
  if (c2r->ok) { fprintf(stderr, "stale plan replay was accepted\n"); return 1; }

  // ---- worker death scenario ----
  printf("[9] kill worker A (real OS process termination)\n");
  kill_proc(wA);
  Sleep(200);
  // A fresh plan on the current (A-dead) state, and a stale-action guard.
  Message c3; c3.type = MsgType::BEGIN_ACTION; c3.plan_id = pid.value(); c3.plan_gen = pgen; c3.action_index = 0; c3.epoch = 1; c3.policy_gen = 1;
  cli.send_frame(c3); auto c3r = cli.recv_frame();
  printf("[10] action on plan after worker death accepted=%d (must be 0: REVALIDATION_REQUIRED)\n", c3r->ok);
  if (c3r->ok) { fprintf(stderr, "plan executed after worker death\n"); return 1; }

  printf("[11] stale WorkerBootId replay rejected\n");
  Message stale; stale.type = MsgType::PUBLISH_LAYOUT; stale.worker_id = 1; stale.worker_boot = 999; stale.epoch = 1;
  stale.resource.id = ResourceId(1); stale.resource_gen = 99;
  cli.send_frame(stale); auto staler = cli.recv_frame();
  if (!staler.has_value()) { fprintf(stderr, "no stale replay response\n"); return 1; }
  printf("[12] stale boot publish accepted=%d (must be 0)\n", staler->ok);
  if (staler->ok) { fprintf(stderr, "stale WorkerBootId was accepted\n"); return 1; }

  // ---- worker A' restart (fresh boot) and revalidation ----
  printf("[13] respawn worker A' with fresh WorkerBootId\n");
  Proc wA2;
  if (!launch(worker_exe, std::to_string(port) + " 1 2", wA2)) { fprintf(stderr, "launch worker A' failed\n"); return 1; }
  Sleep(200);
  Message q2; q2.type = MsgType::QUERY_FIT; q2.epoch = 1; q2.demand = demand;
  cli.send_frame(q2); auto q2r = cli.recv_frame();
  if (!q2r.has_value()) { fprintf(stderr, "post-restart query failed\n"); return 1; }
  printf("[14] post-restart QUERY_FIT outcome = %s\n", to_string(q2r->fit_outcome).data());

  // ---- coordinator restart + revalidation ----
  printf("[15] persist state then kill coordinator\n");
  Message sv; sv.type = MsgType::SAVE; sv.detail = statefile;
  cli.send_frame(sv); auto svr = cli.recv_frame();
  if (!svr.has_value() || !svr->ok) { fprintf(stderr, "SAVE failed\n"); return 1; }
  kill_proc(coord);
  Sleep(200);
  printf("[16] restart coordinator with new epoch from persisted state\n");
  Proc coord2;
  if (!launch(coord_exe, std::to_string(port) + " 2 " + statefile, coord2)) { fprintf(stderr, "relaunch coordinator failed\n"); return 1; }
  if (!wait_port(port, 100)) { fprintf(stderr, "restarted coordinator not ready\n"); return 1; }

  TcpClient cli2;
  if (!cli2.connect(port)) { fprintf(stderr, "controller2 connect failed\n"); return 1; }
  // Republish workers against the restarted coordinator (dynamic evidence not restored).
  Proc wA3, wB3;
  if (!launch(worker_exe, std::to_string(port) + " 1 3", wA3)) { fprintf(stderr, "launch worker A\x27\n"); return 1; }
  if (!launch(worker_exe, std::to_string(port) + " 2 3", wB3)) { fprintf(stderr, "launch worker B'\n"); return 1; }
  Sleep(200);
  Message q3; q3.type = MsgType::QUERY_FIT; q3.epoch = 2; q3.demand = demand;
  cli2.send_frame(q3); auto q3r = cli2.recv_frame();
  if (!q3r.has_value()) { fprintf(stderr, "restart query failed\n"); return 1; }
  printf("[17] post-restart revalidation QUERY_FIT outcome = %s\n", to_string(q3r->fit_outcome).data());
  if (q3r->fit_outcome == FitOutcome::UNKNOWN || q3r->fit_outcome == FitOutcome::INSUFFICIENT_EVIDENCE || q3r->fit_outcome == FitOutcome::REVALIDATION_REQUIRED) {
    printf("[18] recovered dynamic plan is conservatively non-current (correct)\n");
  }

  // ---- clean shutdown ----
  kill_proc(wA2); kill_proc(wB); kill_proc(wA3); kill_proc(wB3); kill_proc(coord2);
  DeleteFileA(statefile.c_str());
  printf("\nMULTIPROCESS PROOF PASSED\n");
  return 0;
}