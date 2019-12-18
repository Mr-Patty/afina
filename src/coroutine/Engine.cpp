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
    if (ctx.Hight < ctx.Low) {
        std::swap(ctx.Hight, ctx.Low);
    }
    auto &buf = std::get<0>(ctx.Stack);
    auto &size = std::get<1>(ctx.Stack);
    auto need_size = ctx.Low - ctx.Hight;

    if (size < need_size) {
        delete[] buf;
        buf = new char[need_size];
        size = need_size;
    }
    memcpy(buf, ctx.Low, need_size);
    ctx.Stack = std::make_tuple(buf, size);
}

void Engine::Restore(context &ctx) {
    char current_address;
    if ((&current_address >= ctx.Low) && (ctx.Hight > &current_address)) {
        Restore(ctx);
    }

    auto &buf = std::get<0>(ctx.Stack);
    auto size = std::get<1>(ctx.Stack);
    memcpy(ctx.Low, buf, size);
    cur_routine = &ctx;
    longjmp(ctx.Environment, 1);
}

void Engine::yield() {
    if (alive == nullptr) {
        return;
    }

    Store(*cur_routine);
    auto routine_todo = alive;
    if (routine_todo == cur_routine) {
        if (alive->next != nullptr) {
            routine_todo = alive->next;
            sched(static_cast<void *>(routine_todo));
        }
    }

}

void Engine::sched(void *routine) {
    if (routine == cur_routine) {
        return;
    } else if (routine == nullptr) {
        yield();
    }
    if (setjmp(cur_routine->Environment) == 0) {
        Store(*cur_routine);
        context *ctx = static_cast<context *>(routine);
        Restore(*ctx);
    }
}

} // namespace Coroutine
} // namespace Afina
