#include "fragmentation_governor/persistence/codec.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace fragmentation_governor {

namespace {

// --- scalar enum range validation ------------------------------------------
bool valid_u8_enum(std::uint8_t v, std::uint8_t min, std::uint8_t max) { return v >= min && v <= max; }
constexpr std::uint8_t V_PLAN_STATE_MIN = 0;
constexpr std::uint8_t V_PLAN_STATE_MAX = 18;  // RETIRED
constexpr std::uint8_t V_ACTION_MIN = 0;
constexpr std::uint8_t V_ACTION_MAX = 18;      // NO_BENEFICIAL_ACTION
constexpr std::uint8_t V_VERIFY_MIN = 0;
constexpr std::uint8_t V_VERIFY_MAX = 6;       // REVALIDATION_REQUIRED

// --- CRC32 (IEEE 802.3, polynomial 0xEDB88320) ------------------------------
std::uint32_t crc32(const std::uint8_t* data, size_t len) {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

// --- bounded writer ---------------------------------------------------------
class Writer {
 public:
  std::vector<std::uint8_t>& b;
  explicit Writer(std::vector<std::uint8_t>& out) : b(out) {}
  void u8(std::uint8_t v) { b.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF)); }
  void raw(const std::uint8_t* p, size_t n) { b.insert(b.end(), p, p + n); }
  void str(const std::string& s) {
    if (s.size() > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("string too long");
    u32(static_cast<std::uint32_t>(s.size()));
    raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
  }
};

// --- bounded reader ---------------------------------------------------------
class Reader {
 public:
  const std::uint8_t* p;
  const size_t sz;
  size_t pos = 0;
  bool ok = true;
  std::string err;
  Reader(const std::uint8_t* data, size_t size) : p(data), sz(size) {}
  bool range(size_t n) {
    if (n > sz - pos) { ok = false; err = "truncated/oversized length"; return false; }
    return true;
  }
  std::uint8_t u8() { if (!range(1)) return 0; return p[pos++]; }
  std::uint32_t u32() {
    if (!range(4)) return 0;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[pos++]) << (8 * i);
    return v;
  }
  std::uint64_t u64() {
    if (!range(8)) return 0;
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[pos++]) << (8 * i);
    return v;
  }
  std::string str() {
    std::uint32_t n = u32();
    if (!ok || !range(n)) return {};
    std::string s(reinterpret_cast<const char*>(p + pos), n);
    pos += n;
    return s;
  }
};

double check_finite(double v, const char* which, Reader& r) {
  if (!std::isfinite(v)) { r.ok = false; r.err = std::string(which) + " non-finite"; return 0.0; }
  return v;
}

}  // namespace

CodecResult encode_state(const PersistedState& state, std::vector<std::uint8_t>& out) {
  try {
    out.clear();
    Writer w(out);
    w.u32(persistence_magic());
    w.u32(persistence_version());

    // Validation of durable state integrity before writing.
    for (const auto& plan : state.plans) {
      if (plan.state == PlanState::COMPLETED && !plan.verification.has_value()) {
        return CodecResult::failure("persisting COMPLETED plan without verification");
      }
      if (!std::isfinite(plan.benefit) || !std::isfinite(plan.cost) ||
          !std::isfinite(plan.risk) || !std::isfinite(plan.disruption)) {
        return CodecResult::failure("plan has non-finite score");
      }
    }
    for (const auto& res : state.reservations) {
      if (res.end <= res.start) return CodecResult::failure("reservation has invalid interval");
    }

    w.u32(static_cast<std::uint32_t>(state.plans.size()));
    for (const auto& plan : state.plans) {
      w.u64(plan.id.value());
      w.u64(plan.generation.value());
      w.u64(plan.domain.value());
      w.u8(static_cast<std::uint8_t>(plan.state));
      w.u8(plan.verification.has_value() ? 1 : 0);
      if (plan.verification.has_value()) w.u8(static_cast<std::uint8_t>(*plan.verification));
      w.u64(plan.policy_generation.value());
      w.u64(plan.epoch.value());
      w.u64(plan.based_on_snapshot.value());
      w.u64(plan.expected_capacity_recovered.value());
      w.u64(0);  // packed double placeholder (see below)
      w.u64(0);
      w.u64(0);
      w.u64(0);
      // Replace packed doubles with the encoded ones written after.
      const std::uint64_t bit[4] = {
          static_cast<std::uint64_t>(std::bit_cast<std::uint64_t>(plan.benefit)),
          static_cast<std::uint64_t>(std::bit_cast<std::uint64_t>(plan.cost)),
          static_cast<std::uint64_t>(std::bit_cast<std::uint64_t>(plan.risk)),
          static_cast<std::uint64_t>(std::bit_cast<std::uint64_t>(plan.disruption))};
      // overwrite the four zero u64s at their recorded positions
      const size_t base = w.b.size() - 32;
      for (int i = 0; i < 4; ++i) {
        w.b[base + i * 8] = static_cast<std::uint8_t>(bit[i] & 0xFF);
        w.b[base + i * 8 + 1] = static_cast<std::uint8_t>((bit[i] >> 8) & 0xFF);
        w.b[base + i * 8 + 2] = static_cast<std::uint8_t>((bit[i] >> 16) & 0xFF);
        w.b[base + i * 8 + 3] = static_cast<std::uint8_t>((bit[i] >> 24) & 0xFF);
        w.b[base + i * 8 + 4] = static_cast<std::uint8_t>((bit[i] >> 32) & 0xFF);
        w.b[base + i * 8 + 5] = static_cast<std::uint8_t>((bit[i] >> 40) & 0xFF);
        w.b[base + i * 8 + 6] = static_cast<std::uint8_t>((bit[i] >> 48) & 0xFF);
        w.b[base + i * 8 + 7] = static_cast<std::uint8_t>((bit[i] >> 56) & 0xFF);
      }
      w.u32(static_cast<std::uint32_t>(plan.action_count.value()));
      w.u32(static_cast<std::uint32_t>(plan.action_kinds.size()));
      for (const auto& k : plan.action_kinds) w.u8(static_cast<std::uint8_t>(k));
    }

    w.u32(static_cast<std::uint32_t>(state.reservations.size()));
    for (const auto& res : state.reservations) {
      w.u64(res.id.value());
      w.u64(res.generation.value());
      w.u64(res.resource.value());
      w.u64(res.start.nanoseconds());
      w.u64(res.end.nanoseconds());
      w.u64(res.amount.value());
      w.u8(res.hard ? 1 : 0);
      w.u8(res.policy_protected ? 1 : 0);
    }

    w.u32(static_cast<std::uint32_t>(state.verification_history.size()));
    for (const auto& v : state.verification_history) {
      w.u64(v.plan.value());
      w.u8(static_cast<std::uint8_t>(v.outcome));
      w.u64(v.generation.value());
    }

    w.u64(state.resource_generation.value());
    w.u64(state.allocation_generation.value());
    w.u64(state.reservation_generation.value());
    w.u64(state.policy_generation.value());
    w.u64(state.epoch.value());
    w.u64(state.next_plan_id);
    w.u8(state.carried_live_dynamic_state ? 1 : 0);

    // Append CRC32 over everything (excluding the footer itself).
    std::uint32_t crc = crc32(w.b.data(), w.b.size());
    w.u32(crc);
    return CodecResult::success();
  } catch (const std::exception& e) {
    return CodecResult::failure(std::string("encode failed: ") + e.what());
  }
}

