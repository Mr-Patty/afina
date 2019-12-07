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
                while (executor->number_of_threads == 0 && executor->state.load() == Executor::State::kRun) {

                    executor->free_threads++;
                    while (executor->empty_condition.wait_until(lock, time_until) == std::cv_status::timeout) {
                        if (executor->number_of_threads > executor->low_watermark) {
                            executor->number_of_threads--;
                            return;
                        } else {
                            continue;
                        }
                    }
                    executor->free_threads--;
                }

                task = executor->tasks.front();
                executor->tasks.pop_front();
            }
            try {
                task();
            } catch(...) {
                std::terminate();
            }
        }
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            executor->number_of_threads--;
            if (executor->number_of_threads == 0) {
                executor->stop_work.notify_all();
            }
        }
    }

    void Executor::Stop(bool await) {
        std::unique_lock<std::mutex> lock(mutex);
        if (state.load() == State::kRun) {
            state.store(State::kStopping);
            empty_condition.notify_all();

            while (number_of_threads != 0 && await) {
                stop_work.wait(lock);
            }
            state.store(State::kStopped);
        }

    }

    void Executor::Start() {
        state.store(State::kRun);
        std::unique_lock<std::mutex> lock(mutex);
        for (int i = 0; i < low_watermark; i++) {
            number_of_threads++;
            std::thread(&perform, this).detach();
        }
        free_threads = 0;
    }



} // namespace Concurrency
} // namespace Afina
