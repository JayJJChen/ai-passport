#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include "voice_turn_gate.h"

int main() {
    std::atomic<uint32_t> active{1};
    assert(voice_turn_current(active, 1));
    assert(!voice_turn_current(active, 0));
    assert(!voice_turn_current(active, 2));

    /* A click and a timeout can race to stop one recording. Only one owns it. */
    std::atomic<bool> go{false};
    std::atomic<int> stopped{0};
    auto stop = [&]() {
        while (!go.load()) std::this_thread::yield();
        if (voice_turn_release(active, 1)) stopped.fetch_add(1);
    };
    std::thread click(stop), timeout(stop);
    go.store(true);
    click.join(); timeout.join();
    assert(stopped.load() == 1);
    assert(active.load() == 0);

    /* A delayed failure/timeout from the old turn cannot stop its successor. */
    active.store(2);
    assert(!voice_turn_release(active, 1));
    assert(voice_turn_current(active, 2));
    assert(!voice_turn_packet_current(1, 2));
    assert(voice_turn_packet_current(2, 2));
    assert(!voice_turn_packet_current(0, 2));

    assert(!voice_turn_expired(100, 20099, 20000));
    assert(voice_turn_expired(100, 20100, 20000));
    assert(!voice_turn_expired(UINT32_MAX - 5, 3, 20));
    assert(voice_turn_expired(UINT32_MAX - 5, 14, 20));
    assert(!voice_reply_timed_out(100, 4, 4, 0, true, 60099, 60000));
    assert(voice_reply_timed_out(100, 4, 4, 0, true, 60100, 60000));
    assert(!voice_reply_timed_out(100, 4, 5, 0, true, 60100, 60000));
    assert(!voice_reply_timed_out(100, 4, 4, 9, true, 60100, 60000));
    assert(!voice_reply_timed_out(100, 4, 4, 0, false, 60100, 60000));
    std::atomic<size_t> bytes{0};
    std::atomic<int> admitted{0};
    go.store(false);
    auto reserve = [&]() {
        while (!go.load()) std::this_thread::yield();
        if (voice_packet_budget_reserve(bytes, 1024, 4096)) admitted.fetch_add(1);
    };
    std::thread contenders[8];
    for (auto &contender : contenders) contender = std::thread(reserve);
    go.store(true);
    for (auto &contender : contenders) contender.join();
    assert(admitted.load() == 4 && bytes.load() == 4096);
    assert(!voice_packet_budget_reserve(bytes, 1, 4096));
    bytes.fetch_sub(1024);
    assert(voice_packet_budget_reserve(bytes, 1024, 4096));
    std::puts("Voice turn identity, cancellation race and timeout: PASS");
}
