#pragma once

#include <barrier> // std::barrier
#include <cstdint> // std::uint32_t
#include <exception> // std::exception_ptr
#include <functional> // std::function
#include <thread> // std::jthread
#include <vector> // std::vector

namespace quokka
{
	class WorkerThreads
	{
	public:
		explicit WorkerThreads(std::uint32_t thread_count);
		~WorkerThreads();

		// The threads keep a pointer to it, so it can't be copied or moved
		WorkerThreads(const WorkerThreads&) = delete;
		WorkerThreads& operator=(const WorkerThreads&) = delete;
		WorkerThreads(WorkerThreads&&) = delete;
		WorkerThreads& operator=(WorkerThreads&&) = delete;

		// Runs job on every thread with its index, waits for all of them and rethrows any exception
		void run(const std::function<void(std::uint32_t)>& job);

	private:
		void thread_loop(std::uint32_t thread_index);

		// The threads and the caller of run meet before and after each job,
		// so the threads sleep between jobs instead of spinning
		std::barrier<> start_barrier_;
		std::barrier<> finish_barrier_;

		// Not atomic because the start barrier makes them visible to the threads
		const std::function<void(std::uint32_t)>* job_ { nullptr };
		bool should_stop_ { false };

		// One per thread, because an exception leaving a thread calls std::terminate
		std::vector<std::exception_ptr> exceptions_;

		// Declared last, so the threads are joined before anything they use is destroyed
		std::vector<std::jthread> threads_ {};
	};
}
