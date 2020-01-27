#pragma once
#include <utility>
#include <queue>
#include <optional>
#include <corio/then.hpp>
#include <corio/io_uring.hpp>

namespace cor3ntin::corio {

struct channel_closed : std::exception {
    virtual const char* what() const noexcept {
        return "channel closed";
    }
};


namespace details {

    struct linked_list_node {
        linked_list_node* next = nullptr;
    };

    template <typename T>
    struct linked_list {
        T* pop() {
            std::unique_lock lock(m_mutex);
            T* n = tail;
            if(!n)
                return n;
            if(head == n)
                head = nullptr;
            tail = static_cast<T*>(n->next);
            n->next = nullptr;
            return n;
        }
        T* front() {
            return tail;
        }

        void push(T* node) {
            std::unique_lock lock(m_mutex);
            if(head != nullptr)
                head->next = node;
            head = node;
            if(tail == nullptr)
                tail = node;
        }

    private:
        std::mutex m_mutex;
        T* head = nullptr;
        T* tail = nullptr;
    };


    template <typename scheduler>
    class event_notification {
    public:
        event_notification(scheduler sch) : m_scheduler(sch) {
            m_fd = eventfd(1, 0);
        }
        void notify() {
            eventfd_write(m_fd, 1);
        }
        void close() {
            ::close(m_fd);
        }
        auto wait() {
            return async_read(m_scheduler, m_fd, &m_event, sizeof(eventfd_t));
        }

    private:
        eventfd_t m_event;
        scheduler m_scheduler;
        int m_fd;
    };

    template <typename scheduler, typename T, bool Buffered>
    class channel {
    public:
        class read_operation_base : public linked_list_node {
            friend channel;

        protected:
            virtual void handle_error(std::error_code err) = 0;
            virtual void handle_value(T&& t) = 0;
        };

        class write_operation_base : public linked_list_node {
            friend channel;

        protected:
            virtual void handle_error(std::error_code err) = 0;
            virtual void handle_value() = 0;
            virtual T& value() = 0;
        };

        class read_channel {
            template <typename Receiver>
            class operation;
            template <typename Receiver>
            friend class operation;
            class sender {
            public:
                channel* m_channel;

            public:
                sender(channel* c) : m_channel(c) {}
                template <template <typename...> class Variant, template <typename...> class Tuple>
                using value_types = Variant<Tuple<T>>;

                template <template <typename...> class Variant>
                using error_types = Variant<>;

                static constexpr bool sends_done = true;

                template <typename Sender, execution::receiver R>
                using operation_type = read_channel::operation<R>;

                template <execution::receiver R>
                auto connect(R&& r) && {
                    return operation<R>(std::move(*this), std::forward<R>(r));
                }
            };

            template <typename R>
            class operation : public read_operation_base {

            public:
                operation(sender s, R&& r) : m_sender(std::move(s)), m_receiver(std::move(r)) {}
                void start() {
                    auto* c = m_sender.m_channel;
                    auto writer = c->m_pending_writers.pop();
                    if(writer) {
                        handle_value(std::move(writer->value()));
                        writer->handle_value();
                        return;
                    }
                    bool empty = true;
                    if constexpr(Buffered) {
                        std::unique_lock lock(c->m_mutex);
                        empty = c->m_queue.empty();
                    }

                    if(empty) {
                        if(c->m_capacity == 0) {
                            execution::set_error(m_receiver, channel_closed{});
                            return;
                        }
                        c->add_reader(this);
                        return;
                    }
                    if constexpr(Buffered) {

                        std::unique_lock lock(c->m_mutex);
                        auto value = std::move(c->m_queue.front());
                        c->m_queue.pop();
                        lock.unlock();
                        if(c->m_capacity != 0) {
                            c->do_write();
                        }
                        handle_value(std::move(value));
                    }
                }

