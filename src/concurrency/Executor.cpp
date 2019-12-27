#include <afina/concurrency/Executor.h>
#include <algorithm>

namespace Afina {
namespace Concurrency {

    void perform(Executor* executor) {
        std::function<void()> task;

        bool have_task = false;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                if (executor->state != Executor::State::kRun) {
                    break;
                }

                auto time_until = std::chrono::system_clock::now() + std::chrono::milliseconds(executor->idle_time);
                executor->free_threads++;
                if (executor->empty_condition.wait_until(lock, time, [&]() { return executor->tasks.empty(); })) {
                    if (executor->number_of_threads > executor->low_watermark) {
                        executor->free_threads--;
                        break;
                    } else {
                        have_task = false;
                    }
                } else {
                    task = executor->tasks.front();
                    executor->tasks.pop_front();
                    have_task = true;
                }
                executor->free_threads--;
            }
            if (have_task) {
                try {
                    task();
                } catch(...) {
                    std::terminate();
                }
                have_task = false;
            }
        }
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            executor->number_of_threads--;
            if (executor->state == Executor::State::kStopping && executor->tasks.empty()) {
                executor->state = Executor::State::kStopped;
                executor->stop_work.notify_all();
            }
        }
    }

    void Executor::Stop(bool await) {
        std::unique_lock<std::mutex> lock(mutex);
        if (state == State::kRun) {
            state = State::kStopping;
        }

        if (await && number_of_threads > 0) {
            stop_work.wait(lock, [&]() { return number_of_threads == 0; });
        } else if (number_of_threads == 0) {
            state = State::kStopped;
        }

    }

    void Executor::Start() {
        std::unique_lock<std::mutex> lock(mutex);
        state = State::kRun;
        for (int i = 0; i < low_watermark; i++) {
            number_of_threads++;
            std::thread(&perform, this).detach();
        }
    }



} // namespace Concurrency
} // namespace Afina
