#pragma once
#include <stl2/concepts.hpp>
#include <type_traits>
#include <exception>
#include <optional>
#include <functional>
#include <cstdio>
#include <corio/tag_invoke.hpp>
#include <corio/spawn.hpp>
#include <corio/meta.hpp>

namespace cor3ntin::corio {

namespace concepts = std::experimental::ranges;

namespace execution {

    class sink_receiver {
    public:
        template <typename... T>
        void set_value(T&&...) {}
        template <typename... T>
        [[noreturn]] void set_error(T&&...) noexcept {
            std::terminate();
        }
        void set_done() noexcept {}
    };

    namespace __set_done_ns {
        struct __set_done_base {};
    }  // namespace __set_done_ns
    inline constexpr struct __set_done_fn : __set_done_ns::__set_done_base {
        template <typename Receiver>
        requires cor3ntin::corio::tag_invocable<__set_done_fn, Receiver> void
        operator()(Receiver&& r) const noexcept {
            cor3ntin::corio::tag_invoke(*this, (Receiver &&) r);
        }
        template <typename Receiver>
        requires requires(Receiver& r) {
            r.set_done();
        }
        friend void tag_invoke(__set_done_fn, Receiver&& r) noexcept {
            r.set_done();
        }
    } set_done;


    namespace __set_value_ns {
        struct __set_value_base {};
    }  // namespace __set_value_ns
    inline constexpr struct __set_value_fn : __set_value_ns::__set_value_base {
        template <typename Receiver, typename... Value>
        requires cor3ntin::corio::tag_invocable<__set_value_fn, Receiver, Value...> void
        operator()(Receiver&& r, Value&&... v) const noexcept {
            cor3ntin::corio::tag_invoke(*this, (Receiver &&) r, (Value &&) v...);
        }
        template <typename Receiver, typename... Value>
        requires requires(Receiver& r, Value&&... v) {
            {r.set_value((Value &&) v...)};
        }
        friend void tag_invoke(__set_value_fn, Receiver&& r, Value&&... v) noexcept {
            r.set_value((Value &&) v...);
        }
    } set_value;


    namespace __set_error_ns {
        struct __set_error_base {};
    }  // namespace __set_error_ns
    inline constexpr struct __set_error_fn : __set_error_ns::__set_error_base {
        template <typename Receiver, typename Error>
        requires cor3ntin::corio::tag_invocable<__set_error_fn, Receiver, Error> void
        operator()(Receiver&& r, Error&& e) const noexcept {
            cor3ntin::corio::tag_invoke(*this, (Receiver &&) r, (Error &&) e);
        }
        template <typename Receiver, typename Error>
        requires requires(Receiver&& r, Error&& e) {
            r.set_error(e);
        }
        friend void tag_invoke(__set_error_fn, Receiver&& r, Error&& e) noexcept {
            r.set_error(e);
        }
    } set_error;


    namespace __connect_ns {
        struct __connect_base {};
    }  // namespace __connect_ns

    inline constexpr struct __connect_fn : __connect_ns::__connect_base {
        template <typename Sender, typename Receiver>
        requires cor3ntin::corio::tag_invocable<__connect_fn, Sender, Receiver> auto
        operator()(Sender&& s, Receiver&& r) const {
            return cor3ntin::corio::tag_invoke(*this, std::forward<Sender>(s), (Receiver &&) r);
        }

        template <typename Sender, typename Receiver>
        requires requires(Sender&& s, Receiver&& r) {
            std::forward<Sender>(s).connect((Receiver &&) r);
        }
        friend auto tag_invoke(__connect_fn, Sender&& s, Receiver&& r) {
            return std::forward<Sender>(s).connect((Receiver &&) r);
        }
    } connect;

    namespace __start_ns {
        struct __start_base {};
    }  // namespace __start_ns

