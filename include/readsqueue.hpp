#pragma once
/**
* FILE: readsqueue.hpp
* Created: Nov 06, 2017 Mon
*/
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <atomic>

#include "common.hpp"
#include "read.hpp"

#ifdef LOCKQUEUE
#  include <queue>
#elif defined(CONCURRENTQUEUE)
#  include "concurrentqueue.h"
#endif


/**
 * Queue for Reads' records. Concurrently accessed by the Reader (producer) and the Processors (consumers)
 */
class ReadsQueue 
{
public:
	std::string id;
	size_t capacity; // max size of the queue
	std::atomic_bool is_done_push; // indicates pushers done adding items to the queue
	std::atomic_uint num_reads_tot; // total number of reads expected to be put/consumed
	std::atomic_uint num_in; // shared
	std::atomic_uint num_out; // shared
	//std::atomic_uint pushers; // counter of threads that push reads on this queue. When zero - the pushing is over.
#ifdef LOCKQUEUE
	std::queue<Read> recs; // shared: Reader & Processors, Writer & Processors

	std::mutex qlock; // lock for push/pop on queue
	std::condition_variable cvQueue;
#elif defined(CONCURRENTQUEUE)
	moodycamel::ConcurrentQueue<std::string> queue; // lockless queue
#endif

public:
	ReadsQueue(std::string id = "", std::size_t capacity = 100, std::size_t num_reads_tot = 0)
		:
		id(id),
		capacity(capacity),
		is_done_push(false),
		num_in(0),
		num_out(0),
		num_reads_tot(num_reads_tot)
#ifdef CONCURRENTQUEUE
		,
		queue(capacity) // set initial capacity
#endif
	{
		std::stringstream ss;
		ss << STAMP << "created Reads queue with capacity [" << capacity << "]" << std::endl;
		std::cout << ss.str();
	}

	~ReadsQueue() {
		std::stringstream ss;
		ss << STAMP << "Destructor called on Reads queue. Reads added: " << num_in << " Reads consumed: " << num_out << std::endl;
		std::cout << ss.str();
	}

	/** 
	 * Synchronized. Blocks until queue has capacity for more reads
	 */
	bool push(std::string& rec) 
	{
		bool res = false;
#if defined(CONCURRENTQUEUE)
		res = queue.try_enqueue(rec);
		num_in.fetch_add(1, std::memory_order_release);
#elif defined(LOCKQUEUE)
		std::unique_lock<std::mutex> lmq(qlock);
		cvQueue.wait(lmq, [this] { return recs.size() < capacity; });
		recs.push(std::move(rec));
		cvQueue.notify_one();
#endif
		return res;
	}

	// synchronized
	bool pop(std::string& rec)
	{
		bool res = false;
#if defined(CONCURRENTQUEUE)
		res = queue.try_dequeue(rec);
		if (res) ++num_out;
#elif defined(LOCKQUEUE)
		std::unique_lock<std::mutex> lmq(qlock);
		cvQueue.wait(lmq, [this] { return (pushers.load() == 0 && recs.empty()) || !recs.empty(); }); // if False - keep waiting, else - proceed.
		if (!recs.empty()) 
		{
			rec = recs.front();
			recs.pop();
			++numPopped;
			if (numPopped.load() % 100000 == 0)
			{
				std::stringstream ss;
				ss << STAMP << id << " Popped read number: " << rec.read_num << "\r";
				std::cout << ss.str();
			}
		}
		cvQueue.notify_one();
#endif
		return res;
	}
}; // ~class ReadsQueue
