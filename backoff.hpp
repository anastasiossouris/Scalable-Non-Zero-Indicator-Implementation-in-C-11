#ifndef BACKOFF_HPP_
#define BACKOFF_HPP_

#include <cstddef>
#include <thread>

namespace concurrent{
	
	class exponential_backoff{
	public:
		void backoff(){
			if (tries <= MAX_TRIES){
				backoff(tries);
				tries *= 2;
			}
			else{	
				std::this_thread::yield();
			}
		}

		void reset(){ tries = 1; }
	private:
		static const std::size_t MAX_TRIES = 16;
		std::size_t tries{1}; 

		void backoff(std::size_t delay) const{
			for (std::size_t i = 0; i < delay; ++i){
				__asm__ __volatile__("pause;");
			}			
		}
	};

} // namespace concurrent

#endif
