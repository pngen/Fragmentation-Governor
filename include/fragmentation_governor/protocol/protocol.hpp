#ifndef FRAGMENTATION_GOVERNOR_PROTOCOL_PROTOCOL_HPP
#define FRAGMENTATION_GOVERNOR_PROTOCOL_PROTOCOL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fragmentation_governor/core/enums.hpp"
#include "fragmentation_governor/core/types.hpp"
#include "fragmentation_governor/core/units.hpp"
#include "fragmentation_governor/fit/demand.hpp"
#include "fragmentation_governor/planner/plan.hpp"
#include "fragmentation_governor/resource/allocation.hpp"
#include "fragmentation_governor/resource/resource.hpp"
#include "fragmentation_governor/snapshot/snapshot.hpp"

namespace fragmentation_governor {

inline constexpr std::uint32_t protocol_magic() noexcept { return 0x464247u; }   // "FBG"
inline constexpr std::uint32_t protocol_version() noexcept { return 0x00010000u; }
inline constexpr std::uint32_t protocol_max_payload() noexcept { return 16u * 1024u * 1024u; }  // 16 MiB

enum class MsgType : std::uint8_t {
  HELLO,
  REGISTER,
  PUBLISH_RESOURCE,
  PUBLISH_LAYOUT,
  PUBLISH_ALLOCATION,
  INVALIDATE_ALLOCATION,
  QUERY_FRAGMENTATION,
  QUERY_FIT,
  CREATE_PLAN,
  PLAN_RESULT,
  APPROVE_PLAN,
  BEGIN_ACTION,
  ACTION_COMPLETE,
  VERIFY,
  CANCEL_PLAN,
  ADVANCE_GENERATION,
  REVALIDATE,
  SAVE,
  SHUTDOWN,
  ERROR,
};

inline constexpr std::string_view to_string(MsgType t) noexcept {
  switch (t) {
    case MsgType::HELLO: return "HELLO";
    case MsgType::REGISTER: return "REGISTER";
    case MsgType::PUBLISH_RESOURCE: return "PUBLISH_RESOURCE";
    case MsgType::PUBLISH_LAYOUT: return "PUBLISH_LAYOUT";
    case MsgType::PUBLISH_ALLOCATION: return "PUBLISH_ALLOCATION";
    case MsgType::INVALIDATE_ALLOCATION: return "INVALIDATE_ALLOCATION";
    case MsgType::QUERY_FRAGMENTATION: return "QUERY_FRAGMENTATION";
    case MsgType::QUERY_FIT: return "QUERY_FIT";
    case MsgType::CREATE_PLAN: return "CREATE_PLAN";
    case MsgType::PLAN_RESULT: return "PLAN_RESULT";
    case MsgType::APPROVE_PLAN: return "APPROVE_PLAN";
    case MsgType::BEGIN_ACTION: return "BEGIN_ACTION";
    case MsgType::ACTION_COMPLETE: return "ACTION_COMPLETE";
    case MsgType::VERIFY: return "VERIFY";
    case MsgType::CANCEL_PLAN: return "CANCEL_PLAN";
    case MsgType::ADVANCE_GENERATION: return "ADVANCE_GENERATION";
    case MsgType::REVALIDATE: return "REVALIDATE";
    case MsgType::SAVE: return "SAVE";
    case MsgType::SHUTDOWN: return "SHUTDOWN";
    case MsgType::ERROR: return "ERROR";
  }
  return "ERROR";
}

/// One protocol message.  Only the fields relevant to `type` are meaningful.
struct Message {
  MsgType type = MsgType::ERROR;
  // Common authority fields.
  std::uint64_t worker_id = 0;
  std::uint64_t worker_boot = 0;
  std::uint64_t epoch = 0;
  std::uint64_t policy_gen = 0;
  std::uint64_t resource_gen = 0;

  Resource resource;
  std::vector<Allocation> allocations;
  Allocation allocation;
  std::uint64_t allocation_id = 0;

  WorkloadDemand demand;
  RemediationPlan plan;

  std::uint64_t plan_id = 0;
  std::uint64_t plan_gen = 0;
  std::uint32_t action_index = 0;
  bool action_ok = false;
  std::uint64_t new_generation = 0;
  std::uint8_t generation_kind = 0;
  std::uint64_t gen_value = 0;

  // Structured result footer (uniform across all message types).
  bool ok = false;
  FitOutcome fit_outcome = FitOutcome::UNKNOWN;
  VerificationOutcome verification_outcome = VerificationOutcome::REVALIDATION_REQUIRED;
  std::string error;
  std::string detail;
};

// --- payload codec ----------------------------------------------------------
/// Encode a message to its payload bytes (no framing).  Deterministic.
std::vector<std::uint8_t> encode_payload(const Message& msg);
/// Decode a message payload.  Returns nullopt on any malformation.
std::optional<Message> decode_payload(const std::uint8_t* data, size_t len);

// --- framing ----------------------------------------------------------------
/// Produce a framed, checksummed frame: [magic][version][len][payload][crc32].
std::vector<std::uint8_t> frame_encode(const Message& msg);
/// Decode a single complete frame.  Rejects bad magic/version/length/checksum.
std::optional<Message> frame_decode(const std::uint8_t* data, size_t len);

/// Streaming frame decoder tolerant of arbitrary partial reads/writes.
class FrameDecoder {
 public:
  std::optional<Message> feed(const std::uint8_t* data, size_t len);
  void reset() { buf_.clear(); }
 private:
  std::vector<std::uint8_t> buf_;
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PROTOCOL_PROTOCOL_HPP