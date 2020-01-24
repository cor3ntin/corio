#pragma once

namespace cor3ntin::corio {

struct intrusive_mpsc_queue_node {
protected:
    template <typename T>
    friend class intrusive_mpsc_queue;
    std::atomic<intrusive_mpsc_queue_node*> next = nullptr;
};

template <typename T>
class intrusive_mpsc_queue;

template <typename T>
requires std::is_base_of_v<intrusive_mpsc_queue_node, T>  //
    class intrusive_mpsc_queue<T> {
public:
    using node = intrusive_mpsc_queue_node;

    void push(T* const item) {
        push_node(item);
    }

    T* front() {
        if(!m_peeked)
            m_peeked = pop_node();
        return static_cast<T*>(m_peeked);
    }

    void pop() {
        if(m_peeked)
            m_peeked = nullptr;
        else
            pop_node();
    }

private:
    void push_node(node* const item) {
        item->next.store(nullptr, std::memory_order_relaxed);
        auto prev = std::atomic_exchange_explicit(&m_head, item, std::memory_order_acq_rel);
        prev->next.store(item, std::memory_order_release);
    }

    node* pop_node() {
        node* tail = m_tail.load(std::memory_order_relaxed);
        ;
        node* next = tail->next.load(std::memory_order_relaxed);
        if(tail == &m_stub) {
            if(!next)
                return nullptr;
            m_tail.store(next, std::memory_order_relaxed);
            tail = next;
            next = next->next.load(std::memory_order_relaxed);
        }
        if(next) {
            m_tail = next;
            return tail;
        }
        node* head = m_head.load(std::memory_order_acquire);
        if(tail != head) {
            return nullptr;
        }
        push_node(&m_stub);
        next = tail->next.load(std::memory_order_relaxed);
        if(next) {
            m_tail.store(next, std::memory_order_relaxed);
            return tail;
        }
        return nullptr;
    }


    mutable node* m_peeked = nullptr;
    node m_stub;
    std::atomic<node*> m_head = &m_stub;
    std::atomic<node*> m_tail = &m_stub;
};

}  // namespace cor3ntin::corio
