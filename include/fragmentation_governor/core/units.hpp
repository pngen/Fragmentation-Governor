#ifndef FRAGMENTATION_GOVERNOR_CORE_UNITS_HPP
#define FRAGMENTATION_GOVERNOR_CORE_UNITS_HPP

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <stdexcept>

namespace fragmentation_governor {

/// Thrown for impossible values (negative capacity, overflow, NaN/Inf, ...).
class ValueError : public std::runtime_error {
 public:
  explicit ValueError(const char* m) : std::runtime_error(m) {}
};

namespace detail {
[[noreturn]] inline void overflow(const char* m) { throw ValueError(m); }
}  // namespace detail

// ---------------------------------------------------------------------------
// Bytes - a checked non-negative quantity of memory.  Arithmetic overflow and
// underflow are rejected rather than silently wrapping.
// ---------------------------------------------------------------------------
class Bytes {
 public:
  using value_type = std::uint64_t;

  constexpr Bytes() noexcept = default;
  constexpr Bytes(std::uint64_t v) noexcept : v_(v) {}

  constexpr value_type value() const noexcept { return v_; }
  constexpr bool is_zero() const noexcept { return v_ == 0; }

  constexpr bool operator==(const Bytes&) const noexcept = default;
  constexpr auto operator<=>(const Bytes&) const noexcept = default;

  Bytes operator+(const Bytes& o) const {
    if (o.v_ > std::numeric_limits<value_type>::max() - v_) detail::overflow("Bytes add overflow");
    return Bytes(v_ + o.v_);
  }
  Bytes operator-(const Bytes& o) const {
    if (o.v_ > v_) detail::overflow("Bytes subtract underflow");
    return Bytes(v_ - o.v_);
  }
  bool operator<=(const Bytes& o) const noexcept { return v_ <= o.v_; }
  bool operator>=(const Bytes& o) const noexcept { return v_ >= o.v_; }
  bool operator<(const Bytes& o) const noexcept { return v_ < o.v_; }
  bool operator>(const Bytes& o) const noexcept { return v_ > o.v_; }

  friend std::ostream& operator<<(std::ostream& os, const Bytes& b) { os << b.v_; return os; }

 private:
  value_type v_ = 0;
};

/// Large power-of-ten multipler helpers for building literal sizes.
inline constexpr Bytes kibibytes(std::uint64_t n) { return Bytes(n * 1024ULL); }
inline constexpr Bytes mebibytes(std::uint64_t n) { return Bytes(n * 1024ULL * 1024ULL); }
inline constexpr Bytes gibibytes(std::uint64_t n) {
  if (n > (std::numeric_limits<std::uint64_t>::max() / (1024ULL * 1024ULL * 1024ULL)))
    detail::overflow("gibibytes overflow");
  return Bytes(n * 1024ULL * 1024ULL * 1024ULL);
}

// ---------------------------------------------------------------------------
// Count - a checked non-negative count (allocations, blocks, devices, ...).
// ---------------------------------------------------------------------------
class Count {
 public:
  using value_type = std::uint64_t;
  constexpr Count() noexcept = default;
  constexpr Count(std::uint64_t v) noexcept : v_(v) {}
  constexpr value_type value() const noexcept { return v_; }
  constexpr bool is_zero() const noexcept { return v_ == 0; }
  constexpr bool operator==(const Count&) const noexcept = default;
  constexpr auto operator<=>(const Count&) const noexcept = default;
  Count operator+(const Count& o) const {
    if (o.v_ > std::numeric_limits<value_type>::max() - v_) detail::overflow("Count add overflow");
    return Count(v_ + o.v_);
  }
  Count operator-(const Count& o) const {
    if (o.v_ > v_) detail::overflow("Count subtract underflow");
    return Count(v_ - o.v_);
  }
  friend std::ostream& operator<<(std::ostream& os, const Count& c) { os << c.v_; return os; }
 private:
  value_type v_ = 0;
};

// ---------------------------------------------------------------------------
// Duration - a checked monotonic nanosecond duration (the unit of temporal
// reservation analysis).  Negative durations are impossible and rejected.
// ---------------------------------------------------------------------------
class Duration {
 public:
  using value_type = std::uint64_t;  // nanoseconds
  constexpr Duration() noexcept = default;
  constexpr explicit Duration(std::uint64_t nanoseconds) noexcept : ns_(nanoseconds) {}
  constexpr value_type nanoseconds() const noexcept { return ns_; }

