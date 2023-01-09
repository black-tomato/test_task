#ifndef MESSAGE_QUEUE_H_
#define MESSAGE_QUEUE_H_

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace test_task
{
    enum class Result {
        Ok,
        Empty,
        Full,
        NotFound,
        Closed
    };

    template<typename Message>
    class MessageQueue final
    {
        MessageQueue(const MessageQueue&) = delete;
        MessageQueue(MessageQueue&&) = delete;
        MessageQueue& operator=(const MessageQueue&) = delete;
        MessageQueue& operator=(MessageQueue&&) = delete;
    public:
        using value_type = Message;

        enum class OperationPolicy {
            Blocking,
            NonBlocking
        };

        explicit MessageQueue(std::size_t queueSize)
            : m_queueSize{ queueSize }
        {
            if (queueSize == 0)
                throw std::invalid_argument{ "Invalid MessageQueue size: size should be greater than zero." };
        }

        // clients may provide either Message itself or arguments enough to construct Message instance
        template<OperationPolicy Policy, typename... Args>
        [[nodiscard]] Result Push(Args&&... messageCtorArgs)
        {
            if (IsClosed())
                return Result::Closed;

            {
                std::unique_lock lk{ m_mtx };
                if (m_queue.size() == m_queueSize)
                {
                    if constexpr (Policy == OperationPolicy::NonBlocking)
                    {
                        return Result::Full;
                    }
                    else
                    {
                        static_assert(Policy == OperationPolicy::Blocking, "Push: Unsupported OperationPolicy.");
                        // use predicate to wait on conditions (MessageQueue is closed or there is some free space to push into) and to avoid spurious wakeup
                        m_pushCv.wait(lk, [this] { return IsClosed() || m_queue.size() < m_queueSize; });

                        if (IsClosed())
                            return Result::Closed;
                    }
                }
                // add a message to the end... (FIFO) [1/2]
                m_queue.emplace_back(std::forward<Args>(messageCtorArgs)...);
            }
            // after adding a message to the queue (there is something to pop) it's time to wake up a reader(if any)
            m_popCv.notify_one();

            return Result::Ok;
        }

        template<OperationPolicy Policy>
        [[nodiscard]] std::pair<Message, Result> Pop()
        {
            if (IsClosed())
                return { {}, Result::Closed };

            Message msg;
            {
                std::unique_lock lk{ m_mtx };
                if (m_queue.empty())
                {
                    if constexpr (Policy == OperationPolicy::NonBlocking)
                    {
                        return { {}, Result::Empty };
                    }
                    else
                    {
                        static_assert(Policy == OperationPolicy::Blocking, "Pop: Unsupported OperationPolicy.");
                        // use predicate to wait on conditions (MessageQueue is closed or there is something to pop) and to avoid spurious wakeup
                        m_popCv.wait(lk, [this] { return IsClosed() || !m_queue.empty(); });

                        if (IsClosed())
                            return { {}, Result::Closed };
                    }
                }
                // ...while pop from the beginning (FIFO) [2/2]
                msg = std::move(m_queue.front());
                m_queue.pop_front();
            }
            // after extracting a message from the queue (there is some free space to push into) it's time to wake up a writer(if any)
            m_pushCv.notify_one();

            return { std::move(msg), Result::Ok };
        }

        // Returns the first message that satisfies provided Predicate
        template<typename Predicate>
        [[nodiscard]] std::pair<Message, Result> Get(Predicate&& predicate)
        {
            if (IsClosed())
                return { {}, Result::Closed };

            Message msg;
            {
                std::unique_lock lk{ m_mtx };
                if (m_queue.empty())
                    return { {}, Result::Empty };

                const auto msgIt = std::find_if(m_queue.begin(), m_queue.end(), std::forward<Predicate>(predicate));
                if (msgIt == m_queue.end())
                    return { {}, Result::NotFound };

                msg = std::move(*msgIt);
                m_queue.erase(msgIt);
            }
            // after extracting a message from the queue (there is some free space to push into) it's time to wake up a writer(if any)
            m_pushCv.notify_one();

            return { std::move(msg), Result::Ok };
        }

        // set MessageQueue state to Closed and notify all readers/writers (if any) about it (interrupt possible waiting)
        Result Close() noexcept
        {
            m_state.store(State::Closed, std::memory_order_release);
            m_popCv.notify_all();
            m_pushCv.notify_all();
            return Result::Ok;
        }

    private:
        bool IsClosed() const noexcept
        {
            return m_state.load(std::memory_order_acquire) == State::Closed;
        }

    private:
        // to protect shared resource (messages queue)
        std::mutex m_mtx;
        // to wait on condition during blocking pop (not empty condition, there is something to pop)
        std::condition_variable m_popCv;
        // to wait on condition during blocking push (not full condition, there is some free space to push into)
        std::condition_variable m_pushCv;
        // "extract from queue message that is matched client provided Predicate (any position in container)" leads to std::list is the choice
        std::list<Message> m_queue;
        // zero size queue has no sense. so the minimum size is 1.
        std::size_t m_queueSize{ 1 };

        enum class State { Running, Closed };
        // state is atomic to avoid mutex lock while state checking
        std::atomic<State> m_state{ State::Running };
    };
}

#endif // MESSAGE_QUEUE_H_
