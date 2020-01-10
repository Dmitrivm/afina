#include <afina/concurrency/Executor.h>

#include <cassert>

namespace Afina {
namespace Concurrency {

Executor::Executor(const std::size_t low_wm, const std::size_t high_wm, const std::size_t max_size,
                   const std::size_t idle_time) {

    low_watermark = low_wm;
    high_watermark = high_wm;
    max_queue_size = max_size;
    this->idle_time = idle_time;
    num_threads = num_idle = low_watermark;

    state = State::kRun;

    for (size_t i = 0; i < low_wm; i++) {
        std::thread t = std::thread(&perform, this);
        t.detach();
    }
}

void Executor::Stop(const bool await) {

    std::unique_lock<std::mutex> lk(mutex);
    if (state == State::kStopped) {
        return;
    }

    state = State::kStopping;
    if (!num_threads) {
        state = State::kStopped;
        return;
    }

    empty_condition.notify_all();
    if (await) {
        while (state != State::kStopped) {
            empty_threads.wait(lk);
        }
    }
}

void perform(Executor *executor) {

    while (true) {

        std::unique_lock<std::mutex> lk(executor->mutex);
        bool isTimeout = false;
        while (executor->tasks.empty() && executor->state == Executor::State::kRun) {
            if (executor->empty_condition.wait_for(lk, std::chrono::milliseconds(executor->idle_time)) == std::cv_status::timeout) {
                isTimeout = true;
                break;
            }
        }

        if ((executor->tasks.empty() && executor->state == Executor::State::kStopping)) {
            break;
        }
        if (isTimeout) {
            if (executor->num_threads > executor->low_watermark) {
                break;
            }
            continue;
        }
        
        auto func = executor->tasks.front();
        executor->tasks.pop_front();
        lk.unlock();

        executor->num_idle--;
        try {
            func();
        } catch (...) {
            std::terminate();
        }

        executor->num_idle++;
    }

    {
        std::lock_guard<std::mutex> lk(executor->mutex);
        executor->num_idle--;
        executor->num_threads--;
        if (!executor->num_threads && executor->state == Executor::State::kStopping) {
            executor->state = Executor::State::kStopped;
            executor->empty_threads.notify_all();
        }
        return;
    }
}

Executor::~Executor() { Stop(true); }

} // namespace Concurrency
} // namespace Afina
