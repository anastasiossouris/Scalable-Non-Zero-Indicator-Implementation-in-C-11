#ifndef __AFFINITY_HPP_IS_INCLUDED__
#define __AFFINITY_HPP_IS_INCLUDED__ 1

#include <stdexcept>
#include <unistd.h>
#include <pthread.h>

namespace concurrent{

	struct affinity{		
		/**
		 * Set's the affinity of the thread with the given id to the passed core.
		 *
		 * \param core The core to which to set the affinity of the current thread
		 * \param id The identifier of the thread.
		 * \throw runtime_error If the affinity cannot be set
		 */
		void operator()(int core, pthread_t id){
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			CPU_SET(core, &cpuset);

			if (pthread_setaffinity_np(id, sizeof(cpu_set_t), &cpuset)){
				throw std::runtime_error("call to pthread_setaffinity_np() failed");
			}
		}


		void operator()(int num_threads, int id, pthread_t tid){
			switch (num_threads){
			case 1:	
				assert(id == 0);
				(*this)(0, tid);
				break;
			case 2:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(4, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 3:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(2, tid);
					break;
				case 2:
					(*this)(4, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 4:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(2, tid);
					break;
				case 2:
					(*this)(4, tid);
					break;
				case 3:
					(*this)(6, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 5:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(1, tid);
					break;
				case 2:
					(*this)(2, tid);
					break;
				case 3:
					(*this)(4, tid);
					break;
				case 4:
					(*this)(6, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 6:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(1, tid);
					break;
				case 2:
					(*this)(2, tid);
					break;
				case 3:
					(*this)(4, tid);
					break;
				case 4:
					(*this)(5, tid);
					break;
				case 5:
					(*this)(6, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 7:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(1, tid);
					break;
				case 2:
					(*this)(2, tid);
					break;
				case 3:
					(*this)(3, tid);
					break;
				case 4:
					(*this)(4, tid);
					break;
				case 5:
					(*this)(5, tid);
					break;
				case 6:
					(*this)(6, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				break;
			case 8:
				switch (id){
				case 0:
					(*this)(0, tid);
					break;
				case 1:
					(*this)(1, tid);
					break;
				case 2:
					(*this)(2, tid);
					break;
				case 3:
					(*this)(3, tid);
					break;
				case 4:
					(*this)(4, tid);
					break;
				case 5:
					(*this)(5, tid);
					break;
				case 6:
					(*this)(6, tid);
					break;
				case 7:
					(*this)(7, tid);
					break;
				default:		
					assert(0);
					break;				
				}
				 break;
			default:
				assert(0 && "invalid number of threads in affinity");
				break;
			}		
		}
	};

} // namespace concurrent

#endif
