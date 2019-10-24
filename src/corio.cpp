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

int main() {
    using namespace cor3ntin::corio::execution;

    std::mutex m;

    static_thread_pool p(20);
    for(auto i = 0; i < 1000000; i++) {
        auto sender = p.scheduler().schedule();
        std::move(sender).spawn(as_receiver{[&m] { printf("%ul\n", std::this_thread::get_id()); }});
    }

    wait(p.depleted());
}