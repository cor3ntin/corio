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


int main() {
    std::cout << compute_pi() << "\n";
}