            protected:
                void handle_value(T&& value) override {
                    execution::set_value(m_receiver, std::move(value));
                }
                void handle_error(std::error_code err) override {
                    if(err == std::errc::bad_file_descriptor)
                        execution::set_error(m_receiver, channel_closed{});
                    else
                        execution::set_error(m_receiver, err);
                }

            private:
                sender m_sender;
                R m_receiver;
            };

        public:
            read_channel(std::shared_ptr<channel> ptr) : ptr(std::move(ptr)) {
                this->ptr->m_readers++;
            }
            read_channel(const read_channel& other) : ptr(other.ptr) {
                ptr->m_readers++;
            }
            read_channel(read_channel&& other) = default;
            ~read_channel() {
                if(ptr) {
                    ptr->m_readers--;
                    if(ptr->m_readers == 0) {
                        ptr->close();
                    }
                }
            }
            void close() {
                if(ptr)
                    ptr->close();
            }
            auto read() {
                return sender(this->ptr.get());
            }

        private:
            std::shared_ptr<channel> ptr;
        };

        class write_channel {
            template <typename Receiver>
            class operation;
            template <typename Receiver>
            friend class operation;
            class sender {
            public:
                channel* m_channel;
                T m_value;

            public:
                sender(channel* c, T value) : m_channel(c), m_value(std::move(value)) {}
                template <template <typename...> class Variant, template <typename...> class Tuple>
                using value_types = Variant<Tuple<>>;

                template <template <typename...> class Variant>
                using error_types = Variant<>;

                static constexpr bool sends_done = true;

                template <typename Sender, execution::receiver R>
                using operation_type = write_channel::operation<R>;

                template <execution::receiver R>
                auto connect(R&& r) && {
                    return operation<R>(std::move(*this), std::forward<R>(r));
                }
            };

            template <typename R>
            class operation : public write_operation_base {
            public:
                operation(sender s, R&& r) : m_sender(std::move(s)), m_receiver(std::move(r)) {}
                void start() {
                    auto* c = m_sender.m_channel;
                    if(c->m_capacity == 0) {
                        execution::set_error(m_receiver, channel_closed{});
                        return;
                    }
                    auto reader = c->m_pending_readers.pop();
                    if(reader) {
                        reader->handle_value(std::move(m_sender.m_value));
                        handle_value();
                        return;
                    }
                    if constexpr(Buffered) {
                        std::unique_lock lock(c->m_mutex);
                        if(c->m_queue.size() < c->m_capacity) {
                            c->m_queue.push(std::move(m_sender.m_value));
                            handle_value();
                            return;
                        }
                    }
                    c->add_writer(this);
                    c->do_write();
                }


            protected:
                void handle_error(std::error_code err) override {
                    if(err == std::errc::bad_file_descriptor)
                        execution::set_error(m_receiver, channel_closed{});
                    else
                        execution::set_error(m_receiver, err);
                }
                void handle_value() override {
                    execution::set_value(m_receiver);
                }
                T& value() override {
                    return m_sender.m_value;
                }

                sender m_sender;
                R m_receiver;
            };

        public:
            write_channel(std::shared_ptr<channel> ptr) : ptr(std::move(ptr)) {
                this->ptr->m_writers++;
            }
            write_channel(const write_channel& other) : ptr(other.ptr) {
                ptr->m_writers++;
            }

            write_channel(write_channel&& other) {
                std::swap(other.ptr, ptr);
            }
            ~write_channel() {
                if(ptr) {
                    ptr->m_writers--;
                    if(ptr->m_writers == 0) {
                        ptr->close();
                    }
                }
            }
            write_channel& operator=(write_channel&& other) {
                std::swap(other.ptr, ptr);
                return this;
            }

            void close() {
                if(ptr)
                    ptr->close();
            }
            auto write(T value) {
                return sender(this->ptr.get(), std::move(value));
            }

        private:
            std::shared_ptr<channel> ptr;
        };

    private:
        friend read_channel;
        friend write_channel;

