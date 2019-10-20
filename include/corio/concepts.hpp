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
    template<class T>
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
    }
    inline constexpr struct __set_done_fn : __set_done_ns::__set_done_base {
        template<typename Receiver>
        requires cor3ntin::corio::tag_invocable<__set_done_fn, Receiver>
        void operator()(Receiver&& r) const noexcept{
            cor3ntin::corio::tag_invoke(*this, (Receiver&&)r);
        }
        template<typename Receiver>
        requires requires (Receiver &r) {
            r.set_done();
        }
        friend void tag_invoke(__set_done_fn, Receiver&& r) noexcept {
            r.set_done();
        }
    } set_done;


    namespace __set_value_ns {
        struct __set_value_base {};
    }
    inline constexpr struct __set_value_fn : __set_value_ns::__set_value_base {
        template<typename Receiver, typename... Value>
        requires cor3ntin::corio::tag_invocable<__set_value_fn, Receiver, Value...>
        void operator()(Receiver&& r, Value&&... v) const noexcept{
            cor3ntin::corio::tag_invoke(*this, (Receiver&&)r, (Value&&)v...);
        }
        template<typename Receiver, typename... Value>
        requires requires (Receiver &r, Value&&...v) {
            { r.set_value((Value&&)v...) };
        }
        friend void tag_invoke(__set_value_fn, Receiver&& r, Value&&... v) noexcept {
            r.set_value((Value&&)v...);
        }
    } set_value;


    namespace __set_error_ns {
        struct __set_error_base {};
    }
    inline constexpr struct __set_error_fn : __set_error_ns::__set_error_base {
        template<typename Receiver, typename Error>
        requires cor3ntin::corio::tag_invocable<__set_error_fn, Receiver, Error>
        void operator()(Receiver&& r, Error&& e) const noexcept {
            cor3ntin::corio::tag_invoke(*this, (Receiver&&)r, (Error&&)e);
        }
        template<typename Receiver, typename Error>
        requires requires (Receiver &&r, Error&& e) {
            r.set_error(e);
        }
        friend void tag_invoke(__set_error_fn, Receiver&& r, Error&& e) noexcept{
            r.set_error(e);
        }
    } set_error;


    template<typename T, typename E = std::exception_ptr>
    concept receiver =
    concepts::move_constructible<std::remove_cvref_t<T>> &&
    details::nothrow_move_or_copy_constructible<std::remove_cvref_t<T>> &&
    requires(T&& t, E&& e) {
        { execution::set_done((T&&) t) } noexcept;
        { execution::set_error((T&&) t, (E&&) e) } noexcept;
    };

    template<typename T, typename... Val>
    concept receiver_of =
    receiver<T> &&
    requires(T&& t, Val&&... val) {
        execution::set_value((T&&) t, (Val&&) val...);
    };


    namespace __submit_ns {
        struct __submit_base {};
        template<typename Sender, typename Receiver>
        concept submittable_receiver = requires (Sender&& s, Receiver && r) {
            s.submit((Receiver &&)r);
        };
        template<typename Sender, typename Receiver>
        concept submittable_receiver_nm = !submittable_receiver<Sender, Receiver>
            && requires (Sender&& s, Receiver && r) {
            submit(s, r);
        };

        template<typename Sender, typename Receiver>
        void execute_on_submit(Sender&& e, Receiver&& r);
    }
    // Inherit from a type defined in submit_ns to make it an associated
    // namespace.
    inline constexpr struct __submit_fn : __submit_ns::__submit_base {
        template<typename Sender, typename Receiver>
        requires cor3ntin::corio::tag_invocable<__submit_fn, Sender, Receiver>
        void operator()(Sender&& s, Receiver&& r) const {
            cor3ntin::corio::tag_invoke(*this, (Sender&&)s, (Receiver&&)r);
        }
        template<typename Sender, typename Receiver>
        requires __submit_ns::submittable_receiver<Sender, Receiver>
        friend auto tag_invoke(__submit_fn, Sender&& s, Receiver&& r) noexcept{
            return s.submit(r);
        }

        template<typename Sender, typename Receiver>
        requires __submit_ns::submittable_receiver_nm<Sender, Receiver>
        friend auto tag_invoke(__submit_fn, Sender&& s, Receiver&& r) noexcept{
            return submit(s, r);
        }
    } submit;

    namespace details {
    template<class S, class R>
        concept sender_to_impl = requires(S&& s, R&& r) {
            execution::submit((S&&) s, (R&&) r);
        };
    }
    template <typename S>
    concept sender = concepts::move_constructible<std::remove_cvref_t<S>> && details::sender_to_impl<S, sink_receiver>;


    template<class S, class R>
    concept sender_to = sender<S> && receiver<R> && details::sender_to_impl<S, R>;


    namespace __execute_ns {
        struct __execute_base {};

        template<typename Executor, typename Fn>
        concept executable_function = concepts::invocable<Fn>
            && requires (Executor&& e, Fn&& f) {
                e.execute((Fn&&)f);
            };
    }

    inline constexpr struct __execute_fn : __execute_ns::__execute_base {
        template<typename Executor, typename Fn>
        requires cor3ntin::corio::tag_invocable<__execute_fn, Executor, Fn> && concepts::invocable<Fn>
        auto operator()(Executor&& e, Fn&& f) const noexcept {
            return cor3ntin::corio::tag_invoke(*this, (Executor&&)e, (Fn&&)f);
        }

        template<typename Executor, typename Fn>
        requires __execute_ns::executable_function<Executor, Fn>
        friend auto tag_invoke(__execute_fn, Executor&& e, Fn&& f) noexcept{
            return e.execute(f);
        }

    } execute;

    namespace details {
        struct invocable_archetype {
            template <typename... Args>
            void operator()(Args&&...) const noexcept;
        };
        template<class E, class F>
        concept executor_of_impl =
            concepts::invocable<F> &&
            std::is_nothrow_copy_constructible_v<E> &&
            std::is_nothrow_destructible_v<E> &&
            concepts::equality_comparable<E> &&
            requires(const E& e, F&& f) {
                execution::execute(e, (F&&)f);
            };
    }

    template<class E>
    concept executor = details::executor_of_impl<E, details::invocable_archetype>;

    template<class E, class F>
    concept executor_of = details::executor_of_impl<E, F>;


    namespace details {
        template<typename F>
        requires concepts::invocable<F>
        struct as_receiver {
            private:
                using invocable_type = std::remove_cvref_t<F>;
                invocable_type f_;
            public:
                explicit as_receiver(invocable_type&& f)
                requires std::is_nothrow_move_constructible_v<invocable_type> : f_(std::move(f)) {}
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

        template<typename R>
        requires receiver<R>
        struct as_invocable {
        private:
        using receiver_type = std::remove_cvref_t<R>;
        std::optional<receiver_type> r_ {};
        template <typename T>
        void try_init_(T&& r) {
            try {
            r_.emplace((decltype(r)&&) r);
            } catch(...) {
            execution::set_error(r, std::current_exception());
            }
        }
        public:
        explicit as_invocable(receiver_type&& r) noexcept {
            try_init_(std::move(r));
        }
        explicit as_invocable(const receiver_type& r) noexcept{
            try_init_(receiver_type(r));
        }
        as_invocable(as_invocable&& other) noexcept{
            if(other.r_) {
                try_init_(std::move(*other.r_));
                other.r_.reset();
            }
        }

        as_invocable(const as_invocable& other) noexcept{
            try_init_(*other.r_);
        }

        ~as_invocable() {
            if(r_)
            execution::set_done(*r_);
        }
        void operator()() noexcept {
            try {
                execution::set_value(*r_);
            } catch(...) {
                execution::set_error(*r_, std::current_exception());
            }
            r_.reset();
        }
        };

        template <typename I>
        bool operator==(const as_invocable<I>&, const as_invocable<I>&) {
            return true;
        };
        template <typename I>
        bool operator!=(const as_invocable<I>&, const as_invocable<I>&) {
                return false;
        };
    }


    template<typename Executor, typename Fn>
    void submit_on_execute(Executor&& e, Fn&& f) {
        execution::submit(e, details::as_receiver<Fn>(f));
    }

    namespace __submit_ns {
        template<typename Sender, typename Receiver, typename I = details::as_invocable<Receiver>>
        requires (!__submit_ns::submittable_receiver_nm<Sender, Receiver>
        && !__submit_ns::submittable_receiver_nm<Sender, Receiver>
        && concepts::invocable<I>
        && executor<Sender>)
        void do_execute_on_submit(Sender&& s, Receiver&& r) {
            execution::execute(s, I(r));
        }

        template<typename Sender, typename Receiver>
        void execute_on_submit(Sender&& s, Receiver&& r) {
            do_execute_on_submit((Sender&&)s, (Receiver&&)r);
        }
    }

} // namespace execution

} // namespace corio

//test

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
static_assert(execution::receiver<execution::details::as_receiver<execution::details::invocable_archetype>>);
static_assert(execution::receiver_of<execution::sink_receiver, int, int>);
static_assert(concepts::invocable<execution::details::as_invocable<execution::sink_receiver>>);
}