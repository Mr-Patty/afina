#include <afina/concurrency/Executor.h>
#include <algorithm>

namespace Afina {
namespace Concurrency {

    void perform(Executor* executor) {

        while (executor->state == Executor::State::kRun) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(executor->mutex);
                auto time_until = std::chrono::system_clock::now() + std::chrono::milliseconds(executor->idle_time);
                while (executor->tasks.empty() && executor->state == Executor::State::kRun) {

                    executor->free_threads++;
                    if (executor->empty_condition.wait_until(lock, time_until) == std::cv_status::timeout) {
                        if (executor->threads.size() > executor->low_watermark) {
                            executor->eraseThread();
                            return;
                        } else {
                            executor->empty_condition.wait(lock);
                        }
                    }
                    executor->free_threads--;
                }

                task = executor->tasks.front();
                executor->tasks.pop_front();
            }
            task();
        }
        {
            std::unique_lock<std::mutex> lock(executor->mutex);
            executor->eraseThread();
            if (executor->threads.empty()) {
                executor->stop_work.notify_all();
            }
        }
    }

    void Executor::Stop(bool await) {
        state = State::kStopping;
        empty_condition.notify_all();
        if (await) {
            std::unique_lock<std::mutex> lock(mutex);
            while (!threads.empty()) {
                stop_work.wait(lock);
            }
        }
        state = State::kStopped;
    }

    void Executor::Start() {
        state = State::kRun;
        std::unique_lock<std::mutex> lock(mutex);
        for (int i = 0; i < low_watermark; i++) {
            threads.push_back(std::thread(&perform, this));
        }
        free_threads = 0;
    }

    void Executor::eraseThread() {
        std::thread::id thread_id = std::this_thread::get_id();
        auto iter = std::find_if(threads.begin(), threads.end(), [=](std::thread &t) { return (t.get_id() == thread_id); });
        if (iter != threads.end()) {
            free_threads--;
            iter->detach();
            threads.erase(iter);
            return;
        }
    }


} // namespace Concurrency
} // namespace Afina
