#pragma once
#include <stl2/concepts.hpp>
#include <type_traits>
#include <exception>
#include <corio/tag_invoke.hpp>
#include <optional>
#include <functional>
#include <cstdio>

namespace cor3ntin::corio {

namespace concepts = std::experimental::ranges;

namespace details {
    template <class T>
    concept nothrow_move_or_copy_constructible =
        std::is_nothrow_move_constructible_v<T> || concepts::copy_constructible<T>;
}

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
        template <class S, class R>
        concept sender_to_impl = true; /*requires(S&& s, R&& r) {
            execution::submit((S &&) s, (R &&) r);
        };*/
    }                                  // namespace details
    template <typename S>
    concept sender = concepts::move_constructible<std::remove_cvref_t<S>>&&
        details::sender_to_impl<S, sink_receiver>;


    template <class S, class R>
    concept sender_to = sender<S>&& receiver<R>&& details::sender_to_impl<S, R>;


}  // namespace execution

}  // namespace cor3ntin::corio

// test

/*struct stupid_executor {
    template <typename Receiver>
    requires cor3ntin::corio::execution::receiver<Receiver>
    void submit(Receiver&& r) const {
        return r.set_value();
    }
};*/


namespace {

using namespace cor3ntin::corio;
static_assert(execution::receiver<execution::sink_receiver>);
/*static_assert(
    execution::receiver<execution::details::as_receiver<execution::details::invocable_archetype>>);
static_assert(execution::receiver_of<execution::sink_receiver, int, int>);
static_assert(concepts::invocable<execution::details::as_invocable<execution::sink_receiver>>);*/
}  // namespace