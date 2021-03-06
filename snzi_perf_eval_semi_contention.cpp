/**
 * This file implements a performance evaluation for the SNZI tree on the intel core i7 2600K processor.
 */
#include <cstddef>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include <numeric>
#include <chrono>
#include <thread>
#include <atomic>
#include "snzi.hpp"
#include "affinity.hpp"
#include "profile.hpp"

// in seconds 
#define MINUTES (3)
#define DURATION (MINUTES*60)

const std::size_t num_threads[] = {1,2,3,4,5,6,7,8};
const std::size_t num_threads_count = sizeof(num_threads)/sizeof(num_threads[0]);
	
/**
 * Performs the experiment for a SNZI with parameters K,H
 */
void run_experiment_for_tree(std::size_t K, std::size_t H, std::vector<double>& all_visits);

int main(void){
	// the parameters we want to test for
	
	/**
	 * Consider for which number of threads (according to the mapping we use in threadid-core-mapping.odp 
	 * each parameter setting has meaning.
	 *
	 * K = 2 and H = 0 : for all threads
	 * K = 2 and H = 1 : we have the root in the L3 cache and the leaves in the L2 cache. So we must have at least 2 threads
	 * 		     for each L2 cache. So for 4,5,6,7,8
	 * K = 2 and H = 2 : we have the root in the L3 cache, the second level in the L2 caches and the leaves in the L1 caches
	 *		     so we must have at least 2 per L1 cache. So only for 8.
	 * K = 4 and H = 1 : We have the root in the L3 cache and the leaves in the L1 caches. So only for 8.
	 *
	 * However i run the experiments for all thread counts for convinience but the results should match the above intuition
	 */
	std::size_t K[] = {2,2,2,4};
	std::size_t H[] = {0,1,2,1};
	const std::size_t num_parameters = sizeof(K)/sizeof(K[0]);
	
	std::vector<std::vector<double> > data;
	data.resize(num_parameters);
	
	std::cout << "Starting the experiemnt" << std::endl;
	for (std::size_t i = 0; i < num_parameters; ++i){
		run_experiment_for_tree(K[i], H[i], data[i]);
	}
	std::cout << "Done" << std::endl;
	
	std::cout << "Writing data to file" << std::endl;
	
	/**
	 * In the output file we will have this format:
	 * 
	 * num_threads (K,H)=(?,?) (K,H)=(?,?) (K,H)=(?,?) ... (K,H)=(?,?)
	 * 1	visits/ms	visits/ms	visits/ms	... visits/ms
	 * 2	visits/ms	visits/ms	visits/ms	... visits/ms
	 * 3	visits/ms	visits/ms	visits/ms	... visits/ms
	 * 4	visits/ms	visits/ms	visits/ms	... visits/ms
	 * 5	visits/ms	visits/ms	visits/ms	... visits/ms
	 * 6	visits/ms	visits/ms	visits/ms	... visits/ms
	 * ...
	 * ...
	 */
	
	std::ofstream out_file;
	
	out_file.open("snzi-semi-contention.dat");
	
	out_file << "# Performance evaluation of snzi object\n";
	out_file << "# num_threads\t";
	
	for (std::size_t i = 0; i < num_parameters; ++i){
		std::size_t k_param = K[i];
		std::size_t h_param = H[i];
		
		out_file << "(K,H)=(" << k_param << "," << h_param << ")" << "\t";
	}
	out_file << "\n";
	
	// write information per thread
	for (std::size_t i = 0; i < num_threads_count; ++i){
		const std::size_t how_many_threads = num_threads[i];
		
		out_file << how_many_threads << "\t";
		
		// for each parameter configuration
		for (std::size_t j = 0; j < num_parameters; ++j){
			//double throughput = (double)data[j][i]/(double)(DURATION*1000);
			double throughput = data[j][i];

			out_file << throughput << "\t";
		}
		
		out_file << "\n";
	}
	
	out_file.close();
	
	std::cout << "OK" << std::endl;
	
	return (0);
}

void run_experiment_for_tree(std::size_t K, std::size_t H, std::vector<double>& all_visits){
	std::cout << "Running experiment for parameters (K,H) = (" << K << "," << H << ")" << std::endl;

	auto thread_job = [](concurrent::semi_contention_handling_snzi& snzi_object, int id, std::atomic<bool>& flag, unsigned long& visits){
		std::chrono::seconds duration{DURATION}; // how many seconds to run?
				
		// when to end
		std::chrono::system_clock::time_point end_time = std::chrono::system_clock::now() + duration;
				
		// wait until they tell us to start
		while (!flag.load()){}
		
		visits = 0;
		
		while (std::chrono::system_clock::now() < end_time){
			// make a visit
			snzi_object.Arrive(id);
			snzi_object.Depart(id);
			snzi_object.Query();
			++visits;
		}
	};
	
	std::atomic<bool> flag; // used to signal the threads when to start
	
	concurrent::affinity aff_setter;
	const unsigned int num_cores = std::thread::hardware_concurrency();
	
	std::cout << "num_cores = " << num_cores << std::endl;

	all_visits.resize(num_threads_count);
	
	for (std::size_t i = 0; i < num_threads_count; ++i){
		std::cout << "Clearing caches..." << std::endl;
		profile::cache_wiper cw;
		cw.clear_caches();
		std::cout << "Done." << std::endl;
					
		const std::size_t how_many_threads = num_threads[i];
		
		std::cout << "Constructing the SNZI object" << std::endl;
		concurrent::semi_contention_handling_snzi snzi_object(K,H, how_many_threads); // the snzi for this experiement
		std::cout << "Done" << std::endl;
		
		std::cout << "Running for " << how_many_threads << " threads" << std::endl;
		
		// before starting the threads we must say that they should wait
		flag = false;
		
		// keep here our threads
		std::vector<std::thread> threads;
		
		std::vector<unsigned long> visits;
		visits.resize(how_many_threads);
		
		// start the threads
		std::cout << "Starting the threads" << std::endl;
		for (std::size_t j = 0; j < how_many_threads; ++j){
			std::size_t id = j; // the id of this thread for the snzi object
			
			std::thread t = std::thread{thread_job, std::ref(snzi_object), id, std::ref(flag), std::ref(visits[j])};
			std::thread::native_handle_type t_handle = t.native_handle();
			threads.push_back(std::move(t));
			
			aff_setter(id%num_cores, t_handle);
			//aff_setter(how_many_threads, id, t_handle);
		}
		std::cout << "Done." << std::endl;
		
		// tell them to start
		flag = true;
		
		std::cout << "Waiting for threads to finish" << std::endl;
		// wait for the threads to finish
		std::for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
		std::cout << "Done." << std::endl;
		
		// retrieve how many visits we had
		double sum_average_throughput = 0.0;
		for (auto& num_visits : visits){
			sum_average_throughput += ((double)num_visits/(double)(DURATION*1000));
		}
		all_visits[i] = sum_average_throughput/(double)how_many_threads;
	}
}