CodecResult decode_state(const std::vector<std::uint8_t>& in, PersistedState& out) {
  Reader r(in.data(), in.size());
  out = PersistedState{};
  const std::uint32_t magic = r.u32();
  const std::uint32_t version = r.u32();
  if (!r.ok) return CodecResult::failure("truncated header");
  if (magic != persistence_magic()) return CodecResult::failure("bad magic");
  if (version != persistence_version()) return CodecResult::failure("bad version");

  const std::uint32_t nplans = r.u32();
  if (!r.ok) return CodecResult::failure("truncated plan count");
  // Bounded count guard (avoid runaway allocations from corrupted input).
  if (nplans > 1'000'000u) return CodecResult::failure("plan count exceeds bound");

  for (std::uint32_t i = 0; i < nplans; ++i) {
    PersistedPlan p;
    p.id = FragmentationPlanId(r.u64());
    p.generation = FragmentationPlanGeneration(r.u64());
    p.domain = FragmentationDomainId(r.u64());
    const std::uint8_t state_u8 = r.u8();
    if (!valid_u8_enum(state_u8, V_PLAN_STATE_MIN, V_PLAN_STATE_MAX))
      return CodecResult::failure("invalid plan state enum");
    p.state = static_cast<PlanState>(state_u8);
    const std::uint8_t has_ver = r.u8();
    if (has_ver) {
      const std::uint8_t ver_u8 = r.u8();
      if (!valid_u8_enum(ver_u8, V_VERIFY_MIN, V_VERIFY_MAX))
        return CodecResult::failure("invalid verification outcome enum");
      p.verification = static_cast<VerificationOutcome>(ver_u8);
    }
    p.policy_generation = PolicyGeneration(r.u64());
    p.epoch = CoordinatorEpoch(r.u64());
    p.based_on_snapshot = FragmentationSnapshotGeneration(r.u64());
    p.expected_capacity_recovered = Bytes(r.u64());
    const std::uint64_t benefit = r.u64();
    const std::uint64_t cost = r.u64();
    const std::uint64_t risk = r.u64();
    const std::uint64_t disruption = r.u64();
    p.benefit = check_finite(std::bit_cast<double>(benefit), "benefit", r);
    p.cost = check_finite(std::bit_cast<double>(cost), "cost", r);
    p.risk = check_finite(std::bit_cast<double>(risk), "risk", r);
    p.disruption = check_finite(std::bit_cast<double>(disruption), "disruption", r);
    if (!r.ok) return CodecResult::failure(r.err);
    p.action_count = Count(r.u32());
    const std::uint32_t nkinds = r.u32();
    if (nkinds > 1'000'000u) return CodecResult::failure("action kind list exceeds bound");
    p.action_kinds.reserve(nkinds);
    for (std::uint32_t k = 0; k < nkinds; ++k) {
      const std::uint8_t a = r.u8();
      if (!valid_u8_enum(a, V_ACTION_MIN, V_ACTION_MAX))
        return CodecResult::failure("invalid action kind enum");
      p.action_kinds.push_back(static_cast<ActionKind>(a));
    }
    if (!r.ok) return CodecResult::failure(r.err);
    out.plans.push_back(p);
  }

  const std::uint32_t nres = r.u32();
  if (!r.ok) return CodecResult::failure("truncated reservation count");
  if (nres > 1'000'000u) return CodecResult::failure("reservation count exceeds bound");
  for (std::uint32_t i = 0; i < nres; ++i) {
    Reservation res;
    res.id = ReservationId(r.u64());
    res.generation = ReservationGeneration(r.u64());
    res.resource = ResourceId(r.u64());
    res.start = Duration(r.u64());
    res.end = Duration(r.u64());
    res.amount = Bytes(r.u64());
    res.hard = r.u8() != 0;
    res.policy_protected = r.u8() != 0;
    if (!r.ok) return CodecResult::failure(r.err);
    if (res.end <= res.start) return CodecResult::failure("reservation malformed interval");
    out.reservations.push_back(res);
  }

  const std::uint32_t nver = r.u32();
  if (!r.ok) return CodecResult::failure("truncated verification count");
  if (nver > 1'000'000u) return CodecResult::failure("verification count exceeds bound");
  for (std::uint32_t i = 0; i < nver; ++i) {
    VerificationResultStub v;
    v.plan = FragmentationPlanId(r.u64());
    const std::uint8_t o = r.u8();
    if (!valid_u8_enum(o, V_VERIFY_MIN, V_VERIFY_MAX))
      return CodecResult::failure("invalid verification outcome enum");
    v.outcome = static_cast<VerificationOutcome>(o);
    v.generation = VerificationGeneration(r.u64());
    out.verification_history.push_back(v);
  }

  out.resource_generation = ResourceGeneration(r.u64());
  out.allocation_generation = AllocationGeneration(r.u64());
  out.reservation_generation = ReservationGeneration(r.u64());
  out.policy_generation = PolicyGeneration(r.u64());
  out.epoch = CoordinatorEpoch(r.u64());
  out.next_plan_id = r.u64();
  out.carried_live_dynamic_state = r.u8() != 0;
  if (!r.ok) return CodecResult::failure("truncated generations trailer");

  // Duplicate plan id check.
  for (size_t i = 0; i < out.plans.size(); ++i)
    for (size_t j = i + 1; j < out.plans.size(); ++j)
      if (out.plans[i].id == out.plans[j].id) return CodecResult::failure("duplicate plan id");

  // Trailing garbage / checksum verification.
  if (!r.ok) return CodecResult::failure(r.err);
  if (r.pos > in.size()) return CodecResult::failure("oversized frame");
  const std::uint32_t stored_crc = r.u32();
  if (!r.ok) return CodecResult::failure("missing checksum");
  const size_t payload_len = r.pos - 4;
  const std::uint32_t computed = crc32(in.data(), payload_len);
  if (stored_crc != computed) return CodecResult::failure("checksum mismatch");
  if (r.pos != in.size()) return CodecResult::failure("trailing garbage");

  // Conservative recovery: degrade any plan whose state depended on live
  // dynamic evidence and force revalidation of current dynamic facts.
  out.needs_revalidation = true;
  for (auto& p : out.plans) {
    switch (p.state) {
      case PlanState::EXECUTING:
      case PlanState::QUIESCING:
      case PlanState::PRESERVING_STATE:
      case PlanState::MOVING:
      case PlanState::REBINDING:
      case PlanState::VERIFYING:
      case PlanState::APPROVED:
        p.state = PlanState::REVALIDATION_REQUIRED;
        break;
      default:
        break;
    }
  }
  return CodecResult::success();
}

CodecResult save_state(const PersistedState& state, const std::string& path) {
  std::vector<std::uint8_t> buf;
  CodecResult enc = encode_state(state, buf);
  if (!enc.ok) return enc;
  const std::string tmp = path + ".tmp";
  FILE* f = nullptr;
  if (fopen_s(&f, tmp.c_str(), "wb") != 0 || f == nullptr) return CodecResult::failure("cannot open temp file");
  const size_t n = std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (n != buf.size()) return CodecResult::failure("short write");
  if (std::rename(tmp.c_str(), path.c_str()) != 0) return CodecResult::failure("rename failed");
  return CodecResult::success();
}

CodecResult load_state(const std::string& path, PersistedState& out) {
  FILE* f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return CodecResult::failure("cannot open file");
  std::fseek(f, 0, SEEK_END);
  const long len = std::ftell(f);
  if (len < 0) { std::fclose(f); return CodecResult::failure("cannot determine size"); }
  std::fseek(f, 0, SEEK_SET);
  std::vector<std::uint8_t> buf(static_cast<size_t>(len));
  const size_t n = std::fread(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  if (n != buf.size()) return CodecResult::failure("short read");
  return decode_state(buf, out);
}

}  // namespace fragmentation_governor