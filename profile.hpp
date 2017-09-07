#ifndef PROFILE_HPP_
#define PROFILE_HPP_

#include <cassert>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <vector>
#include <thread>
#include <unistd.h>
#include <pthread.h>

namespace profile{

	// Used to wipe a cache. Code taken from tbb source code perf.cpp
	struct cache_wiper{
		static const std::uintptr_t CacheSize = 8*1024*1024; 

		void operator()(int core) const{
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(core, &cpuset);

			if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset)){
				assert(0);
			}

			volatile std::intptr_t* W = new volatile std::intptr_t[CacheSize];

			volatile std::intptr_t sink = 0;
			for (std::uintptr_t i = 0; i < CacheSize; ++i){
				sink += W[i];
			}

			delete [] W;
		}

		// This function is used to clear the caches from all cores.
		// the strategy is to run a cache_wiper on all cores
		void clear_caches(){
			// get number of hardware contexts.
			unsigned int hardware_threads = std::thread::hardware_concurrency();

			assert(hardware_threads >= 1);

			// start a thread to run a cache_wiper per hardware context
			// this is overkill since some contexts will share the same cache but it is the
			// most sure thing to do
			std::vector<std::thread> threads;

			for (unsigned int i = 0; i < hardware_threads; ++i){
				threads.push_back(std::thread{cache_wiper{}, i});
			}

			// wait for the threads to finish
			std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
		}
	};


} // namespace profile


#endif /* PROFILE_HPP_ */
