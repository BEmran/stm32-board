#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gateway {

// Single-producer/single-consumer ring with overwrite-on-full (drop oldest).
template <typename T, size_t Capacity>
class SpscOverwriteRing {
  static_assert(Capacity >= 2, "Capacity must be >= 2");

public:
  bool push_overwrite(const T& item) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t next = inc(head);

    if (next == tail_.load(std::memory_order_acquire)) {
      drops_.fetch_add(1, std::memory_order_relaxed);
      tail_.store(inc(tail_.load(std::memory_order_relaxed)), std::memory_order_release);
    }

    buf_[head] = item;
    head_.store(next, std::memory_order_release);
    return true;
  }

  std::optional<T> pop() noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;

    T out = buf_[tail];
    tail_.store(inc(tail), std::memory_order_release);
    return out;
  }

  template <typename Fn>
  size_t drain(size_t max_n, Fn&& fn) noexcept {
    size_t n = 0;
    while (n < max_n) {
      auto v = pop();
      if (!v) break;
      fn(*v);
      ++n;
    }
    return n;
  }

  uint64_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

private:
  static constexpr size_t inc(size_t i) noexcept { return (i + 1) % Capacity; }

  std::array<T, Capacity> buf_{};
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
  std::atomic<uint64_t> drops_{0};
};

} // namespace gateway
