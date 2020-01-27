#pragma once
#include <corio/io_uring/base.hpp>

namespace cor3ntin::corio::iouring::read {
template <typename R>
class operation;
class sender : public base_sender {
    template <typename R>
    friend class operation;
    friend io_uring_context;

public:
    sender(io_uring_context* ctx, native_file_handle fd, void* buffer, std::size_t size)
        : base_sender(ctx), m_fd(fd), m_buffer(buffer), m_size(size) {}

    template <template <typename...> class Variant, template <typename...> class Tuple>
    using value_types = Variant<Tuple<std::size_t>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::error_code>;

    static constexpr bool sends_done = true;

    template <typename Sender, execution::receiver<std::error_code> R>
    using operation_type = operation<R>;

    template <execution::receiver<std::error_code> R>
    auto connect(R&& r) && {
        return operation(std::move(*this), std::forward<R>(r));
    }

    native_file_handle m_fd;
    void* m_buffer;
    std::size_t m_size;
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
        if(cqe->res > 0) {
            execution::set_value(m_receiver, cqe->res);
        } else {
            execution::set_error(m_receiver, std::make_error_code(std::errc(-cqe->res)));
        }
    }

    void set_done() noexcept override {
        execution::set_done(m_receiver);
    }

    void prepare(io_uring_sqe* const sqe) noexcept override {
        io_uring_prep_read(sqe, m_sender.m_fd, m_sender.m_buffer, m_sender.m_size, 0);
    }

private:
    R m_receiver;
    sender m_sender;
};
}  // namespace cor3ntin::corio::iouring::read