#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

volatile std::uint64_t shared_global = 0x1122334455667788ULL;
std::mutex g_mutex;
std::condition_variable g_cv;
int g_ready = 0;

extern "C" __attribute__((noinline)) std::uintptr_t known_code() {
    return reinterpret_cast<std::uintptr_t>(&known_code);
}

extern "C" __attribute__((noinline)) void replay_point(int worker_id,
                                                          std::uintptr_t worker0_stack,
                                                          std::uintptr_t worker1_stack,
                                                          std::uintptr_t shared_addr) {
    asm volatile("" ::: "memory");
    if (worker_id < 0 || worker0_stack == 0 || worker1_stack == 0 || shared_addr == 0) {
        std::abort();
    }
}

static void worker(int worker_id, std::uintptr_t *published) {
    volatile std::uint64_t stack_value =
        0xA000000000000000ULL + static_cast<std::uint64_t>(worker_id);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        published[worker_id] = reinterpret_cast<std::uintptr_t>(&stack_value);
        ++g_ready;
    }
    g_cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cv.wait(lock, [] { return g_ready == 2; });
    }
    replay_point(worker_id, published[0], published[1],
                 reinterpret_cast<std::uintptr_t>(&shared_global));
    asm volatile("" :: "r"(stack_value) : "memory");
}

int main() {
    std::uintptr_t published[2] = {0, 0};
    std::thread t0(worker, 0, published);
    std::thread t1(worker, 1, published);
    t0.join();
    t1.join();
    std::cout << "known_code=0x" << std::hex << known_code()
              << " shared_global=0x" << reinterpret_cast<std::uintptr_t>(&shared_global)
              << " worker0_stack=0x" << published[0]
              << " worker1_stack=0x" << published[1] << std::dec << "\n";
    return 0;
}
