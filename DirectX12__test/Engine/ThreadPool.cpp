#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t numThreads)
{
	for (size_t i = 0; i < numThreads; ++i)
	{
		m_Workers.emplace_back([this]()
			{
				for (;;)
				{
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(m_QueueMutex);
						m_Condition.wait(lock, [this]() { return m_Stop || !m_Tasks.empty(); });

						if (m_Stop && m_Tasks.empty())
						{
							return;
						}

						task = std::move(m_Tasks.front());
						m_Tasks.pop();
						++m_ActiveTasks;
					}

					task();

					{
						std::unique_lock<std::mutex> lock(m_QueueMutex);
						--m_ActiveTasks;
						if (m_Tasks.empty() && m_ActiveTasks == 0)
						{
							m_Condition.notify_all();
						}
					}
				}
			});
	}
}

ThreadPool::~ThreadPool()
{
	ShutDown();
}

void ThreadPool::WaitAll()
{
	std::unique_lock<std::mutex> lock(m_QueueMutex);
	m_Condition.wait(lock, [this]() { return m_Tasks.empty() && m_ActiveTasks == 0; });
}

void ThreadPool::ShutDown()
{
	{
		std::unique_lock<std::mutex> lock(m_QueueMutex);
		if (m_Stop)
		{
			return;
		}
		m_Stop = true;
	}
	m_Condition.notify_all();

	for (std::thread& worker : m_Workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}
}