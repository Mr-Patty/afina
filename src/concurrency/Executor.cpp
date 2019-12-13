#include <afina/concurrency/Executor.h>
#include <algorithm>

namespace Afina {
namespace Concurrency {

    void perform(Executor* executor) {

        while (executor->state.load() == Executor::State::kRun) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                auto time_until = std::chrono::system_clock::now() + std::chrono::milliseconds(executor->idle_time);

                while (executor->tasks.empty() == 0 && executor->state.load() == Executor::State::kRun) {
                    if (executor->empty_condition.wait_until(lock, time_until) == std::cv_status::timeout &&
                        executor->active_threads + executor->free_threads > executor->low_watermark) {
                        break;
                    } else {
                        executor->empty_condition.wait(lock);
                    }
                }
                if (executor->tasks.empty()) {
                    continue;
                }
                executor->free_threads--;
                executor->active_threads++;
                task = executor->tasks.front();
                executor->tasks.pop_front();
            }
            try {
                task();
            } catch(...) {
                std::terminate();
            }
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                executor->free_threads++;
                executor->active_threads--;
            }
        }
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            executor->free_threads--;
            if (executor->state == Executor::State::kStopping && executor->tasks.empty()) {
                executor->state = Executor::State::kStopped;
                executor->stop_work.notify_all();
            }
        }
    }

    void Executor::Stopping() {

        std::unique_lock<std::mutex> lock(mutex);
        while (active_threads != 0) {
            stop_work.wait(lock);
        }
        state.store(State::kStopped);
    }

    void Executor::Stop(bool await) {
        if (state.load() == State::kRun) {
            state.store(State::kStopping);
            std::thread t(&Executor::Stopping, this);
            if (!await) {
                t.detach();
            } else {
                t.join();
            }
        }
    }

    void Executor::Start() {
        state.store(State::kRun);
        std::unique_lock<std::mutex> lock(mutex);
        for (int i = 0; i < low_watermark; i++) {
            free_threads++;
            std::thread(&perform, this).detach();
        }
    }



} // namespace Concurrency
} // namespace Afina
