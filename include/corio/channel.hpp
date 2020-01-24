#pragma once
#include <utility>
#include <queue>
#include <optional>

namespace cor3ntin::corio {

namespace details {
    template <typename T>
    struct channel_implementation_base {
    protected:
        std::mutex m_mutex;
        std::queue<T> m_queue;
        std::atomic<std::size_t> m_capacity = std::numeric_limits<std::size_t>::max();
    };

    template <typename Context, typename T, typename Channel>
    struct channel_implementation;
}  // namespace details
template <typename Ctx, typename T>
class channel : public details::channel_implementation<Ctx, T, channel<Ctx, T>> {
    using Base = details::channel_implementation<Ctx, T, channel<Ctx, T>>;
    friend Base;
    using channel_type = std::shared_ptr<channel<Ctx, T>>;
    channel(Ctx& ctx, std::size_t capacity = std::numeric_limits<std::size_t>::max())
        : Base(ctx, capacity){};
    channel(const channel&) = delete;
    class _read {
        friend channel;

    public:
        _read(const _read&) = delete;
        _read(_read&&) = default;
        [[nodiscard]] auto read() {
            return m_channel->get_read_sender();
        }

        void close() {
            if(m_channel)
                m_channel->close();
        }

        ~_read() {
            close();
        }

    private:
        _read(channel_type channel) : m_channel(channel) {}
        channel_type m_channel;
        template <typename T_, typename Ctx_>
        friend typename channel<Ctx_, T_>::channels make_channel(Ctx_& context);
    };
    class _write {
        friend channel;


    public:
        _write(const _write&) = delete;
        _write(_write&&) = default;

        [[nodiscard]] auto write(T value) {
            return m_channel->get_write_sender(std::move(value));
        }

        void close() {
            if(m_channel)
                m_channel->close();
        }

        ~_write() {
            close();
        }

    private:
        _write(channel_type channel) : m_channel(channel) {}
        channel_type m_channel;
        template <typename T_, typename Ctx_>
        friend typename channel<Ctx_, T_>::channels make_channel(Ctx_& context);
    };

public:
    struct channels {
        _read read;
        _write write;
    };

    template <typename T_, typename Ctx_>
    friend typename channel<Ctx_, T_>::channels make_channel(Ctx_& context);
};

template <typename T, typename Ctx>
typename channel<Ctx, T>::channels make_channel(Ctx& context) {
    auto c = std::shared_ptr<channel<Ctx, T>>(new channel<Ctx, T>(context));
    return {c, c};
}

struct channel_closed : std::exception {
    virtual const char* what() const noexcept {
        return "channel closed";
    }
};

}  // namespace cor3ntin::corio