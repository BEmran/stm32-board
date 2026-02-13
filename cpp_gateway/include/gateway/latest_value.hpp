#pragma once
#include <atomic>
#include <cstdint>
#include <optional>

namespace gateway {

// SPSC writer / multi-reader "latest-wins" mailbox.
// Intended for trivially copyable-ish structs used in this project.
template <typename T>
class LatestValue {
public:
  void store(const T& v) noexcept {
    value_ = v;
    seq_.fetch_add(1, std::memory_order_release);
  }

  std::optional<T> load() const noexcept {
    const uint64_t s = seq_.load(std::memory_order_acquire);
    if (s == 0) return std::nullopt;
    return value_;
  }

  T load_or_default() const noexcept { return value_; }
  uint64_t seq() const noexcept { return seq_.load(std::memory_order_acquire); }

private:
  mutable T value_{};
  std::atomic<uint64_t> seq_{0};
};

} // namespace gateway
