#pragma once
#include <vector>
#include <vector>
#include <thread>
#include <queue>
#include <exception>
#include <mutex>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <liburing.h>
#include <corio/concepts.hpp>
#include <corio/deadline.hpp>
#include <corio/stop_token.hpp>
#include <corio/intrusive_linked_list.hpp>
#include <corio/channel.hpp>

namespace cor3ntin::corio {
class io_uring_context;

__kernel_timespec to_timespec(const deadline& d) {
    auto duration = d.d;
    auto secs = duration_cast<std::chrono::seconds>(duration);
    duration -= secs;
    return {secs.count(), duration.count()};
}

namespace _iouring {
    template <typename R>
    class schedule_operation;
    class schedule_operation_base;
    class scheduler;

    class base_sender {
    public:
        base_sender(io_uring_context* ctx) : m_ctx(ctx) {}
        base_sender(const base_sender&) = delete;
        base_sender(base_sender&&) noexcept = default;

    protected:
        io_uring_context* m_ctx;
    };

    class task_sender : public base_sender {
        template <typename R>
        friend class schedule_operation;
        friend schedule_operation_base;
        friend scheduler;
        friend io_uring_context;
        task_sender(io_uring_context* ctx, deadline d = {}) : base_sender(ctx), m_deadline(d) {}

    private:
        deadline m_deadline;

    public:
        template <template <typename...> class Variant, template <typename...> class Tuple>
        using value_types = Variant<Tuple<>>;

        template <template <typename...> class Variant>
        using error_types = Variant<>;

        static constexpr bool sends_done = true;

        template <typename Sender, execution::receiver R>
        using operation_type = schedule_operation<R>;

        template <execution::receiver R>
        auto connect(R&& r) && {
            return schedule_operation(std::move(*this), std::forward<R>(r));
        }
    };


    class operation_base;
    class cancel_sender : public base_sender {
        friend class cancel_operation_base;
        template <typename R>
        friend class cancel_operation;
        friend class operation_base;
        friend io_uring_context;
        cancel_sender(io_uring_context* ctx, const operation_base* const op)
            : base_sender(ctx), m_op(op) {}

    private:
        const operation_base* const m_op;

    public:
        template <template <typename...> class Variant, template <typename...> class Tuple>
        using value_types = Variant<Tuple<>>;

        template <template <typename...> class Variant>
        using error_types = Variant<>;

        static constexpr bool sends_done = true;

        template <typename Sender, execution::receiver R>
        using operation_type = schedule_operation<R>;

        template <execution::receiver R>
        auto connect(R&& r) && {
            return cancel_operation(std::move(*this), std::forward<R>(r));
        }
    };

    class receiver {};

    class operation_base : public intrusive_mpsc_queue_node {
        friend intrusive_mpsc_queue_node;
        friend io_uring_context;

    protected:
        enum class operation_type {
            schedule,  //
            channel,
            cancel,
        };

    public:
        operation_base(operation_type type, io_uring_context* ctx) : m_type(type), m_ctx(ctx) {}
        // the state is neither copyable nor movable
        operation_base(const operation_base&) = delete;
        operation_base(operation_base&&) = delete;
        virtual void start() noexcept;

    protected:
        operation_base* m_prev = nullptr;
        operation_type m_type : 7;
        bool m_cancelled : 1 = false;
        io_uring_context* m_ctx;
        virtual void set_result(const io_uring_cqe* const res) noexcept = 0;
        virtual void set_done() noexcept = 0;

    private:
        operation_base* const get_next() const {
            return static_cast<operation_base* const>(next.load(std::memory_order_relaxed));
        }
        operation_base* const get_prev() const {
            return m_prev;
        }

        void set_next(operation_base* op) {
            next.store(op, std::memory_order_relaxed);
        }
        void set_prev(operation_base* op) {
            m_prev = op;
        }
    };

    class cancel_operation_base : public operation_base {
        friend cancel_sender;
        friend io_uring_context;

    public:
        cancel_operation_base(cancel_sender s)
            : operation_base(operation_type::cancel, s.m_ctx), m_sender(std::move(s)) {}

    private:
        cancel_sender m_sender;
    };


    template <typename R>
    class cancel_operation : public cancel_operation_base {
        friend cancel_sender;
        friend io_uring_context;

    public:
        cancel_operation(cancel_sender s, R&& r)
            : cancel_operation_base(std::move(s)), m_receiver(std::move(r)) {}