        void close() {
            if(m_capacity != 0) {
                m_capacity = 0;
                do_rw();
            }
        }
        scheduler m_scheduler;
        std::mutex m_mutex;
        linked_list<read_operation_base> m_pending_readers;
        linked_list<write_operation_base> m_pending_writers;
        [[no_unique_address]] std::conditional_t<Buffered, std::queue<T>, empty_result_t> m_queue;
        std::atomic<std::size_t> m_readers = 0;
        std::atomic<std::size_t> m_writers = 0;
        std::atomic<std::size_t> m_capacity;

    public:
        channel(scheduler sch, std::size_t capacity = std::numeric_limits<std::size_t>::max())
            : m_scheduler(std::move(sch)), m_capacity(capacity) {}

        void add_reader(read_operation_base* op) {
            m_pending_readers.push(op);
        }
        void add_writer(write_operation_base* op) {
            m_pending_writers.push(op);
        }

        void do_rw() {
            do_read();
            do_write();
        }

        void do_read() {
            bool notify = handle_unbuffered_rw();
            std::unique_lock lock(m_mutex);
            bool empty = false;
            if constexpr(Buffered) {
                while(!m_queue.empty()) {
                    auto node = m_pending_readers.pop();
                    if(!node)
                        break;
                    node->handle_value(std::move(m_queue.front()));
                    m_queue.pop();
                    notify = true;
                }
                empty = m_queue.empty();
            }
            empty = empty && !m_pending_writers.front();
            if(empty && (m_capacity == 0 || m_writers == 0)) {
                this->on_read_event_error(std::make_error_code(std::errc::bad_file_descriptor));
                return;
            }
        }

        void do_write() {
            if(m_capacity == 0) {
                this->on_write_event_error(std::make_error_code(std::errc::bad_file_descriptor));
                return;
            }
            bool notify = handle_unbuffered_rw();
            if constexpr(Buffered) {
                std::unique_lock lock(m_mutex);
                auto node = m_pending_writers.pop();
                while(node) {
                    m_queue.push(std::move(node->value()));
                    node->handle_value();
                    node = m_pending_writers.pop();
                    notify = true;
                }
            }
            if(notify)
                do_read();
        }

        bool handle_unbuffered_rw() {
            bool notify = false;
            while(true) {
                auto reader = m_pending_readers.pop();
                if(!reader) {
                    break;
                }
                auto writer = m_pending_writers.pop();
                if(!writer) {
                    m_pending_readers.push(reader);
                    break;
                }
                reader->handle_value(std::move(writer->value()));
                writer->handle_value();
                notify = true;
            }
            return notify;
        }

        void on_read_event_error(std::error_code err) {
            auto* node = m_pending_readers.pop();
            while(node) {
                node->handle_error(err);
                node = m_pending_readers.pop();
            }
        }

        void on_write_event_error(std::error_code err) {
            auto* node = m_pending_writers.pop();
            while(node) {
                node->handle_error(err);
                node = m_pending_writers.pop();
            }
        }

        struct channels {
            read_channel read() const {
                return ptr;
            }
            write_channel write() const {
                return ptr;
            }

            channels(std::shared_ptr<channel> ptr) : ptr(ptr) {}

        private:
            std::shared_ptr<channel> ptr;
        };

        template <typename T_, typename scheduler_, bool Buffered_>
        friend typename channel<scheduler_, T_, Buffered_>::channels
        make_channel(scheduler_ context);
    };  // namespace details
}  // namespace details

template <typename T, typename scheduler>
typename details::channel<scheduler, T, false>::channels make_channel(scheduler context) {
    auto c = std::shared_ptr<details::channel<scheduler, T, false>>(
        new details::channel<scheduler, T, false>(context));
    return {c};
}

template <typename T, typename scheduler>
typename details::channel<scheduler, T, true>::channels make_channel(scheduler context,
                                                                     std::size_t buffer_size) {
    auto c = std::shared_ptr<details::channel<scheduler, T, true>>(
        new details::channel<scheduler, T, true>(context, buffer_size));
    return {c};
}

}  // namespace cor3ntin::corio