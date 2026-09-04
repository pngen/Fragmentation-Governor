#include "fragmentation_governor/protocol/protocol.hpp"

#include <bit>
#include <limits>

namespace fragmentation_governor {

namespace {
struct PWriter {
  std::vector<std::uint8_t>& b;
  explicit PWriter(std::vector<std::uint8_t>& out) : b(out) {}
  void u8(std::uint8_t v) { b.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8*i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8*i)) & 0xFF)); }
  void raw(const std::uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
  void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
};
struct PReader {
  const std::uint8_t* p;
  const size_t sz;
  size_t pos = 0;
  bool ok = true;
  std::string err;
  PReader(const std::uint8_t* d, size_t n) : p(d), sz(n) {}
  bool range(size_t n) { if (n > sz - pos) { ok = false; err = "truncated"; return false; } return true; }
  std::uint8_t u8() { if (!range(1)) return 0; return p[pos++]; }
  std::uint32_t u32() { if (!range(4)) return 0; std::uint32_t v=0; for (int i=0;i<4;++i) v |= static_cast<std::uint32_t>(p[pos++]) << (8*i); return v; }
  std::uint64_t u64() { if (!range(8)) return 0; std::uint64_t v=0; for (int i=0;i<8;++i) v |= static_cast<std::uint64_t>(p[pos++]) << (8*i); return v; }
  std::string str() { std::uint32_t n = u32(); if (!ok || !range(n)) return {}; std::string s(reinterpret_cast<const char*>(p+pos), n); pos += n; return s; }
  bool b1() { return u8() != 0; }
};

