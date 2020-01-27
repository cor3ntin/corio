#pragma once
#include <corio/io_uring/base.hpp>

namespace cor3ntin::corio::iouring::cancel {
template <typename R>
class operation;
class sender : public base_sender {
    template <typename R>
    friend class operation;
    friend io_uring_context;
    sender(io_uring_context* ctx, const operation_base* const op) : base_sender(ctx), m_op(op) {}

private:
    const operation_base* const m_op;

public:
    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<>;

    static constexpr bool sends_done = true;

    template <typename Sender, execution::receiver R>
    using operation_type = operation<R>;

    template <execution::receiver R>
    auto connect(R&& r) && {
        return operation(std::move(*this), std::forward<R>(r));
    }
};

template <typename R>
class operation : public operation_base {
    friend sender;
    friend io_uring_context;

public:
    operation(sender s, R&& r)
        : operation_base(s.m_ctx), m_sender(std::move(s)), m_receiver(std::move(r)) {}

protected:
    void set_result(const io_uring_cqe* const cqe) noexcept override {
        if(cqe->res >= 0) {
            execution::set_value(m_receiver);
        }
    }
    void set_done() noexcept override {
        // cancelled the cancel operation ?
    }
    void prepare(io_uring_sqe* const sqe) noexcept override {
        io_uring_prep_cancel(sqe, (void*)(m_sender.m_op), 0);
    }

private:
    sender m_sender;
    R m_receiver;
};
}  // namespace cor3ntin::corio::iouring::cancel