#include "quokka/worker_threads.hpp"

#include <cstddef> // std::ptrdiff_t
#include <utility> // std::exchange

namespace quokka
{
	WorkerThreads::WorkerThreads(std::uint32_t thread_count)
		// The threads plus the caller of run()
		: start_barrier_ { static_cast<std::ptrdiff_t>(thread_count) + 1 }
		, finish_barrier_ { static_cast<std::ptrdiff_t>(thread_count) + 1 }
		// Parentheses, not braces: this is the count constructor
		, exceptions_(thread_count)
	{
		threads_.reserve(thread_count);

		for (std::uint32_t thread_index { 0 }; thread_index < thread_count; ++thread_index)
		{
			threads_.emplace_back(&WorkerThreads::thread_loop, this, thread_index);
		}
	}

	WorkerThreads::~WorkerThreads()
	{
		// Releases the threads waiting for a job, which see the flag and return, so the jthreads can join them
		should_stop_ = true;
		start_barrier_.arrive_and_wait();
	}

	void WorkerThreads::run(const std::function<void(std::uint32_t)>& job)
	{
		job_ = &job;

		// Releases the threads, and then waits until every one of them has finished the job
		start_barrier_.arrive_and_wait();
		finish_barrier_.arrive_and_wait();

		for (auto& exception : exceptions_)
		{
			if (exception)
			{
				// Cleared as it's rethrown, so the next run() doesn't throw it again
				std::rethrow_exception(std::exchange(exception, nullptr));
			}
		}
	}

	void WorkerThreads::thread_loop(std::uint32_t thread_index)
	{
		while (true)
		{
			start_barrier_.arrive_and_wait();

			if (should_stop_)
			{
				return;
			}

			try
			{
				(*job_)(thread_index);
			}
			catch (...)
			{
				exceptions_[thread_index] = std::current_exception();
			}

			finish_barrier_.arrive_and_wait();
		}
	}
}