    protected:
        void set_result(const io_uring_cqe* const cqe) noexcept override {
            if(cqe->res >= 0) {
                execution::set_value(m_receiver);
            }
        }

        virtual void set_done() noexcept override {
            // cancelled the cancel operation ?
        }

    private:
        R m_receiver;
    };


    class schedule_operation_base : public operation_base {
        friend task_sender;
        friend io_uring_context;

    public:
        schedule_operation_base(task_sender s)
            : operation_base(operation_type::schedule, s.m_ctx), m_sender(std::move(s)) {}

    private:
        task_sender m_sender;
        __kernel_timespec m_ts;
    };

    template <typename R>
    class schedule_operation : public schedule_operation_base {
        friend class task_sender;
        schedule_operation(task_sender s, R&& r)
            : schedule_operation_base(std::move(s)), m_receiver(std::move(r)) {}

    protected:
        void set_result(const io_uring_cqe* const cqe) noexcept override {
            if(cqe->res >= 0 || cqe->res == -ETIME) {
                execution::set_value(m_receiver);
            }
        }
        virtual void set_done() noexcept override {
            execution::set_done(m_receiver);
        }

    private:
        R m_receiver;
    };

    class scheduler {
    public:
        task_sender schedule() {
            return task_sender{m_ctx};
        }
        task_sender schedule(deadline d) {
            return task_sender{m_ctx, d};
        }

    private:
        template <typename R>
        friend class operation;
        friend io_uring_context;
        scheduler(io_uring_context* ctx) : m_ctx(ctx) {}

    private:
        io_uring_context* m_ctx;
    };

    class channel_operation_base : public _iouring::operation_base {
        friend io_uring_context;

    public:
        channel_operation_base(io_uring_context* ctx, int fd)
            : _iouring::operation_base(operation_type::channel, ctx), m_op_fd(fd) {}

