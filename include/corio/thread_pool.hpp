#pragma once
#include <corio/concepts.hpp>
#include <vector>
#include <vector>
#include <thread>
#include <queue>
#include <exception>

namespace cor3ntin::corio {

class static_thread_pool {

    class stp_scheduler;
    template <typename R>
    class schedule_operation;

    class task_sender {
        template <typename R>
        friend class schedule_operation;
        friend class stp_scheduler;


        task_sender(static_thread_pool& pool) : m_pool(pool) {}
        static_thread_pool& m_pool;

    public:
        template <typename Sender, execution::receiver R>
        using operation_type = schedule_operation<R>;

        task_sender(const task_sender&) = delete;
        task_sender(task_sender&&) noexcept = default;

        template <execution::receiver R>
        auto connect(R&& r) && {
            return schedule_operation(std::move(*this), std::forward<R>(r));
        }
    };

    class operation_base {
        friend class static_thread_pool;

        operation_base() = default;
        // the state is neither copyable nor movable
        operation_base(const operation_base&) = delete;
        operation_base(operation_base&&) = delete;

    protected:
        virtual void set_value() noexcept = 0;
        virtual void set_error() noexcept = 0;
        virtual void set_done() noexcept = 0;
        operation_base* m_next = nullptr;
    };

    template <typename R>
    class schedule_operation : public operation_base {
        friend class task_sender;
        schedule_operation(task_sender s, R r) : m_sender(std::move(s)), m_receiver(std::move(r)) {}

        task_sender m_sender;
        R m_receiver;


    protected:
        void set_value() noexcept override {
            m_receiver.set_value();
        }
        void set_done() noexcept override {
            m_receiver.set_done();
        }
        void set_error() noexcept override {
            m_receiver.set_done();
        }

    public:
        void start() noexcept {
            m_sender.m_pool.execute(*this);
        }
    };

    template <execution::receiver R>
    class depleted_operation;


    class depleted_sender {
        friend class static_thread_pool;
        depleted_sender(static_thread_pool& pool) : m_pool(pool) {}

        static_thread_pool& m_pool;

    public:
        template <typename sender, execution::receiver R>
        using operation_type = depleted_operation<R>;


        template <typename R>
            auto connect(R&& r) && noexcept {
            return depleted_operation{std::move(*this), std::forward<R>(r)};
        }
    };

    template <execution::receiver R>
    class depleted_operation : public operation_base {
        friend class depleted_sender;
        depleted_operation(depleted_sender sender, R r)
            : m_sender(std::move(sender)), m_receiver(std::move(r)) {}

        depleted_sender m_sender;
        R m_receiver;

    protected:
        void set_value() noexcept override {
            m_receiver.set_value();
        }
        void set_done() noexcept override {
            m_receiver.set_done();
        }
        void set_error() noexcept override {
            m_receiver.set_done();
        }

    public:
        void start() noexcept {
            m_sender.m_pool.register_depleted_sender(*this);
        }
    };


    class stp_scheduler {
    public:
        stp_scheduler(const stp_scheduler&) = delete;
        stp_scheduler(stp_scheduler&&) noexcept = default;
        task_sender schedule() const noexcept {
            return task_sender(m_pool);
        }

    private:
        friend class static_thread_pool;
        stp_scheduler(static_thread_pool& pool) : m_pool(pool){};


        static_thread_pool& m_pool;
    };

public:
    static_thread_pool(std::size_t n) {
        for(std::size_t i = 0; i < n; ++i)
            m_threads.emplace_back([this] { attach(); });
    }


    void attach() {
        std::unique_lock lock(m_mutex);

        while(true) {
            if(m_stopped)
                return;

            m_condition.wait(lock);

            while(m_head) {
                operation_base& op = *m_head;
                m_head = m_head->m_next;
                lock.unlock();
                op.set_value();
                lock.lock();
            }
            on_depleted();
            continue;
        }
    }


    void stop() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stopped = true;

        for(auto op = m_head; op; op = op->m_next) {
            lock.unlock();
            op->set_done();
            lock.lock();
            m_condition.notify_all();
        }

        for(operation_base* op = m_depleted_head; op != nullptr; op = op->m_next) {
            lock.unlock();
            op->set_done();
            lock.lock();
            m_condition.notify_all();
        }

        m_head = m_tail = nullptr;
        m_condition.notify_all();

        lock.unlock();

        for(auto&& t : m_threads) {
            t.join();
            m_condition.notify_all();
        }
    }

    auto scheduler() noexcept {
        return stp_scheduler(*this);
    }

    depleted_sender depleted() noexcept {
        return depleted_sender(*this);
    }


    ~static_thread_pool() {
        stop();
    }

private:
    void execute(static_thread_pool::operation_base& op) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_head == nullptr) {
            m_head = m_tail = &op;
        } else {
            m_tail->m_next = &op;
            m_tail = &op;
        }
        m_condition.notify_all();
    }

    void register_depleted_sender(static_thread_pool::operation_base& op) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_depleted_head == nullptr) {
            m_depleted_head = m_depleted_tail = &op;
        } else {
            m_depleted_tail->m_next = &op;
            m_depleted_head = &op;
        }
        m_condition.notify_all();
    }

    void on_depleted() {
        m_tail = nullptr;
        for(operation_base* op = m_depleted_head; op != nullptr; op = op->m_next) {
            op->set_value();
        }
        m_depleted_head = nullptr;
        m_condition.notify_all();
    }


    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::thread> m_threads;
    operation_base* m_head = nullptr;
    operation_base* m_tail = nullptr;

    operation_base* m_depleted_head = nullptr;
    operation_base* m_depleted_tail = nullptr;
    bool m_stopped = false;
};


}  // namespace cor3ntin::corio