  constexpr bool operator==(const Duration&) const noexcept = default;
  constexpr auto operator<=>(const Duration&) const noexcept = default;

  Duration operator+(const Duration& o) const {
    if (o.ns_ > std::numeric_limits<value_type>::max() - ns_) detail::overflow("Duration add overflow");
    return Duration(ns_ + o.ns_);
  }
  Duration operator-(const Duration& o) const {
    if (o.ns_ > ns_) detail::overflow("Duration subtract underflow");
    return Duration(ns_ - o.ns_);
  }
  friend std::ostream& operator<<(std::ostream& os, const Duration& d) { os << d.ns_ << "ns"; return os; }
 private:
  value_type ns_ = 0;
};

inline constexpr Duration nanoseconds(std::uint64_t n) { return Duration(n); }
inline constexpr Duration milliseconds(std::uint64_t n) {
  if (n > std::numeric_limits<std::uint64_t>::max() / 1'000'000ULL) detail::overflow("milliseconds overflow");
  return Duration(n * 1'000'000ULL);
}
inline constexpr Duration seconds(std::uint64_t n) { return milliseconds(n * 1'000ULL); }

// ---------------------------------------------------------------------------
// Fraction - a normalized fraction normalized to [0, 1].  NaN/Inf, negatives,
// and values above 1 are rejected at construction.
// ---------------------------------------------------------------------------
class Fraction {
 public:
  constexpr Fraction() noexcept = default;
  Fraction(double v) {
    if (!std::isfinite(v)) throw ValueError("Fraction must be finite");
    if (v < 0.0 || v > 1.0) throw ValueError("Fraction out of [0,1]");
    v_ = v;
  }
  double value() const noexcept { return v_; }
  bool operator==(const Fraction&) const noexcept = default;
  friend std::ostream& operator<<(std::ostream& os, const Fraction& f) { os << f.v_; return os; }
 private:
  double v_ = 0.0;
};

// ---------------------------------------------------------------------------
// Score - a bounded dimensionless score (e.g. fragmentation score) in [-1, 1]
// or a cost/benefit weight in [0, 1].  Non-finite and out-of-range values are
// rejected so that no "invented precision" and no NaN can propagate.
// ---------------------------------------------------------------------------
class Score {
 public:
  constexpr Score() noexcept = default;
  Score(double v, double lo, double hi) : lo_(lo), hi_(hi) {
    if (!std::isfinite(v)) throw ValueError("Score must be finite");
    if (v < lo_ || v > hi_) throw ValueError("Score out of [lo,hi]");
    v_ = v;
  }
  double value() const noexcept { return v_; }
 private:
  double v_ = 0.0;
  double lo_ = 0.0;
  double hi_ = 1.0;
};

/// A score guaranteed within [0,1].
class HealthyScore {
 public:
  constexpr HealthyScore() noexcept = default;
  HealthyScore(double v) {
    if (!std::isfinite(v)) throw ValueError("HealthyScore must be finite");
    if (v < 0.0 || v > 1.0) throw ValueError("HealthyScore out of [0,1]");
    v_ = v;
  }
  double value() const noexcept { return v_; }
  bool operator==(const HealthyScore&) const noexcept = default;
 private:
  double v_ = 0.0;
};

}  // namespace fragmentation_governor

#endif  // FRAGMENTATION_GOVERNOR_CORE_UNITS_HPP