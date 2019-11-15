#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx) {
    char current_address;
    ctx.Low = StackBottom;
    ctx.Hight = &current_address;

    auto &buf = std::get<0>(ctx.Stack);
    auto &size = std::get<1>(ctx.Stack);
    auto need_size = ctx.Low - ctx.Hight;

    if (size < need_size) {
        delete[] buf;
        buf = new char[need_size];
        size = need_size;
    }
    memcpy(buf, ctx.Low, need_size);
}

void Engine::Restore(context &ctx) {
    char current_address;
    if (&current_address >= ctx.Low) {
        Restore(ctx);
    }

    auto &buf = std::get<0>(ctx.Stack);
    auto size = ctx.Low - ctx.Hight;
    memcpy(ctx.Low, buf, size);
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (alive == nullptr || alive->next == nullptr) {
        return;
    }

    if (alive == cur_routine) {
        Enter(*(alive->next));
    } else {
        Enter(*alive);
    }
}

void Engine::sched(void *routine_) {
    if (routine_ == cur_routine) {
        return;
    } else if (routine_ == nullptr) {
        yield();
    } else {
        Enter(*(static_cast<context *>(routine_)));
    }
}

void Engine::Enter(context& ctx) {
    if (cur_routine != nullptr && cur_routine != idle_ctx) {
        if (setjmp(cur_routine->Environment) > 0) {
            return;
        }
        Store(*cur_routine);
    }
    cur_routine = &ctx;
    Restore(ctx);
}

} // namespace Coroutine
} // namespace Afina
