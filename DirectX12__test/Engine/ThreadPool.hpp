#pragma once

#include "../Defines.hpp"

class ThreadPool
{
public:
	explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
	~ThreadPool();

	template<typename Func, typename... Args>
	auto Enqueue(Func&& func, Args&&... args) -> std::future<typename std::invoke_result<Func, Args...>::type>
	{
		using ReturnType = typename std::invoke_result<Func, Args...>::type;
		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);

		std::future<ReturnType> res = task->get_future();
		{
			std::unique_lock<std::mutex> lock(m_QueueMutex);
			if (m_Stop)
			{
				throw std::runtime_error("Enqueue on stopped ThreadPool");
			}
			m_Tasks.emplace([task]() { (*task)(); });
		}
		m_Condition.notify_one();
		return res;
	}

	/// @brief 全てのタスク完了を待機
	void WaitAll();

	/// @brief スレッドプール停止
	void ShutDown();

private:
	std::vector<std::thread> m_Workers;
	std::queue<std::function<void()>> m_Tasks;
	std::mutex m_QueueMutex;
	std::condition_variable m_Condition;
	std::atomic<bool> m_Stop = false;
	std::atomic<size_t> m_ActiveTasks = 0;
};