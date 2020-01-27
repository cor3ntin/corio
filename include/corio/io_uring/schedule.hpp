#pragma once
#include <corio/io_uring/base.hpp>

namespace cor3ntin::corio::iouring::schedule {
template <typename R>
class operation;
class sender : public base_sender {
    template <typename R>
    friend class operation;
    friend scheduler;
    friend io_uring_context;
    sender(io_uring_context* ctx, deadline d = {}) : base_sender(ctx), m_deadline(d) {}

private:
    deadline m_deadline;

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
    operation(sender s, R&& r)
        : operation_base(s.m_ctx), m_sender(std::move(s)), m_receiver(std::move(r)) {}

protected:
    void set_result(const io_uring_cqe* const cqe) noexcept override {
        if(cqe->res >= 0 || cqe->res == -ETIME) {
            execution::set_value(m_receiver);
        }
    }
    virtual void set_done() noexcept override {
        execution::set_done(m_receiver);
    }
    void prepare(io_uring_sqe* const sqe) noexcept override {
        m_ts = to_timespec(m_sender.m_deadline);
        // std::cout << "Deadline " << m_ts.tv_sec << " " << m_ts.tv_nsec << "\n";
        if(m_ts.tv_sec == 0 && m_ts.tv_nsec == 0) {
            io_uring_prep_nop(sqe);
        } else {
            io_uring_prep_timeout(sqe, &m_ts, 0, 0);
        }
    }


private:
    sender m_sender;
    __kernel_timespec m_ts;
    R m_receiver;
};

}  // namespace cor3ntin::corio::iouring::schedule