    protected:
        bool m_registered = false;
        int m_op_fd;
    };

}  // namespace _iouring

namespace details {
    template <typename T, typename Channel>
    class channel_implementation<io_uring_context, T, Channel>
        : public channel_implementation_base<T> {
        using C = channel_implementation<io_uring_context, T, Channel>;

        template <typename R>
        class read_operation;
        template <typename R>
        class write_operation;

        class read_sender : public _iouring::base_sender {
            friend io_uring_context;
            friend C;
            read_sender(io_uring_context* ctx, C* channel) : base_sender(ctx), m_channel(channel) {}
            C* m_channel;

        public:
            template <template <typename...> class Variant, template <typename...> class Tuple>
            using value_types = Variant<Tuple<T>>;

            template <template <typename...> class Variant>
            using error_types = Variant<>;

            static constexpr bool sends_done = true;

            template <typename Sender, execution::receiver R>
            using operation_type = read_operation<R>;

            template <execution::receiver R>
            auto connect(R&& r) && {
                return read_operation<R>(std::move(*this), std::forward<R>(r));
            }
        };

        class write_sender : public _iouring::base_sender {
            friend io_uring_context;
            friend C;
            write_sender(io_uring_context* ctx, C* channel, T value)
                : base_sender(ctx), m_channel(channel), m_value(std::move(value)) {}
            T m_value;
            C* m_channel;

        public:
            template <template <typename...> class Variant, template <typename...> class Tuple>
            using value_types = Variant<Tuple<T>>;

            template <template <typename...> class Variant>
            using error_types = Variant<>;

            static constexpr bool sends_done = true;

            template <typename Sender, execution::receiver R>
            using operation_type = write_operation<R>;

            template <execution::receiver R>
            auto connect(R&& r) && {
                return write_operation<R>(std::move(*this), std::forward<R>(r));
            }
        };

        template <typename R>
        class read_operation : public _iouring::channel_operation_base {
            friend read_sender;
            friend io_uring_context;

        public:
            read_operation(read_sender&& s, R&& receiver)
                : channel_operation_base(s.m_ctx, s.m_channel->m_read_fd)
                , m_sender(std::move(s))
                , m_receiver(std::move(receiver)) {}

            void start() noexcept override {
                handle_read();
            }

        private:
            void set_result(const io_uring_cqe* const res) noexcept override {
                const auto* chan = m_sender.m_channel;
                if(res->res < 0) {
                    execution::set_error(m_receiver, channel_closed{});
                    return;
                }
                eventfd_t c;
                eventfd_read(chan->m_read_fd, &c);
                if(chan->m_capacity == 0 && chan->m_queue.empty()) {
                    execution::set_error(m_receiver, channel_closed{});
                    return;
                }
                handle_read();
            }

            void set_done() noexcept override {
                execution::set_done(m_receiver);
            }

            void handle_read() {
                C* channel = m_sender.m_channel;
                std::unique_lock lock(channel->m_mutex);
                if(channel->m_queue.empty()) {
                    lock.unlock();
                    _iouring::channel_operation_base::start();
                    return;
                }
                auto value = std::move(channel->m_queue.front());
                channel->m_queue.pop();
                if(channel->m_capacity != 0)
                    notify_read();
                execution::set_value(m_receiver, std::move(value));
            }
            void notify_read() {
                eventfd_write(m_sender.m_channel->m_write_fd, 0);
            }

        private:
            read_sender m_sender;
            R m_receiver;
        };


        template <typename R>
        class write_operation : public _iouring::channel_operation_base {
            friend write_sender;
            friend io_uring_context;

        public:
            write_operation(write_sender&& s, R&& receiver)
                : channel_operation_base(s.m_ctx, s.m_channel->m_write_fd)
                , m_sender(std::move(s))
                , m_receiver(std::move(receiver)) {}

            void start() noexcept override {
                handle_write();
            }

        private:
            void set_result(const io_uring_cqe* const res) noexcept override {
                C* chan = m_sender.m_channel;
                if(res->res < 0) {
                    execution::set_error(m_receiver, channel_closed{});
                    return;
                }
                eventfd_t c;
                eventfd_read(m_sender.m_channel->m_read_fd, &c);
                if(chan->m_capacity == 0) {
                    execution::set_error(m_receiver, channel_closed{});
                }
                handle_write();
            }

            void set_done() noexcept override {
                execution::set_done(m_receiver);
            }

            void handle_write() {
                C* channel = m_sender.m_channel;
                std::unique_lock lock(channel->m_mutex);
                if(channel->m_queue.size() >= channel->m_capacity) {
                    lock.unlock();
                    _iouring::channel_operation_base::start();
                    return;
                }
                channel->m_queue.push(std::move(m_sender.m_value));
                notify_write();
                execution::set_value(m_receiver);
            }

            void notify_write() {
                eventfd_write(m_sender.m_channel->m_read_fd, 1);
            }

        private:
            write_sender m_sender;
            R m_receiver;
        };

    public:
        channel_implementation(io_uring_context& ctx, std::size_t capacity) : m_ctx(ctx) {
            m_read_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
            m_write_fd = eventfd(0, O_NONBLOCK | O_CLOEXEC);
            if(m_read_fd <= 0 || m_write_fd <= 0) {
                fprintf(stderr, "ring setup failed %d %s\n", errno, strerror(errno));
            }
            this->m_capacity = capacity;
        }
        channel_implementation(const channel_implementation&) = delete;
        ~channel_implementation() {
            std::cout << "delete\n";
            ::close(m_read_fd);
            ::close(m_write_fd);
        }

    protected:
        auto get_read_sender() {  //
            return read_sender(&m_ctx, this);
        }

        auto get_write_sender(T value) {
            return write_sender(&m_ctx, this, std::move(value));
        }

        void close() {
            this->m_capacity = 0;
            eventfd_write(m_read_fd, 1);
            eventfd_write(m_write_fd, 1);
        }

    private:
        io_uring_context& m_ctx;
        int m_read_fd;
        int m_write_fd;
    };
}  // namespace details

class io_uring_context {
    friend _iouring::operation_base;

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
        while(!m_stopped || m_queue.front() || m_head) {
            if(m_stopped) {
                cancel_pending_operations();
            } else {
                schedule_pendings();
            }
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
                std::cout << "Queue notified\n";
                uint64_t c;
                eventfd_read(m_notify_fd, &c);
                m_notify = true;
            } else {
                std::cout << "Operation " << cqe->res << " " << cqe->user_data << "\n";
                auto op = reinterpret_cast<_iouring::operation_base*>(cqe->user_data);
                if(op) {
                    pop_inflight_op(op);
                    op->set_result(cqe);
                }
            }
            io_uring_cqe_seen(&m_ring, cqe);
        }
        io_uring_queue_exit(&m_ring);
    }
    auto scheduler() noexcept {
        return _iouring::scheduler{this};
    }

