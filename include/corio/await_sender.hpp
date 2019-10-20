#pragma once
#include <corio/concepts.hpp>
#include <experimental/coroutine>

template <typename Sender>
struct sender_awaiter {
private:
  using value_type  = int; //sender_values_t<Sender, detail::identity_or_void_t>;
  using coro_handle = std::experimental::coroutine_handle<>;

  std::add_pointer_t<Sender> sender_{};
  enum class state { empty, value, exception };

  coro_handle continuation_{};
  state state_ = state::empty;

  union {
    value_type value_{};
    std::exception_ptr exception_;
  };

  //using is_always_blocking = property_query<From, is_always_blocking<>>;

struct internal_receiver {
    sender_awaiter* this_;

    //using receiver_category = receiver_tag;

    template <class U>
    requires cor3ntin::corio::concepts::convertible_to<U, value_type>
    void set_value(U&& value)
      noexcept(std::is_nothrow_constructible<value_type, U>::value) {
      this_->value_.construct(static_cast<U&&>(value));
      this_->state_ = state::value;
    }

    template <class V = value_type>
    requires std::is_void_v<V>
    void set_value() noexcept {
      this_->value_.construct();
      this_->state_ = state::value;
    }

    void set_done() noexcept {
      //if (!is_always_blocking::value)
        this_->continuation_.resume();
    }

    template<typename Error>
    void set_error(Error error) noexcept {
      assert(this_->state_ != state::value);
      if constexpr(std::is_same<Error, std::exception_ptr>::value){
        this_->exception_.construct(std::move(error));
      } else {
        this_->exception_.construct(std::make_exception_ptr(std::move(error)));
      }
      this_->state_ = state::exception;
      //if (!is_always_blocking::value)
      this_->continuation_.resume();
    }
  };

public:
  sender_awaiter() {}
  sender_awaiter(Sender&& sender) noexcept
  : sender_(std::addressof(sender))
  {}
  sender_awaiter(sender_awaiter &&that)
    noexcept(std::is_nothrow_move_constructible<value_type>::value ||
      std::is_void<value_type>::value)
  : sender_(std::exchange(that.sender_, nullptr))
  , continuation_{std::exchange(that.continuation_, {})}
  , state_(that.state_) {
    if (that.state_ == state::value) {
        if constexpr(!std::is_void<value_type>::value) {
          id(value_).construct(std::move(that.value_).get());
        }
        else {
            that.value_.destruct();
            that.state_ = state::empty;
        }
    } else if (that.state_ == state::exception) {
      exception_.construct(std::move(that.exception_).get());
      that.exception_.destruct();
      that.state_ = state::empty;
    }
  }

  ~sender_awaiter() {
    if (state_ == state::value) {
      value_.destruct();
    } else if (state_ == state::exception) {
      exception_.destruct();
    }
  }

  static constexpr bool await_ready() noexcept {
    return false;
  }

  // Add detection and handling of blocking completion here, and
  // return 'false' from await_suspend() in that case rather than
  // potentially recursively resuming the awaiting coroutine which
  // could eventually lead to a stack-overflow.
  using await_suspend_result_t =
    std::conditional_t<true, bool, void>;

  await_suspend_result_t await_suspend(coro_handle continuation) noexcept {
    continuation_ = continuation;
    //pushmi::submit(static_cast<From&&>(*sender_), internal_receiver{this});
    return await_suspend_result_t(); // return false or void
  }

  decltype(auto) await_resume() {
    if (state_ == state::exception) {
      std::rethrow_exception(std::move(exception_).get());
    } else if (state_ == state::empty) {
      //throw operation_cancelled{};
    } else {
      return std::move(value_).get();
    }
  }
};

template<typename From>
sender_awaiter(From&&) -> sender_awaiter<From>;

/*
template <sender S>
sender_awaiter<S> operator co_await(S&& sender) {
    return static_cast<S&&>(from);
}
*/

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