#pragma once
#include <corio/concepts.hpp>

namespace cor3ntin::corio {

template <typename Predecessor, typename Func>
struct transform_sender {
    [[no_unique_address]] Predecessor pred_;
    [[no_unique_address]] Func func_;

    template <template <typename...> class Tuple>
    struct transform_result {
    private:
        template <typename Result, typename = void>
        struct impl {
            using type = Tuple<Result>;
        };
        template <typename Result>
        struct impl<Result, std::enable_if_t<std::is_void_v<Result>>> {
            using type = Tuple<>;
        };

    public:
        template <typename... Args>
        using apply = typename impl<std::invoke_result_t<Func, Args...>>::type;
    };

    template <typename... Args>
    using is_overload_noexcept =
        std::bool_constant<noexcept(std::invoke(std::declval<Func>(), std::declval<Args>()...))>;

    template <template <typename...> class Variant>
    struct calculate_errors {
    public:
        template <typename... Errors>
        using apply = std::conditional_t<
            Predecessor::template value_types<std::conjunction, is_overload_noexcept>::value,
            Variant<Errors...>, details::deduplicate_t<Variant<Errors..., std::exception_ptr>>>;
    };

    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = details::deduplicate_t<typename Predecessor::template value_types<
        Variant, transform_result<Tuple>::template apply>>;

    template <template <typename...> class Variant>
    using error_types =
        typename Predecessor::template error_types<calculate_errors<Variant>::template apply>;

    template <typename Receiver>
    struct transform_receiver {
        [[no_unique_address]] Func func_;
        [[no_unique_address]] Receiver receiver_;

        template <typename... Values>
            void set_value(Values&&... values) && noexcept {
            using result_type = std::invoke_result_t<Func, Values...>;
            if constexpr(std::is_void_v<result_type>) {
                if constexpr(noexcept(std::invoke((Func &&) func_, (Values &&) values...))) {
                    std::invoke((Func &&) func_, (Values &&) values...);
                    execution::set_value((Receiver &&) receiver_);
                } else {
                    try {
                        std::invoke((Func &&) func_, (Values &&) values...);
                        execution::set_value((Receiver &&) receiver_);
                    } catch(...) {
                        execution::set_error((Receiver &&) receiver_, std::current_exception());
                    }
                }
            } else {
                if constexpr(noexcept(std::invoke((Func &&) func_, (Values &&) values...))) {
                    execution::set_value((Receiver &&) receiver_,
                                         std::invoke((Func &&) func_, (Values &&) values...));
                } else {
                    try {
                        execution::set_value((Receiver &&) receiver_,
                                             std::invoke((Func &&) func_, (Values &&) values...));
                    } catch(...) {
                        execution::set_error((Receiver &&) receiver_, std::current_exception());
                    }
                }
            }
        }

        template <typename Error>
            void set_error(Error&& error) && noexcept {
            execution::set_error((Receiver &&) receiver_, (Error &&) error);
        }

        void set_done() && noexcept {
            execution::set_done((Receiver &&) receiver_);
        }
    };

    template <typename Receiver>
    auto connect(Receiver&& receiver) && {
        return execution::connect(std::forward<Predecessor>(pred_),
                                  transform_receiver<std::remove_cvref_t<Receiver>>{
                                      std::forward<Func>(func_), std::forward<Receiver>(receiver)});
    }
};

template <typename Sender, typename Func>
auto transform(Sender&& predecessor, Func&& func) {
    return transform_sender<std::remove_cvref_t<Sender>, std::decay_t<Func>>{
        (Sender &&) predecessor, (Func &&) func};
}

}  // namespace cor3ntin::corio