private:
    static constexpr int URING_ENTRIES = 128;

    struct io_uring m_ring;
    std::atomic_int m_notify_fd = -1;
    intrusive_mpsc_queue<_iouring::operation_base> m_queue;
    std::atomic_bool m_notify = true;
    std::atomic_bool m_stopped = false;
    // Maintain a double-linked list of in-flight operation so we can cancel them
    _iouring::operation_base* m_head = nullptr;

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

    void enqueue_operation(_iouring::operation_base* op) {
        if(m_stopped) {
            op->set_done();
        }
        m_queue.push(op);
        notify();
    }
    void notify() {
        eventfd_write(m_notify_fd, 0);
    }

    void schedule_queue_read() {
        auto sqe = io_uring_get_sqe(&m_ring);
        if(!sqe) {
            std::cout << "No sqe\n\n";
            // Queue full - not m_notifysure what we should do
        }
        std::cout << "Prep read\n";
        io_uring_prep_poll_add(sqe, m_notify_fd, POLLIN);
        sqe->user_data = uint64_t(this);
        m_notify = false;
    }

    void schedule_pendings() {
        while(_iouring::operation_base* op = m_queue.front()) {
            bool res = false;
            using E = _iouring::operation_base::operation_type;
            // check cqe size too
            auto sqe = io_uring_get_sqe(&m_ring);
            if(!sqe) {
                // Queue full
                break;
            }
            switch(op->m_type) {
                case E::schedule: {
                    res =
                        try_schedule_one(sqe, static_cast<_iouring::schedule_operation_base*>(op));
                    break;
                }
                case E::channel: {
                    res = try_schedule_one(sqe, static_cast<_iouring::channel_operation_base*>(op));
                    break;
                }
                case E::cancel: {
                    res = try_schedule_one(sqe, static_cast<_iouring::cancel_operation_base*>(op));
                    break;
                }
            }
            push_inflight_op(op);
            m_queue.pop();
            if(m_notify) {
                schedule_queue_read();
                m_notify = false;
            }
        }
    }


    void cancel_pending_operations() {
        while(_iouring::operation_base* op = m_queue.front()) {
            // op->set_done();
            m_queue.pop();
        }
        const auto* n = m_head;
        while(n) {
            auto sqe = io_uring_get_sqe(&m_ring);
            if(!sqe) {
                // Queue full
                break;
            }
            io_uring_prep_cancel(sqe, (void*)n, 0);
            sqe->user_data = uint64_t(0);
            n = n->get_prev();
        }
    }

    void push_inflight_op(_iouring::operation_base* op) {
        op->set_next(nullptr);
        op->set_prev(m_head);
        if(m_head)
            m_head->set_next(op);
        m_head = op;
    }
    void pop_inflight_op(_iouring::operation_base* op) {
        auto prev = op->get_prev();
        auto next = op->get_next();
        if(prev) {
            prev->set_next(next);
        }
        if(next) {
            next->set_prev(prev);
        }
        if(op == m_head)
            m_head = next;
    }

    bool try_schedule_one(io_uring_sqe* sqe, _iouring::schedule_operation_base* op) {
        auto& ts = op->m_ts;
        ts = to_timespec(op->m_sender.m_deadline);
        std::cout << "Deadline " << ts.tv_sec << " " << ts.tv_nsec << "\n";
        if(ts.tv_sec == 0 && ts.tv_nsec == 0) {
            io_uring_prep_nop(sqe);
        } else {
            io_uring_prep_timeout(sqe, &op->m_ts, 0, 0);
        }
        sqe->user_data = uint64_t(op);
        return true;
    }
    bool try_schedule_one(io_uring_sqe* sqe, _iouring::channel_operation_base* op) {
        io_uring_prep_poll_add(sqe, op->m_op_fd, POLLIN);
        sqe->user_data = uint64_t(op);
        return true;
    }

    bool try_schedule_one(io_uring_sqe* sqe, _iouring::cancel_operation_base* op) {
        io_uring_prep_cancel(sqe, (void*)op->m_sender.m_op, 0);
        sqe->user_data = uint64_t(op);
        return true;
    }
};

namespace _iouring {
    void operation_base::start() noexcept {
        m_ctx->enqueue_operation(this);
    }
}  // namespace _iouring

}  // namespace cor3ntin::corio
