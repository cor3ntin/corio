#pragma once
#include <corio/concepts.hpp>
#include <vector>
#include <vector>
#include <thread>
#include <queue>
#include <exception>

namespace cor3ntin::corio {

bool wait(execution::sender auto&& send) {

    struct state {
        std::mutex m;
        std::condition_variable c;
        bool cancelled = false;
        std::exception_ptr err;
    } s;

    struct r {
        void set_value() {
            s.c.notify_one();
        }

        void set_done() {
            s.cancelled = true;
            s.c.notify_one();
        }

        void set_error(std::exception_ptr ptr) {
            s.err = ptr;
        }
        state& s;
    };
    struct r r {
        s
    };

    auto op = std::move(send).connect(std::move(r));
    op.start();

    std::unique_lock<std::mutex> lock(s.m);
    s.c.wait(lock);

    if(s.err) {
        std::rethrow_exception(s.err);
    }
    return !s.cancelled;
}

}