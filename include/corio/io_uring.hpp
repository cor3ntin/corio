#pragma once
#include <corio/io_uring/base.hpp>
#include <corio/io_uring/schedule.hpp>
#include <corio/io_uring/cancel.hpp>
#include <corio/io_uring/read.hpp>

namespace cor3ntin::corio {
class io_uring_context;

namespace iouring {
    class scheduler {
    public:
        // scheduler(io_uring_context* ctx) : m_ctx(ctx) {}

        schedule::sender schedule() {
            return schedule::sender{m_ctx};
        }
        schedule::sender schedule(deadline d) {
            return schedule::sender{m_ctx, d};
        }

        friend auto async_read(iouring::scheduler sch, iouring::native_file_handle fd, void* buffer,
                               std::size_t size) {
            return read::sender(sch.m_ctx, fd, buffer, size);
        }

    private:
        template <typename R>
        friend class operation;
        friend io_uring_context;
        scheduler(io_uring_context* ctx) : m_ctx(ctx) {}

    private:
        io_uring_context* m_ctx;
    };
}  // namespace iouring


class io_uring_context {
    friend iouring::operation_base;

public:
    io_uring_context() {
        // init();
    }
    ~io_uring_context() {}

    void run(corio::stop_token stop_token) {
        stop_callback _(stop_token, [this] {
            m_stopped = true;
            notify();
        });
        init();
        schedule_queue_read();
        while(!m_stopped) {
            schedule_pendings();
            if(io_uring_submit(&m_ring) < 0) {
                std::cout << "Submit failed\n";
            }
            struct io_uring_cqe* cqe;
            auto ret = io_uring_wait_cqe(&m_ring, &cqe);
            if(!cqe) {
                std::cout << "No cqe";
                continue;
            }
            if(cqe->user_data == 0) {
                // ignore, maybe a cancel operation ?
            } else if(cqe->user_data == uint64_t(this)) {
                uint64_t c;
                eventfd_read(m_notify_fd, &c);
                m_notify = true;
            } else {
                // std::cout << "Operation " << cqe->res << " " << cqe->user_data << "\n";
                auto op = reinterpret_cast<iouring::operation_base*>(cqe->user_data);
                if(op) {
                    op->set_result(cqe);
                }
            }
            io_uring_cqe_seen(&m_ring, cqe);
        }
        io_uring_queue_exit(&m_ring);
    }
    auto scheduler() noexcept {
        return iouring::scheduler{this};
    }

private:
    static constexpr int URING_ENTRIES = 128;

    struct io_uring m_ring;
    std::atomic_int m_notify_fd = -1;
    intrusive_mpsc_queue<iouring::operation_base> m_queue;
    std::atomic_bool m_notify = true;
    std::atomic_bool m_stopped = false;

    void init() {
        m_notify_fd = ::eventfd(0, O_NONBLOCK);
        if(m_notify_fd < 0) {
            fprintf(stderr, "ring setup failed %d %s\n", errno, strerror(errno));
        }

        auto ret = io_uring_queue_init(URING_ENTRIES, &m_ring, 0);
        if(ret) {
            fprintf(stderr, "ring setup failed %d %s\n", ret, strerror(-ret));
            std::terminate();
        }
    }

    void enqueue_operation(iouring::operation_base* op) {
        if(m_stopped) {
            op->set_done();
        }
        m_queue.push(op);
        notify();
    }
    void notify() {
        eventfd_write(m_notify_fd, 1);
    }

    void schedule_queue_read() {
        auto sqe = io_uring_get_sqe(&m_ring);
        if(!sqe) {
            std::cout << "No sqe\n\n";
            // Queue full - not m_notifysure what we should do
        }
        io_uring_prep_poll_add(sqe, m_notify_fd, POLLIN);
        sqe->user_data = uint64_t(this);
        m_notify = false;
    }

    void schedule_pendings() {
        while(iouring::operation_base* op = m_queue.front()) {
            // check cqe size too
            auto sqe = io_uring_get_sqe(&m_ring);
            if(!sqe) {
                // Queue full
                break;
            }
            op->prepare(sqe);
            sqe->user_data = uint64_t(op);
            m_queue.pop();
            if(m_notify) {
                schedule_queue_read();
                m_notify = false;
            }
        }
    }
};

namespace iouring {
    void operation_base::start() noexcept {
        m_ctx->enqueue_operation(this);
    }
}  // namespace iouring

}  // namespace cor3ntin::corio
