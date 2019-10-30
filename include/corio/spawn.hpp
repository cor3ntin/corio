
#include <corio/tag_invoke.hpp>
#pragma once

namespace cor3ntin::corio {

namespace details {
    template <typename Sender, typename Receiver>
    struct spawned_op {
        struct wrapped_receiver {
            spawned_op* m_op;
            explicit wrapped_receiver(spawned_op* op) noexcept : m_op(op) {}
            template <typename... Values>
            void set_value(Values&&... values) noexcept {
                m_op->m_receiver.set_value((Values &&) values...);
                delete m_op;
            }
            template <typename Error>
            void set_error(Error&& error) noexcept {
                m_op->m_receiver.set_error((Error &&) error);
                delete m_op;
            }
            void set_done() noexcept {
                m_op->m_receiver.set_done();
                delete m_op;
            }
        };
        spawned_op(Sender&& sender, Receiver&& receiver)
            : m_receiver((Receiver &&) receiver)
            , m_inner(std::move(sender).connect(wrapped_receiver{this})) {}
        void start() & noexcept {
            m_inner.start();
        }
        Receiver m_receiver;
        typename Sender::template operation_type<Sender, wrapped_receiver> m_inner;
    };

    template <typename Sender, typename Receiver>
    void spawn(Sender&& sender, Receiver&& receiver) noexcept {
        auto* op = new spawned_op<Sender, std::remove_cvref_t<Receiver>>((Sender &&) sender,
                                                                         (Receiver &&) receiver);
        op->start();
    }
}  // namespace details

namespace execution {
    namespace __spawn_ns {
        struct __spawn_base {};
    }  // namespace __spawn_ns

    inline constexpr struct __spawn_fn : __spawn_ns::__spawn_base {
        template <typename Sender, typename Receiver>
        requires cor3ntin::corio::tag_invocable<__spawn_fn, Sender, Receiver> auto
        operator()(Sender&& s, Receiver&& r) const {
            return cor3ntin::corio::tag_invoke(*this, std::forward<Sender>(s), (Receiver &&) r);
        }

        template <typename Sender, typename Receiver>
        requires requires(Sender&& s, Receiver&& r) {
            details::spawn(std::forward<Sender>(s), std::forward<Receiver>(r));
        }
        friend void tag_invoke(__spawn_fn, Sender&& s, Receiver&& r) {
            return details::spawn(std::forward<Sender>(s), std::forward<Receiver>(r));
        }
    } spawn;

}  // namespace execution


}  // namespace cor3ntin::corio