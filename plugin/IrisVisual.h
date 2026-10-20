#pragma once

#include "src/IrisDSP.h"
#include <array>
#include <atomic>

namespace iris::vst3 {

inline constexpr const char* kVisualRequest = "Iris.Visual.Request.v1";
inline constexpr const char* kVisualMessage = "Iris.Visual.Frame.v1";

// Three bounded ownership slots. The audio writer never waits for the GUI and
// never writes a slot while a reader copies it. This deliberately drops old
// visual frames: telemetry cannot delay audio. Unlike a seqlock over plain
// floats, no reader/writer ever accesses the same payload concurrently.
class VisualMailbox {
    enum : unsigned { free = 0, writing = 1, ready = 2, reading = 3 };
    mutable std::array<std::atomic<unsigned>, 3> states_{};
    mutable std::array<VisualFrame, 3> frames_{};
    std::atomic<unsigned> newest_{0};
public:
    static_assert(std::atomic<unsigned>::is_always_lock_free, "Visual ownership must be lock-free");
    VisualMailbox() noexcept { for (auto& state : states_) state.store(free, std::memory_order_relaxed); }

    bool publish(const VisualFrame& frame) noexcept {
        for (unsigned i = 0; i < states_.size(); ++i) {
            unsigned expected = free;
            if (!states_[i].compare_exchange_strong(expected, writing,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            frames_[i] = frame;
            // Retire older unread data before making this frame readable.
            // A reader must not take the new frame and then fall back to an
            // older ready slot on its next timer tick.
            for (unsigned j = 0; j < states_.size(); ++j) if (j != i) {
                expected = ready;
                states_[j].compare_exchange_strong(expected, free,
                    std::memory_order_acq_rel, std::memory_order_relaxed);
            }
            states_[i].store(ready, std::memory_order_release);
            newest_.store(i, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool take(VisualFrame& frame) const noexcept {
        const unsigned latest = newest_.load(std::memory_order_acquire);
        for (unsigned offset = 0; offset < states_.size(); ++offset) {
            const unsigned index = (latest + offset) % states_.size();
            auto& state = states_[index];
            unsigned expected = ready;
            if (!state.compare_exchange_strong(expected, reading,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            frame = frames_[index];
            state.store(free, std::memory_order_release);
            return true;
        }
        return false;
    }

    void clear() noexcept {
        for (auto& state : states_) {
            unsigned expected = ready;
            state.compare_exchange_strong(expected, free,
                std::memory_order_acq_rel, std::memory_order_relaxed);
        }
    }
};
} // namespace iris::vst3
