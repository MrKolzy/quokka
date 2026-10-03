#pragma once

#include <barrier>
#include <cstdint> // std::uint32_t
#include <exception> // std::exception_ptr
#include <functional>
#include <thread> // std::jthread
#include <vector>

namespace quokka
{
	// A fixed set of threads, created once, that run the same job in parallel every time run() is called
	class WorkerThreads
	{
	public:
		explicit WorkerThreads(std::uint32_t thread_count);
		~WorkerThreads();

		// Neither copyable nor movable: the threads keep a pointer to it
		WorkerThreads(const WorkerThreads&) = delete;
		WorkerThreads& operator=(const WorkerThreads&) = delete;
		WorkerThreads(WorkerThreads&&) = delete;
		WorkerThreads& operator=(WorkerThreads&&) = delete;

		// Runs job(thread_index) on every thread at once and returns when all of them finish,
		// rethrowing the first exception any of them threw
		void run(const std::function<void(std::uint32_t)>& job);

	private:
		void thread_loop(std::uint32_t thread_index);

		// Every thread and the caller of run() meet here before each job starts and after it finishes,
		// so the threads sleep in between instead of spinning
		std::barrier<> start_barrier_;
		std::barrier<> finish_barrier_;

		// Written before the start barrier, which makes them visible to the threads once they're released
		const std::function<void(std::uint32_t)>* job_ { nullptr };
		bool should_stop_ { false };

		// One per thread, set when its job throws, since an exception leaving a thread would end the program
		std::vector<std::exception_ptr> exceptions_;

		// Declared last, so the threads are joined before anything they use is destroyed
		std::vector<std::jthread> threads_ {};
	};
}