std::uint32_t crc32(const std::uint8_t* d, size_t n) {
  static std::uint32_t table[256]; static bool init = false;
  if (!init) { for (std::uint32_t i=0;i<256;++i) { std::uint32_t c=i; for (int k=0;k<8;++k) c=(c&1)?(0xEDB88320u^(c>>1)):(c>>1); table[i]=c; } init=true; }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (size_t i=0;i<n;++i) crc = table[(crc ^ d[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

bool valid_movability(std::uint8_t v) { return v <= 9; }
bool valid_domain(std::uint8_t v) { return v <= 17; }
bool valid_provenance(std::uint8_t v) { return v <= 6; }
bool valid_action(std::uint8_t v) { return v <= 18; }
bool valid_plan_state(std::uint8_t v) { return v <= 18; }
bool valid_verification(std::uint8_t v) { return v <= 6; }
bool valid_res_impact(std::uint8_t v) { return v <= 3; }

void put_alloc(PWriter& w, const Allocation& a) {
  w.u64(a.id.value()); w.u64(a.generation.value()); w.u64(a.owner.value());
  w.u64(a.offset.value()); w.u64(a.length.value());
  w.u8(static_cast<std::uint8_t>(a.movability));
  w.u8(a.reservation.has_value()?1:0); if (a.reservation.has_value()) w.u64(a.reservation->value());
  w.u8(a.policy_protected?1:0);
  w.u8(a.workload.has_value()?1:0); if (a.workload.has_value()) w.u64(a.workload->value());
  w.u8(static_cast<std::uint8_t>(a.provenance));
}
bool get_alloc(PReader& r, Allocation& a) {
  a.id = AllocationId(r.u64()); a.generation = AllocationGeneration(r.u64()); a.owner = AllocationOwnerId(r.u64());
  a.offset = Bytes(r.u64()); a.length = Bytes(r.u64());
  std::uint8_t mv = r.u8(); if (!valid_movability(mv)) { r.ok=false; r.err="bad movability"; return false; } a.movability = static_cast<Movability>(mv);
  if (r.b1()) a.reservation = ReservationId(r.u64());
  a.policy_protected = r.b1();
  if (r.b1()) a.workload = WorkloadId(r.u64());
  std::uint8_t pr = r.u8(); if (!valid_provenance(pr)) { r.ok=false; r.err="bad provenance"; return false; } a.provenance = static_cast<Provenance>(pr);
  return r.ok;
}

void put_demand(PWriter& w, const WorkloadDemand& d) {
  w.u64(d.id.value()); w.u64(d.generation.value());
  w.u64(d.accelerator_count.value());
  w.u64(d.per_device_memory.value()); w.u64(d.aggregate_memory.value()); w.u64(d.contiguous_memory.value()); w.u64(d.pinned_memory.value());
  w.u8(d.topology_group.has_value()?1:0); if (d.topology_group.has_value()) w.u64(static_cast<std::uint64_t>(*d.topology_group));
  w.u8(d.locality_group.has_value()?1:0); if (d.locality_group.has_value()) w.u64(static_cast<std::uint64_t>(*d.locality_group));
  w.u8(d.requires_highspeed_comm_path?1:0);
  w.u8(d.reservation_duration.has_value()?1:0); if (d.reservation_duration.has_value()) w.u64(d.reservation_duration->nanoseconds());
  w.u8(d.requires_model_residency?1:0); w.u8(d.requires_adapter_residency?1:0);
  w.u8(d.elastic_min.has_value()?1:0); if (d.elastic_min.has_value()) w.u64(d.elastic_min->value());
  w.u8(d.elastic_max.has_value()?1:0); if (d.elastic_max.has_value()) w.u64(d.elastic_max->value());
}
bool get_demand(PReader& r, WorkloadDemand& d) {
  d.id = WorkloadDemandId(r.u64()); d.generation = WorkloadDemandGeneration(r.u64());
  d.accelerator_count = Count(r.u64());
  d.per_device_memory = Bytes(r.u64()); d.aggregate_memory = Bytes(r.u64()); d.contiguous_memory = Bytes(r.u64()); d.pinned_memory = Bytes(r.u64());
  if (r.b1()) d.topology_group = static_cast<std::int64_t>(r.u64());
  if (r.b1()) d.locality_group = static_cast<std::int64_t>(r.u64());
  d.requires_highspeed_comm_path = r.b1();
  if (r.b1()) d.reservation_duration = Duration(r.u64());
  d.requires_model_residency = r.b1(); d.requires_adapter_residency = r.b1();
  if (r.b1()) d.elastic_min = Bytes(r.u64());
  if (r.b1()) d.elastic_max = Bytes(r.u64());
  if (d.elastic_min.has_value() && d.elastic_max.has_value() && d.elastic_min->value() > d.elastic_max->value()) { r.ok=false; r.err="elastic min>max"; return false; }
  return r.ok;
}

void put_resource(PWriter& w, const Resource& r) {
  w.u64(r.id.value()); w.u64(r.generation.value()); w.u64(r.pool.value()); w.u64(r.device.value()); w.u64(r.node.value()); w.u64(r.memory_domain.value());
  w.u8(r.topology_group.has_value()?1:0); if (r.topology_group.has_value()) w.u64(static_cast<std::uint64_t>(*r.topology_group));
  w.u8(static_cast<std::uint8_t>(r.domain)); w.u64(r.capacity.value()); w.u8(static_cast<std::uint8_t>(r.provenance));
}
bool get_resource(PReader& r, Resource& res) {
  res.id = ResourceId(r.u64()); res.generation = ResourceGeneration(r.u64()); res.pool = ResourcePoolId(r.u64()); res.device = DeviceId(r.u64()); res.node = NodeId(r.u64()); res.memory_domain = MemoryDomainId(r.u64());
  if (r.b1()) res.topology_group = static_cast<std::int64_t>(r.u64());
  std::uint8_t d = r.u8(); if (!valid_domain(d)) { r.ok=false; r.err="bad domain"; return false; } res.domain = static_cast<Domain>(d);
  res.capacity = Bytes(r.u64());
  std::uint8_t pr = r.u8(); if (!valid_provenance(pr)) { r.ok=false; r.err="bad provenance"; return false; } res.provenance = static_cast<Provenance>(pr);
  return r.ok;
}

void put_plan(PWriter& w, const RemediationPlan& p) {
  w.u64(p.id.value()); w.u64(p.generation.value()); w.u64(p.domain_id.value());
  w.u64(p.based_on_snapshot.value()); w.u64(p.policy_generation.value()); w.u64(p.epoch.value());
  w.u32(static_cast<std::uint32_t>(p.actions.size()));
  for (const auto& a : p.actions) {
    w.u8(static_cast<std::uint8_t>(a.kind)); w.u64(a.target_resource.value()); w.u64(a.affected_owner.value());
    w.u8(a.source_allocation.has_value()?1:0); if (a.source_allocation.has_value()) w.u64(a.source_allocation->value());
    w.u8(a.destination.has_value()?1:0); if (a.destination.has_value()) w.u64(a.destination->value());
    w.u32(static_cast<std::uint32_t>(a.affected_allocations.size())); for (const auto& x : a.affected_allocations) w.u64(x.value());
    w.u64(a.required_authority.value());
    w.u32(static_cast<std::uint32_t>(a.required_preconditions.size())); for (const auto& s : a.required_preconditions) w.str(s);
    w.u32(static_cast<std::uint32_t>(a.proof_obligations.size())); for (const auto& s : a.proof_obligations) w.str(s);
    w.u64(a.expected_capacity_recovered.value());
    w.u64(std::bit_cast<std::uint64_t>(a.expected_fit_improvement.value()));
    w.u64(std::bit_cast<std::uint64_t>(a.cost.value())); w.u64(std::bit_cast<std::uint64_t>(a.risk.value())); w.u64(std::bit_cast<std::uint64_t>(a.disruption.value()));
    w.u8(a.reversible?1:0); w.u8(a.state_preservation_required?1:0); w.u8(a.preemption_required?1:0); w.u8(static_cast<std::uint8_t>(a.reservation_impact));
  }
  w.u64(p.expected_capacity_recovered.value());
  w.u64(std::bit_cast<std::uint64_t>(p.expected_fit_improvement.value()));
  w.u64(std::bit_cast<std::uint64_t>(p.benefit.value())); w.u64(std::bit_cast<std::uint64_t>(p.cost.value())); w.u64(std::bit_cast<std::uint64_t>(p.risk.value())); w.u64(std::bit_cast<std::uint64_t>(p.disruption.value()));
  w.u32(static_cast<std::uint32_t>(p.factor_explanation.size())); for (const auto& s : p.factor_explanation) w.str(s);
  w.u8(static_cast<std::uint8_t>(p.state));
  w.u8(p.verification.has_value()?1:0); if (p.verification.has_value()) w.u8(static_cast<std::uint8_t>(*p.verification));
  w.u32(static_cast<std::uint32_t>(p.precondition_failures.size())); for (const auto& s : p.precondition_failures) w.str(s);
}
bool get_plan(PReader& r, RemediationPlan& p) {
  p.id = FragmentationPlanId(r.u64()); p.generation = FragmentationPlanGeneration(r.u64()); p.domain_id = FragmentationDomainId(r.u64());
  p.based_on_snapshot = FragmentationSnapshotGeneration(r.u64()); p.policy_generation = PolicyGeneration(r.u64()); p.epoch = CoordinatorEpoch(r.u64());
  std::uint32_t na = r.u32(); if (!r.ok || na > 100000) { r.err="plan action count"; r.ok=false; return false; }
  for (std::uint32_t i=0;i<na;++i) {
    PlanAction a;
    std::uint8_t k = r.u8(); if (!valid_action(k)) { r.ok=false; r.err="bad action kind"; return false; } a.kind = static_cast<ActionKind>(k);
    a.target_resource = ResourceId(r.u64()); a.affected_owner = AllocationOwnerId(r.u64());
    if (r.b1()) a.source_allocation = AllocationId(r.u64());
    if (r.b1()) a.destination = ResourceId(r.u64());
    std::uint32_t naz = r.u32(); if (!r.ok || naz > 100000) { r.err="affected count"; r.ok=false; return false; }
    for (std::uint32_t j=0;j<naz;++j) a.affected_allocations.push_back(AllocationId(r.u64()));
    a.required_authority = AuthorityGeneration(r.u64());
    std::uint32_t npre = r.u32(); if (!r.ok || npre > 1000) { r.err="precondition count"; r.ok=false; return false; }
    for (std::uint32_t j=0;j<npre;++j) a.required_preconditions.push_back(r.str());
    std::uint32_t nprf = r.u32(); if (!r.ok || nprf > 1000) { r.err="proof count"; r.ok=false; return false; }
    for (std::uint32_t j=0;j<nprf;++j) a.proof_obligations.push_back(r.str());
    a.expected_capacity_recovered = Bytes(r.u64());
    a.expected_fit_improvement = HealthyScore(std::bit_cast<double>(r.u64()));
    a.cost = HealthyScore(std::bit_cast<double>(r.u64())); a.risk = HealthyScore(std::bit_cast<double>(r.u64())); a.disruption = HealthyScore(std::bit_cast<double>(r.u64()));
    a.reversible = r.b1(); a.state_preservation_required = r.b1(); a.preemption_required = r.b1();
    std::uint8_t ri = r.u8(); if (!valid_res_impact(ri)) { r.ok=false; r.err="bad reservation impact"; return false; } a.reservation_impact = static_cast<ReservationImpact>(ri);
    p.actions.push_back(a);
  }
  p.expected_capacity_recovered = Bytes(r.u64());
  p.expected_fit_improvement = HealthyScore(std::bit_cast<double>(r.u64()));
  p.benefit = HealthyScore(std::bit_cast<double>(r.u64())); p.cost = HealthyScore(std::bit_cast<double>(r.u64())); p.risk = HealthyScore(std::bit_cast<double>(r.u64())); p.disruption = HealthyScore(std::bit_cast<double>(r.u64()));
  std::uint32_t nfe = r.u32(); if (!r.ok || nfe > 1000) { r.err="factor explanation count"; r.ok=false; return false; }
  for (std::uint32_t j=0;j<nfe;++j) p.factor_explanation.push_back(r.str());
  std::uint8_t st = r.u8(); if (!valid_plan_state(st)) { r.ok=false; r.err="bad plan state"; return false; } p.state = static_cast<PlanState>(st);
  if (r.b1()) { std::uint8_t vo = r.u8(); if (!valid_verification(vo)) { r.ok=false; r.err="bad verification"; return false; } p.verification = static_cast<VerificationOutcome>(vo); }
  std::uint32_t npf = r.u32(); if (!r.ok || npf > 1000) { r.err="precondition failure count"; r.ok=false; return false; }
  for (std::uint32_t j=0;j<npf;++j) p.precondition_failures.push_back(r.str());
  return r.ok;
}
}  // namespace

std::vector<std::uint8_t> encode_payload(const Message& msg) {
  std::vector<std::uint8_t> out;
  PWriter w(out);
  w.u8(static_cast<std::uint8_t>(msg.type));
  switch (msg.type) {
    case MsgType::REGISTER: w.u64(msg.worker_id); w.u64(msg.worker_boot); w.u64(msg.epoch); break;
    case MsgType::PUBLISH_RESOURCE: w.u64(msg.worker_id); w.u64(msg.worker_boot); w.u64(msg.epoch); put_resource(w, msg.resource); break;
    case MsgType::PUBLISH_LAYOUT: w.u64(msg.worker_id); w.u64(msg.worker_boot); w.u64(msg.epoch); w.u64(msg.resource.id.value()); w.u64(msg.resource_gen); w.u32(static_cast<std::uint32_t>(msg.allocations.size())); for (const auto& a : msg.allocations) put_alloc(w, a); break;
    case MsgType::PUBLISH_ALLOCATION: w.u64(msg.worker_id); w.u64(msg.worker_boot); w.u64(msg.epoch); w.u64(msg.resource.id.value()); put_alloc(w, msg.allocation); break;
    case MsgType::INVALIDATE_ALLOCATION: w.u64(msg.worker_id); w.u64(msg.worker_boot); w.u64(msg.epoch); w.u64(msg.allocation_id); w.u64(msg.new_generation); break;
    case MsgType::QUERY_FRAGMENTATION: w.u64(msg.epoch); break;
    case MsgType::QUERY_FIT: w.u64(msg.epoch); put_demand(w, msg.demand); break;
    case MsgType::CREATE_PLAN: w.u64(msg.epoch); w.u64(msg.policy_gen); put_demand(w, msg.demand); break;
    case MsgType::PLAN_RESULT: put_plan(w, msg.plan); break;
    case MsgType::APPROVE_PLAN: w.u64(msg.plan_id); w.u64(msg.plan_gen); w.u64(msg.policy_gen); break;
    case MsgType::BEGIN_ACTION: w.u64(msg.plan_id); w.u64(msg.plan_gen); w.u32(msg.action_index); w.u64(msg.epoch); w.u64(msg.policy_gen); break;
    case MsgType::ACTION_COMPLETE: w.u64(msg.plan_id); w.u32(msg.action_index); w.u8(msg.action_ok?1:0); w.u64(msg.new_generation); break;
    case MsgType::VERIFY: w.u64(msg.plan_id); w.u64(msg.plan_gen); break;
    case MsgType::CANCEL_PLAN: w.u64(msg.plan_id); w.u64(msg.plan_gen); break;
    case MsgType::ADVANCE_GENERATION: w.u8(msg.generation_kind); w.u64(msg.gen_value); break;
    case MsgType::REVALIDATE: w.u64(msg.plan_id); w.u64(msg.plan_gen); break;
    case MsgType::ERROR: w.str(msg.error); break;
    case MsgType::HELLO: case MsgType::SAVE: case MsgType::SHUTDOWN: break;
  }
  // Uniform result footer.
  w.u8(msg.ok?1:0);
  w.u8(static_cast<std::uint8_t>(msg.fit_outcome));
  w.u8(static_cast<std::uint8_t>(msg.verification_outcome));
  w.str(msg.detail);
  return out;
}

std::optional<Message> decode_payload(const std::uint8_t* data, size_t len) {
  PReader r(data, len);
  Message m;
  std::uint8_t t = r.u8();
  if (!r.ok) return std::nullopt;
  if (t > 19) return std::nullopt;
  m.type = static_cast<MsgType>(t);
  switch (m.type) {
    case MsgType::REGISTER: m.worker_id=r.u64(); m.worker_boot=r.u64(); m.epoch=r.u64(); break;
    case MsgType::PUBLISH_RESOURCE: m.worker_id=r.u64(); m.worker_boot=r.u64(); m.epoch=r.u64(); if(!get_resource(r, m.resource)) return std::nullopt; break;
    case MsgType::PUBLISH_LAYOUT: m.worker_id=r.u64(); m.worker_boot=r.u64(); m.epoch=r.u64(); m.resource.id=ResourceId(r.u64()); m.resource_gen=r.u64(); { std::uint32_t n=r.u32(); if(!r.ok||n>100000) return std::nullopt; for(std::uint32_t i=0;i<n;++i){ Allocation a; if(!get_alloc(r,a)) return std::nullopt; m.allocations.push_back(a);} } break;
    case MsgType::PUBLISH_ALLOCATION: m.worker_id=r.u64(); m.worker_boot=r.u64(); m.epoch=r.u64(); m.resource.id=ResourceId(r.u64()); if(!get_alloc(r, m.allocation)) return std::nullopt; break;
    case MsgType::INVALIDATE_ALLOCATION: m.worker_id=r.u64(); m.worker_boot=r.u64(); m.epoch=r.u64(); m.allocation_id=r.u64(); m.new_generation=r.u64(); break;
    case MsgType::QUERY_FRAGMENTATION: m.epoch=r.u64(); break;
    case MsgType::QUERY_FIT: m.epoch=r.u64(); if(!get_demand(r, m.demand)) return std::nullopt; break;
    case MsgType::CREATE_PLAN: m.epoch=r.u64(); m.policy_gen=r.u64(); if(!get_demand(r, m.demand)) return std::nullopt; break;
    case MsgType::PLAN_RESULT: if(!get_plan(r, m.plan)) return std::nullopt; break;
    case MsgType::APPROVE_PLAN: m.plan_id=r.u64(); m.plan_gen=r.u64(); m.policy_gen=r.u64(); break;
    case MsgType::BEGIN_ACTION: m.plan_id=r.u64(); m.plan_gen=r.u64(); m.action_index=r.u32(); m.epoch=r.u64(); m.policy_gen=r.u64(); break;
    case MsgType::ACTION_COMPLETE: m.plan_id=r.u64(); m.action_index=r.u32(); m.action_ok=r.b1(); m.new_generation=r.u64(); break;
    case MsgType::VERIFY: m.plan_id=r.u64(); m.plan_gen=r.u64(); break;
    case MsgType::CANCEL_PLAN: m.plan_id=r.u64(); m.plan_gen=r.u64(); break;
    case MsgType::ADVANCE_GENERATION: m.generation_kind=r.u8(); m.gen_value=r.u64(); break;
    case MsgType::REVALIDATE: m.plan_id=r.u64(); m.plan_gen=r.u64(); break;
    case MsgType::ERROR: m.error=r.str(); break;
    case MsgType::HELLO: case MsgType::SAVE: case MsgType::SHUTDOWN: break;
  }
  if (!r.ok) return std::nullopt;
  // Uniform result footer.
  m.ok = r.b1();
  std::uint8_t fo = r.u8(); if (fo > 16) return std::nullopt; m.fit_outcome = static_cast<FitOutcome>(fo);
  std::uint8_t vo = r.u8(); if (vo > 6) return std::nullopt; m.verification_outcome = static_cast<VerificationOutcome>(vo);
  m.detail = r.str();
  if (!r.ok) return std::nullopt;
  if (r.pos != len) return std::nullopt;
  return m;
}

std::vector<std::uint8_t> frame_encode(const Message& msg) {
  std::vector<std::uint8_t> payload = encode_payload(msg);
  std::vector<std::uint8_t> out;
  PWriter w(out);
  w.u32(protocol_magic()); w.u32(protocol_version()); w.u32(static_cast<std::uint32_t>(payload.size()));
  w.raw(payload.data(), payload.size());
  std::uint32_t crc = crc32(out.data(), out.size());
  w.u32(crc);
  return out;
}

std::optional<Message> frame_decode(const std::uint8_t* data, size_t len) {
  if (len < 16) return std::nullopt;
  PReader r(data, len);
  if (r.u32() != protocol_magic()) return std::nullopt;
  if (r.u32() != protocol_version()) return std::nullopt;
  std::uint32_t plen = r.u32();
  if (!r.ok) return std::nullopt;
  if (plen > protocol_max_payload()) return std::nullopt;
  if (len != 12 + plen + 4) return std::nullopt;
  std::vector<std::uint8_t> payload(plen);
  for (std::uint32_t i=0;i<plen;++i) payload[i] = r.u8();
  std::uint32_t stored = r.u32();
  if (!r.ok) return std::nullopt;
  std::uint32_t computed = crc32(data, 12 + plen);
  if (stored != computed) return std::nullopt;
  return decode_payload(payload.data(), payload.size());
}

std::optional<Message> FrameDecoder::feed(const std::uint8_t* data, size_t len) {
  buf_.insert(buf_.end(), data, data + len);
  while (true) {
    if (buf_.size() < 16) return std::nullopt;
    std::uint32_t plen = 0;
    for (int i=0;i<4;++i) plen |= static_cast<std::uint32_t>(buf_[8+i]) << (8*i);
    if (plen > protocol_max_payload()) { reset(); return std::nullopt; }
    const size_t frame_len = 12u + plen + 4u;
    if (buf_.size() < frame_len) return std::nullopt;
    auto msg = frame_decode(buf_.data(), frame_len);
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(frame_len));
    return msg;
  }
}

}  // namespace fragmentation_governor