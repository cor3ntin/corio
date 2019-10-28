#pragma once
#include <corio/concepts.hpp>
#include <cppcoro/task.hpp>
#include <experimental/coroutine>
#include <variant>
#include <iostream>

namespace cor3ntin::corio {

struct operation_cancelled : std::exception {
    virtual const char* what() const noexcept {
        return "operation cancelled";
    }
};

template <typename Sender>
struct sender_awaiter {
private:
    // using is_always_blocking = property_query<From, is_always_blocking<>>;
    struct internal_receiver {
        sender_awaiter* this_;

        // using receiver_category = receiver_tag;

        template <class U>
        void set_value(U&& value) noexcept(std::is_nothrow_constructible_v<value_type, U>) {
            this_->m_data.template emplace<1>(std::forward<U>(value));
            this_->m_continuation.resume();
        }

        void set_value() noexcept {
            this_->m_data.template emplace<1>();
            this_->m_continuation.resume();
        }

        template <typename Error>
        void set_error(Error&& error) noexcept {
            if constexpr(std::is_same<Error, std::exception_ptr>::value) {
                this_->m_data.template emplace<2>(std::move(error));
            } else {
                this_->m_data.template emplace<2>(std::make_exception_ptr(std::move(error)));
            }
            // if (!is_always_blocking::value)
            this_->m_continuation.resume();
        }

        void set_done() noexcept {
            this_->m_data.template emplace<0>(std::monostate{});
            // if (!is_always_blocking::value)
            this_->m_continuation.resume();
        }
    };


    using value_type = int;
    using coro_handle = std::experimental::coroutine_handle<>;

    coro_handle m_continuation{};
    using operation_type = decltype(
        corio::execution::connect(std::declval<Sender>(), std::declval<internal_receiver>()));
    operation_type m_op;
    std::variant<std::monostate, value_type, std::exception_ptr> m_data;


public:
    sender_awaiter(Sender sender) noexcept
        : m_op(corio::execution::connect(std::move(sender), internal_receiver{this})) {
        printf("CTR\n");
    }
    sender_awaiter(sender_awaiter&& that) = default;
    ~sender_awaiter() {
        printf("DTR\n");
    }


    static constexpr bool await_ready() noexcept {
        return true;
    }

    // TODO HANDLE BLOCKING
    void await_suspend(coro_handle continuation) noexcept {
        printf("await_suspend\n");
        m_continuation = continuation;
        m_op.start();

        return;
    }

    decltype(auto) await_resume() {
        printf("await_resume\n");
        switch(m_data.index()) {
            case 0: throw operation_cancelled{}; break;
            case 1: return std::get<1>(m_data);
            case 2: std::rethrow_exception(std::move(std::get<2>(m_data))); break;
        }
        return std::get<1>(m_data);
    }
};

template <typename From>
sender_awaiter(From)->sender_awaiter<From>;

template <cor3ntin::corio::execution::sender S>
auto operator co_await(S&& sender) {
    return cor3ntin::corio::sender_awaiter(std::forward<S>(sender));
}

}  // namespace cor3ntin::corio


/*namespace awaitable_senders {
  // Any TypedSender that inherits from `sender` or `sender_traits` is
  // automatically awaitable with the following operator co_await through the
  // magic of associated namespaces. To make any other TypedSender awaitable,
  // from within the body of the awaiting coroutine, do:
  // `using namespace ::folly::pushmi::awaitable_senders;`
  PUSHMI_TEMPLATE(class From)( //
    requires not Awaiter<From> && SingleTypedSender<From> //
  ) //
  sender_awaiter<From> operator co_await(From&& from) {
    return static_cast<From&&>(from);
  }
} // namespace awaitable_senders
*/