    inline constexpr struct __start_fn : __start_ns::__start_base {
        template <typename Operation>
        requires cor3ntin::corio::tag_invocable<__start_fn, Operation&> void
        operator()(Operation& op) const {
            return cor3ntin::corio::tag_invoke(*this, op);
        }

        template <typename Operation>
        requires requires(Operation& op) {
            { op.start() }
            ->concepts::same_as<void>;
        }
        friend auto tag_invoke(__start_fn, Operation& op) {
            op.start();
        }
    } start;


    template <typename T, typename E = std::exception_ptr>
    concept receiver = concepts::move_constructible<std::remove_cvref_t<T>>&&
        details::nothrow_move_or_copy_constructible<std::remove_cvref_t<T>>&& requires(T&& t,
                                                                                       E&& e) {
        { execution::set_done((T &&) t) }
        noexcept;
        { execution::set_error((T &&) t, (E &&) e) }
        noexcept;
    };

    template <typename T, typename... Val>
    concept receiver_of = receiver<T>&& requires(T&& t, Val&&... val) {
        execution::set_value((T &&) t, (Val &&) val...);
    };

    namespace details {
        template <typename T>
        concept sealed_object = !concepts::move_constructible<std::remove_cvref_t<T>> &&
            !concepts::copy_constructible<std::remove_cvref_t<T>>;
    }


    template <typename Op>
    concept operation = details::sealed_object<Op>&& requires(Op& t) {
        { execution::start(t) }
        ->concepts::same_as<void>;
    };


    namespace details {
        template <class S, class R>
        concept sender_to_impl = requires(S&& s, R&& r) {
            {execution::connect(std::forward<S>(s), (R &&) r)};
            { execution::spawn((S &&) s, (R &&) r) }
            ->concepts::same_as<void>;
        };
    }  // namespace details
    template <typename S>
    concept sender = concepts::move_constructible<std::remove_cvref_t<S>>&&
        details::sender_to_impl<S, sink_receiver>;


    template <class S, class R>
    concept sender_to = sender<S>&& receiver<R>&& details::sender_to_impl<S, R>;


    template <template <template <class...> class, template <class...> class> class>
    struct __test_has_values;

    template <template <template <class...> class> class>
    struct __test_has_errors;

    template <class T>
    concept __has_sender_types = requires {
        typename __test_has_values<T::template value_types>;
        typename __test_has_errors<T::template error_types>;
        typename std::bool_constant<T::sends_done>;
    };
    struct __void_sender {
        template <template <class...> class Tuple, template <class...> class Variant>
        using value_types = Variant<Tuple<>>;
        template <template <class...> class Variant>
        using error_types = Variant<std::exception_ptr>;
        static constexpr bool sends_done = true;
    };
    template <class S>
    struct __typed_sender {
        template <template <class...> class Tuple, template <class...> class Variant>
        using value_types = typename S::template value_types<Tuple, Variant>;
        template <template <class...> class Variant>
        using error_types = typename S::template error_types<Variant>;
        static constexpr bool sends_done = S::sends_done;
    };

    using sender_base = struct __sender_base {};
    struct __no_sender_traits {
        using __unspecialized = void;
    };

    template <class S>
    auto __sender_traits_base_fn() {
        if constexpr(__has_sender_types<S>) {
            return __typed_sender<S>{};
        } else if constexpr(concepts::derived_from<S, sender_base>) {
            return sender_base{};
        } else {
            return __no_sender_traits{};
        }
    }
    template <class S>
    struct sender_traits : decltype(__sender_traits_base_fn<S>()) {};

    template <class S>
    concept typed_sender = sender<S>&& __has_sender_types<sender_traits<std::remove_cvref_t<S>>>;


    template <typename Sender>
    using single_value_result_t = typename std::remove_reference_t<Sender>::template value_types<
        corio::details::single_type, corio::details::single_value_type>::type::type;

    template <class S>
    concept typed_sender_single = typed_sender<S>&& requires {
        typename single_value_result_t<S>;
    };

    template <typename S>
    concept scheduler = requires(S s) {
        { s.schedule() }
        ->sender;
    };


}  // namespace execution

}  // namespace cor3ntin::corio

namespace {

using namespace cor3ntin::corio;
static_assert(execution::receiver<execution::sink_receiver>);
}  // namespace