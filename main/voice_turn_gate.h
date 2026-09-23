#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

/* Pure turn-ID rules shared by the control, capture and sender tasks. The
 * timestamp subtraction deliberately works across the 32-bit ms wrap. */
inline bool voice_turn_current(const std::atomic<uint32_t> &active, uint32_t turn) {
    return turn && active.load() == turn;
}

inline bool voice_turn_release(std::atomic<uint32_t> &active, uint32_t turn) {
    if (!turn) return false;
    return active.compare_exchange_strong(turn, 0);
}

inline bool voice_turn_expired(uint32_t started_ms, uint32_t now_ms, uint32_t limit_ms) {
    return (uint32_t)(now_ms - started_ms) >= limit_ms;
}

inline bool voice_reply_timed_out(uint32_t started_ms, uint32_t reply_generation,
                                  uint32_t chat_generation, uint32_t active_turn,
                                  bool waiting_for_reply, uint32_t now_ms, uint32_t limit_ms) {
    return started_ms && reply_generation == chat_generation && !active_turn &&
           waiting_for_reply && voice_turn_expired(started_ms, now_ms, limit_ms);
}

inline bool voice_turn_packet_current(uint32_t packet_turn, uint32_t capture_turn) {
    return packet_turn && packet_turn == capture_turn;
}

inline bool voice_packet_budget_reserve(std::atomic<size_t> &used, size_t size, size_t limit) {
    size_t current = used.load();
    while (size && size <= limit && current <= limit - size) {
        if (used.compare_exchange_weak(current, current + size)) return true;
    }
    return false;
}
