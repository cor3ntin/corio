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
oneway_task ping(scheduler sch, auto r, auto w, int i) {
    try {
        co_await sch.schedule(std::chrono::milliseconds(1000));
        co_await w.write(i);
        std::cout << "Wrote Ping -->" << i << "\n";

    } catch(operation_cancelled c) {
        std::cout << "ping cancelled\n";
    } catch(channel_closed c) {
        std::cout << "channel closed !?\n";
    }
    // channel is closed (RAII)
}

template <execution::scheduler scheduler>
oneway_task pong(scheduler sch, auto r, auto w, stop_source& stop, int n) {
    try {
        int s = co_await r.read();
        std::cout << s << " ------ " << n << "\n";
        co_await sch.schedule(std::chrono::milliseconds(5));
    } catch(operation_cancelled c) {
        std::cout << "pong cancelled\n";
    } catch(channel_closed c) {
        std::cout << "channel closed - shutting down\n";
        stop.request_stop();  // ask the context to stop itself
    }
}

int main() {
    stop_source stop;
    io_uring_context ctx;
    std::thread t([&ctx, &stop] { ctx.run(stop.get_token()); });

    auto c1 = make_channel<int>(ctx.scheduler());
    auto c2 = make_channel<int>(ctx.scheduler(), 10);


    for(int i = 0; i <= 9; i++) {
        ping(ctx.scheduler(), c1.read(), c2.write(), i);
    }
    for(int i = 0; i <= 10; i++) {
        pong(ctx.scheduler(), c2.read(), c1.write(), stop, i);
    }
    t.join();
}