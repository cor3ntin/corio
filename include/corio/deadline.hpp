#pragma once
#include <chrono>

struct __kernel_timespec;

namespace cor3ntin::corio {

struct deadline {
    constexpr deadline() noexcept = default;
    constexpr explicit operator bool() const noexcept {
        return d.count() != 0;
    }
    template <class Clock, class Duration>
    constexpr deadline(std::chrono::time_point<Clock, Duration> tp) noexcept {
        this->d = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch());
        this->absolute = true;
    }
    template <class Rep, class Period>
    constexpr deadline(std::chrono::duration<Rep, Period> d) noexcept {
        this->d = std::chrono::duration_cast<std::chrono::nanoseconds>(d);
        this->absolute = false;
    }

private:
    friend __kernel_timespec to_timespec(const deadline& d);
    std::chrono::nanoseconds d;
    bool absolute = false;
};

}  // namespace cor3ntin::corio
