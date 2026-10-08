#include "quokka/worker_threads.hpp"

#include <algorithm> // std::ranges::fill
#include <cstddef> // std::ptrdiff_t
#include <exception> // std::current_exception, std::rethrow_exception

namespace quokka
{
	WorkerThreads::WorkerThreads(std::uint32_t thread_count)
		// The threads plus the caller of run
		: start_barrier_ { static_cast<std::ptrdiff_t>(thread_count) + 1 }
		, finish_barrier_ { static_cast<std::ptrdiff_t>(thread_count) + 1 }
		// Parentheses because braces could take thread_count as an element
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
		// Releases the waiting threads so they see the flag and return, or the join would hang
		should_stop_ = true;
		start_barrier_.arrive_and_wait();
	}

	void WorkerThreads::run(const std::function<void(std::uint32_t)>& job)
	{
		job_ = &job;

		// Releases the threads, then waits until all of them finish the job
		start_barrier_.arrive_and_wait();
		finish_barrier_.arrive_and_wait();

		for (const auto& exception : exceptions_)
		{
			if (exception)
			{
				// Clears them all, so the next run doesn't rethrow one from this run
				const std::exception_ptr first_exception { exception };
				std::ranges::fill(exceptions_, nullptr);

				std::rethrow_exception(first_exception);
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
