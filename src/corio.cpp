#include <corio/corio.hpp>
#include <iostream>

struct stupid_receiver {
public:
    void set_value() {
        puts("Hello World");
    }
    template <typename... T>
    void set_error(T&&...) noexcept {
        std::terminate();
    }
    void set_done() noexcept {}
};


/*
bool operator==(const stupid_executor&, const stupid_executor&) {
    return true;
};
bool operator!=(const stupid_executor&, const stupid_executor&) {
    return false;
};
**/


template <typename scheduler>
cppcoro::task<> run_in_pool(scheduler my_scheduler) {
    printf("Coro: %ul\n", std::this_thread::get_id());
    cor3ntin::corio::sender_awaiter a{my_scheduler.schedule()};
    auto x = co_await a;
    printf("Coro: %ul\n", std::this_thread::get_id());
}

int main() {
    printf("Main: %ul\n", std::this_thread::get_id());
    using namespace cor3ntin::corio::execution;

    // std::mutex m;

    static_thread_pool p(20);
    static_assert(cor3ntin::corio::execution::sender<decltype(p.scheduler().schedule())>);


    run_in_pool(p.scheduler());


    for(auto i = 0; i < 10; i++) {
        auto sender = p.scheduler().schedule();
        std::move(sender).spawn(
            as_receiver{[] { printf("Receiver: %ul\n", std::this_thread::get_id()); }});
    }

    wait(p.depleted());
}