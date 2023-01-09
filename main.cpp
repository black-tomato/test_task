#include "MessageQueue.h"

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
    enum ErrorCode {
        Succeeded,
        Failed
    };

    constexpr auto unhandleExceptionMsg = "Unhandled exception has been caught!\n";

    template<typename... Args>
    void Log(Args&&... args)
    {
        (std::cout << ... << args) << "\n";
    }

    template<std::mt19937::result_type MinValue, std::mt19937::result_type MaxValue>
    auto RandomInt()
    {
        static std::random_device dev;
        static std::mt19937 rng{ dev() };
        static std::uniform_int_distribution<std::mt19937::result_type> dist{ MinValue, MaxValue };
        return dist(rng);
    }

    using MessageQueue = test_task::MessageQueue<std::string>;
    struct Context
    {
        MessageQueue messagesQueue{ 2 };
        // to stop all readers/writers from main thread
        std::atomic<bool> stop{ false };
        // to synchronize writing to outstream
        std::mutex mtx;
    };

    [[nodiscard]] constexpr bool IsNonBlockingPop(std::size_t context) noexcept
    {
        // just emulate some logic
        return context % 3 == 0;
    }

    [[nodiscard]] constexpr bool IsBlockingPop(std::size_t context) noexcept
    {
        // just emulate some logic
        return context % 3 == 1;
    }

    [[nodiscard]] constexpr bool IsNonBlockingPush(std::size_t context) noexcept
    {
        // just emulate some logic
        return context % 2;
    }
}

int main()
{
    try
    {
        Context context;

        constexpr std::size_t numOfReaders{ 3 };
        std::vector<std::thread> readers;
        readers.reserve(numOfReaders);
        for (std::size_t i = 0; i < numOfReaders; ++i)
        {
            readers.emplace_back([i, &context]
            {
                try
                {
                    const auto id = "Reader " + std::to_string(i);
                    while (!context.stop.load(std::memory_order_relaxed))
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds{ RandomInt<1, 1000>() });

                        typename MessageQueue::value_type msg;
                        test_task::Result result{};

                        if (IsNonBlockingPop(i))
                            std::tie(msg, result) = context.messagesQueue.Pop<MessageQueue::OperationPolicy::NonBlocking>();
                        else if (IsBlockingPop(i))
                            std::tie(msg, result) = context.messagesQueue.Pop<MessageQueue::OperationPolicy::Blocking>();
                        else
                            std::tie(msg, result) = context.messagesQueue.Get([](const auto& val) { return val == "3"; });

                        std::scoped_lock lk{ context.mtx };
                        switch (result)
                        {
                        case test_task::Result::Ok:
                            Log(id, ": ", msg);
                            break;
                        default:
                            Log(id, ": An error occurred while popping. Code: ", static_cast<std::underlying_type_t<test_task::Result>>(result));
                            break;
                        }
                    }
                }
                catch (const std::exception& exc)
                {
                    std::cerr << i << exc.what();
                }
                catch (...)
                {
                    std::cerr << i << unhandleExceptionMsg;
                }
            });
        }

        constexpr std::size_t numOfWriters{ 5 };
        std::vector<std::thread> writers;
        writers.reserve(numOfWriters);
        for (std::size_t i = 0; i < numOfWriters; ++i)
        {
            writers.emplace_back([i, &context]
            {
                try
                {
                    const auto iStr = std::to_string(i);
                    const auto id = "Writer " + iStr;
                    while (!context.stop.load(std::memory_order_relaxed))
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds{ RandomInt<1, 1000>() });

                        test_task::Result result;
                        if (IsNonBlockingPush(i))
                            result = context.messagesQueue.Push<MessageQueue::OperationPolicy::NonBlocking>(iStr);
                        else
                            result = context.messagesQueue.Push<MessageQueue::OperationPolicy::Blocking>(id);

                        std::scoped_lock lk{ context.mtx };
                        switch (result)
                        {
                        case test_task::Result::Ok:
                            Log(id, ": push operation succeeded.");
                            break;
                        default:
                            Log(id, ": An error occurred while pushing. Code: ", static_cast<std::underlying_type_t<test_task::Result>>(result));
                            break;
                        }
                    }
                }
                catch (const std::exception& exc)
                {
                    std::cerr << i << exc.what();
                }
                catch (...)
                {
                    std::cerr << i << unhandleExceptionMsg;
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::seconds{ 5 });
        context.messagesQueue.Close();
        context.stop.store(true, std::memory_order_relaxed);

        for (auto& reader : readers)
            if (reader.joinable())
                reader.join();

        for (auto& writer : writers)
            if (writer.joinable())
                writer.join();

        Log("The program is finished successfully");
    }
    catch (const std::exception& exc)
    {
        std::cerr << exc.what();
        return Failed;
    }
    catch (...)
    {
        std::cerr << unhandleExceptionMsg;
        return Failed;
    }

    return Succeeded;
}
