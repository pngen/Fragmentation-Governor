#ifndef FRAGMENTATION_GOVERNOR_PERSISTENCE_CODEC_HPP
#define FRAGMENTATION_GOVERNOR_PERSISTENCE_CODEC_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "fragmentation_governor/persistence/state.hpp"

namespace fragmentation_governor {

/// Format magic and version.  Changing the encoding must bump the version, at
/// which point older files are rejected (bad version).
inline constexpr std::uint32_t persistence_magic() noexcept {
  return 0x464747u;  // "FGG"
}
inline constexpr std::uint32_t persistence_version() noexcept {
  return 0x00010000u;  // major=1 minor=0
}

/// Field-level validation/encode/decode result.
struct CodecResult {
  bool ok = false;
  std::string error;
  static CodecResult success() { return CodecResult{true, {}}; }
  static CodecResult failure(std::string msg) { return CodecResult{false, std::move(msg)}; }
};

/// Deterministically encode state to a byte buffer, with integrity checking
/// performed *and* the buffer guaranteed well-formed.
CodecResult encode_state(const PersistedState& state, std::vector<std::uint8_t>& out);

/// Decode state from a byte buffer.  Rejects bad magic/version, truncation,
/// corruption, malformed lengths, invalid enums, duplicate ids, generation
/// regression, invalid capacity, NaN/Inf, invalid action dependency, and
/// trailing garbage.  After a successful decode, dynamic evidence is flagged as
/// requiring revalidation.
CodecResult decode_state(const std::vector<std::uint8_t>& in, PersistedState& out);

/// Atomic save (temp file + rename) with byte-exact integrity.
CodecResult save_state(const PersistedState& state, const std::string& path);

/// Load and decode from disk.
CodecResult load_state(const std::string& path, PersistedState& out);

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_PERSISTENCE_CODEC_HPP
