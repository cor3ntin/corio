#pragma once
#include <corio/concepts.hpp>


namespace cor3ntin::corio {
template <concepts::invocable F>
struct as_receiver {
private:
    using invocable_type = std::remove_cvref_t<F>;
    invocable_type f_;

public:
    explicit as_receiver(
        invocable_type&& f) requires std::is_nothrow_move_constructible_v<invocable_type>
        : f_(std::move(f)) {}
    explicit as_receiver(const invocable_type& f) noexcept : f_(f) {}
    as_receiver(as_receiver&& other) noexcept = default;
    void set_value() {
        std::invoke(f_);
    }
    void set_error(std::exception_ptr) {
        std::terminate();
    }
    void set_done() noexcept {}
};

template <concepts::invocable F>
as_receiver(F)->as_receiver<F>;

}  // namespace cor3ntin::corio