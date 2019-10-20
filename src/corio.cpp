#include <corio/corio.hpp>


struct stupid_sender {
    template <typename R>
    requires cor3ntin::corio::execution::receiver_of<R, int>
    void submit(R&& r) const noexcept {
        cor3ntin::corio::execution::set_value(r, 42);
    }
};

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

struct stupid_executor {
    template <typename Fn>
    requires cor3ntin::corio::concepts::invocable<Fn>
    auto execute(Fn&& f) const {
        return f();
    }
};


bool operator==(const stupid_executor&, const stupid_executor&) {
        return true;
};
bool operator!=(const stupid_executor&, const stupid_executor&) {
        return false;
};


namespace {

using namespace cor3ntin::corio;
static_assert(execution::sender<stupid_sender>);
static_assert(execution::__submit_ns::submittable_receiver<stupid_sender, execution::sink_receiver>);
static_assert(execution::__execute_ns::executable_function<stupid_executor, execution::details::as_invocable<execution::sink_receiver>>);
static_assert(execution::sender<stupid_sender>);
static_assert(execution::receiver<stupid_receiver>);
static_assert(execution::executor<stupid_executor>);
}

int main() {
    using namespace cor3ntin::corio::execution;
    stupid_executor ex;
    execute(ex, [] {
        puts("Hello World\n");
        return;
    });
}