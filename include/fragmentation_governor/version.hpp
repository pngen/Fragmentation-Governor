// Fragmentation Governor - open-source, vendor-neutral C++20 runtime for
// detecting, quantifying, explaining, and governing stranded capacity.
//
// Copyright 2026 Summon Software Labs.  Apache License 2.0.
// No telemetry transmission.

#ifndef FRAGMENTATION_GOVERNOR_VERSION_HPP
#define FRAGMENTATION_GOVERNOR_VERSION_HPP

#define FRAGMENTATION_GOVERNOR_VERSION_MAJOR 1
#define FRAGMENTATION_GOVERNOR_VERSION_MINOR 0
#define FRAGMENTATION_GOVERNOR_VERSION_PATCH 0

#define FRAGMENTATION_GOVERNOR_VERSION_STRING "1.0.0"

namespace fragmentation_governor {

/// The released runtime version, in "major.minor.patch" form.
inline constexpr const char* version_string() noexcept { return FRAGMENTATION_GOVERNOR_VERSION_STRING; }

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_VERSION_HPP
