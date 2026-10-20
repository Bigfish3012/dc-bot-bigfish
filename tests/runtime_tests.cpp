#include "../include/keyed_executor.h"
#include "../include/bounded_buffer.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace std::chrono_literals;
using Admission = KeyedExecutor::Admission;

void test_buffer() {
    BoundedBuffer buffer{{}, 4};
    char bytes[] = "abcd";
    assert(bounded_write(bytes, 1, 4, &buffer) == 4);
    assert(buffer.data == "abcd");
    assert(bounded_write(bytes, 1, 1, &buffer) == 0);
    assert(buffer.exceeded && buffer.data == "abcd");
    BoundedBuffer overflow{{}, 4};
    assert(bounded_write(bytes, 2, std::numeric_limits<std::size_t>::max(), &overflow) == 0);
    assert(overflow.exceeded && overflow.data.empty());
    BoundedBuffer empty{{}, 0};
    assert(bounded_write(nullptr, 0, 100, &empty) == 0);
    assert(!empty.exceeded);
}

void test_order_and_parallelism() {
    KeyedExecutor jobs(2, 8, 4);
    std::promise<void> release, first_started, other_finished, all_done;
    auto released = release.get_future().share();
    auto started = first_started.get_future();
    auto other = other_finished.get_future();
    auto finished = all_done.get_future();
    std::atomic<int> sequence{0};
    assert(jobs.submit("a", [&] {
        first_started.set_value();
        released.wait();
        assert(sequence.fetch_add(1) == 0);
    }) == Admission::accepted);
    assert(started.wait_for(2s) == std::future_status::ready);
    assert(jobs.submit("a", [&] { assert(sequence.fetch_add(1) == 1); }) == Admission::accepted);
    assert(jobs.submit("a", [&] {
        assert(sequence.fetch_add(1) == 2);
        all_done.set_value();
    }) == Admission::accepted);
    assert(jobs.submit("b", [&] { other_finished.set_value(); }) == Admission::accepted);
    // A queued session must not occupy the second worker and block session b.
    const auto other_status = other.wait_for(2s);
    release.set_value();
    assert(other_status == std::future_status::ready);
    assert(finished.wait_for(2s) == std::future_status::ready);
    jobs.stop();
    assert(sequence == 3);
}

void test_admission_and_shutdown() {
    KeyedExecutor jobs(1, 3, 2);
    std::promise<void> started, release;
    auto entered = started.get_future();
    auto gate = release.get_future().share();
    std::atomic<int> queued_runs{0};
    assert(jobs.submit("a", [&] { started.set_value(); gate.wait(); }) == Admission::accepted);
    assert(entered.wait_for(2s) == std::future_status::ready);
    assert(jobs.submit("a", [&] { ++queued_runs; }) == Admission::accepted);
    assert(jobs.submit("a", [] {}) == Admission::session_full);
    assert(jobs.submit("b", [&] { ++queued_runs; }) == Admission::accepted);
    assert(jobs.submit("c", [] {}) == Admission::full);
    auto stopped = std::async(std::launch::async, [&] { jobs.stop(); });
    // Wait for stop to close admission, without depending on scheduler timing.
    Admission result;
    do { result = jobs.submit("c", [] {}); std::this_thread::yield(); }
    while (result != Admission::stopped);
    release.set_value();
    assert(stopped.wait_for(2s) == std::future_status::ready);
    stopped.get();
    assert(queued_runs == 0);
}

void test_exception_releases_session() {
    KeyedExecutor jobs(2, 8, 4);
    std::promise<void> done;
    auto completed = done.get_future();
    assert(jobs.submit("a", [] { throw std::runtime_error("test"); }) == Admission::accepted);
    assert(jobs.submit("a", [&] { done.set_value(); }) == Admission::accepted);
    assert(completed.wait_for(2s) == std::future_status::ready);
    jobs.stop();
}

void test_stress() {
    KeyedExecutor jobs(2, 1024, 128);
    std::atomic<int> active[8]{};
    std::atomic<int> finished{0};
    std::mutex completed_mutex;
    std::condition_variable completed;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer] {
            for (int i = 0; i < 200; ++i) {
                const int session = (producer * 200 + i) % 8;
                assert(jobs.submit(std::to_string(session), [&, session] {
                    assert(active[session].fetch_add(1) == 0);
                    std::this_thread::yield();
                    assert(active[session].fetch_sub(1) == 1);
                    std::lock_guard<std::mutex> lock(completed_mutex);
                    ++finished;
                    completed.notify_one();
                }) == Admission::accepted);
            }
        });
    }
    for (auto& producer : producers) producer.join();
    std::unique_lock<std::mutex> lock(completed_mutex);
    assert(completed.wait_for(lock, 5s, [&] { return finished == 800; }));
    lock.unlock();
    jobs.stop();
}

int main() {
    test_buffer();
    test_order_and_parallelism();
    test_admission_and_shutdown();
    test_exception_releases_session();
    test_stress();
    std::cout << "PASS: buffer limits/overflow, session FIFO, parallelism, admission, shutdown, exceptions, 800-job concurrent stress\n";
}
