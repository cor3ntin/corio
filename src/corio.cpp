#include <corio/corio.hpp>
#include <iostream>
#include <random>


template <typename scheduler>
cor3ntin::corio::oneway_task compute_pi(scheduler my_scheduler, std::vector<int>& v, int n,
                                        int slot) {

    co_await my_scheduler.schedule();

    static thread_local auto gen = [] {
        std::mt19937 e;
        std::random_device rdev;
        e.seed(rdev());
        return e;
    }();
    auto dist = std::uniform_real_distribution<>{0, 1};
    for(auto i = 0; i < n; i++) {
        if(std::sqrt(std::pow(dist(gen), 2) + std::pow(dist(gen), 2)) < 1)
            v[slot]++;
    }
}

double compute_pi() {
    static constexpr auto jobs = 100'000;
    static constexpr auto iters = 10'000;
    std::vector<int> v(jobs);
    static_thread_pool p(std::thread::hardware_concurrency());

    for(int i = 0; i < jobs; i++) {
        compute_pi(p.scheduler(), v, iters, i);
    }

    wait(p.depleted());
    return 4.0 * static_cast<double>(std::accumulate(v.begin(), v.end(), 0)) /
        static_cast<double>(jobs * iters);
}

using namespace cor3ntin::corio;
template <execution::scheduler scheduler>
oneway_task ping(scheduler sch, auto r, auto w) {
    try {
        for(int n = 10; n; n--) {
            co_await w.write("ping");
            co_await sch.schedule(std::chrono::milliseconds(100));
            std::string s = co_await r.read();
            std::cout << s << " " << n << "\n";
        }
    } catch(operation_cancelled c) {
        std::cout << "ping cancelled\n";
    }
    // channel is closed (RAII)
}

template <execution::scheduler scheduler>
oneway_task pong(scheduler sch, auto r, auto w, stop_source& stop) {
    try {
        for(int n = 0;; n++) {
            std::string s = co_await r.read();
            std::cout << s << " " << n << "\n";
            co_await sch.schedule(std::chrono::milliseconds(100));
            co_await w.write("pong");
        }
    } catch(operation_cancelled c) {
        std::cout << "pong cancelled\n";
    } catch(channel_closed c) {
        std::cout << "channel closed\n";
        stop.request_stop();  // ask the context to stop itself
    }
}

int main() {
    stop_source stop;
    io_uring_context ctx;
    std::thread t([&ctx, &stop] { ctx.run(stop.get_token()); });
    auto [r1, w1] = make_channel<std::string>(ctx);
    auto [r2, w2] = make_channel<std::string>(ctx);
    ping(ctx.scheduler(), std::move(r1), std::move(w2));
    pong(ctx.scheduler(), std::move(r2), std::move(w1), stop);
    t.join();
}