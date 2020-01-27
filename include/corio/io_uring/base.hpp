#pragma once
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <liburing.h>
#include <corio/concepts.hpp>
#include <corio/deadline.hpp>
#include <corio/intrusive_linked_list.hpp>

namespace cor3ntin::corio {
class io_uring_context;

__kernel_timespec to_timespec(const deadline& d) {
    auto duration = d.d;
    auto secs = duration_cast<std::chrono::seconds>(duration);
    duration -= secs;
    return {secs.count(), duration.count()};
}

namespace iouring {
    using native_file_handle = int;
    class scheduler;

    class base_sender {
    public:
        base_sender(io_uring_context* ctx) : m_ctx(ctx) {}
        base_sender(const base_sender&) = delete;
        base_sender(base_sender&&) noexcept = default;

    protected:
        io_uring_context* m_ctx;
    };

    class operation_base : public intrusive_mpsc_queue_node {
        friend intrusive_mpsc_queue_node;
        friend io_uring_context;

    public:
        operation_base(io_uring_context* ctx) : m_ctx(ctx) {}
        // the state is neither copyable nor movable
        operation_base(const operation_base&) = delete;
        operation_base(operation_base&&) = delete;
        virtual void start() noexcept;

    protected:
        io_uring_context* m_ctx;
        virtual void set_result(const io_uring_cqe* const res) noexcept = 0;
        virtual void set_done() noexcept = 0;
        virtual void prepare(io_uring_sqe* const sqe) noexcept = 0;

    private:
    };
}  // namespace iouring
}  // namespace cor3ntin::corio