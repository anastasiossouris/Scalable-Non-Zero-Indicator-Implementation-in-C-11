#ifndef SNZI_HPP_
#define SNZI_HPP_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <atomic>
#include "backoff.hpp"
#include "config.hpp"

namespace concurrent{

	/**
	 * Class snzi implements a Scalable NonZero Indicator (SNZI for short).
	 *
	 * SNZI were published in Faith Ellen, Yossi Lev, Victor Luchangco, and Mark Moir. 2007. SNZI: scalable NonZero indicators.
	 * In Proceedings of the twenty-sixth annual ACM symposium on Principles of distributed computing (PODC '07). ACM, New York, NY, USA, 13-22. DOI=10.1145/1281100.1281106 http://doi.acm.org/10.1145/1281100.1281106
	 *
	 * A SNZI object provides Arrive and Depart operations, as well as Query operations. The Query operation returns a boolean value indicating
	 * whether there is a surplus of Arrive operations (i.e whether the number of Arrive operations exceeds the number of Depart operations).
	 * In short, a thread calls Arrive to declare its presence; later, that same thread can call the Depart operation to declare that it departs. Note
	 * that a thread cannot call Depart unless it has previously called Arrive. More formally, the number of Depart operations invoked before
	 * any point in time is at most the number of Arrive operations completed before that time (Well-formedness condition). Any thread can call the
	 * Query operation at any time.
	 *
	 * Implementation details useful for clients.
	 *
	 * A SNZI object is implemented as a perfect K-ary tree of height H. The threads start their Arrive and Depart operations on a leaf node
	 * (the Query operation is always serviced from the root of the tree). It is generally beneficial to have more than 1 thread use the same leaf node because otherwise if only 1 thread uses a leaf node then it will always call operations on the
	 * parent node and thus increasing contention on it. For this reason, the height H should be chosen such that the number of leaf nodes L is lower
	 * that the number of threads T to use the SNZI object. Also, the threads with identifiers {0,1,2,...,N-1} are assigned to the leaf in linear order;
	 * that is, assume that there are 4 leaf nodes and 8 threads. Then, each leaf node will receive 2 threads as:
	 * 			+ leaf 0: threads 0 and 1
	 * 			+ leaf 1: threads 2 and 3
	 * 			+ leaf 2: threads 4 and 5
	 * 			+ leaf 3: threads 6 and 7
	 * The above doesn't apply if the number of nodes is less than the number of leaf nodes.
	 */



	class no_contention_handling_snzi{
	public:
		using size_type = std::size_t; //! For sizes and thread identifiers

	private:
		using counter_type = std::uint64_t; //! Type used for the counter at each SNZI node

		struct root_node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;

			root_node(){
				X.store(0);
			}

			void Arrive(){
				X.fetch_add(1);
			}

			void Depart(){
				X.fetch_sub(1);
			}

			bool Query() const{
				return X.load() != 0;
			}
		};

		struct node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;
			size_type parent;
			no_contention_handling_snzi* snzi_tree;

			node(){
				X.store(0);
			}

			void Arrive(){
				bool pArrInv = false;

				counter_type oldx = X.load();

				do{
					if (!oldx && !pArrInv){
						switch(parent){
						case 0:
							snzi_tree->root.Arrive();
							break;
						default:
							snzi_tree->others[parent].Arrive();
							break;
						}
						pArrInv = true;
					}
				} while (!X.compare_exchange_weak(oldx, oldx + 1));

				if (pArrInv && oldx){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}

			void Depart(){
				counter_type oldx = X.load();

				while (!X.compare_exchange_weak(oldx, oldx - 1)){}

				if (oldx == 1){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}
		};

	public:

		friend class root_node;
		friend class node;

		/**
		 * Constructs a SNZI perfect K-ary tree with height H. T specifies the maximum number of threads that will use the SNZI object.
		 *
		 * The following restrictions are applied to the parameters:
		 * 			+ K must be larger than or equal to 2.
		 * 			+ H must be larger than or equal to 0.
		 * If at least one of the above restrictions is not satisfied then an invalid_argument exception is thrown with a suitable error message string.
		 *
		 * Note that this constructor doesn't guarnatee memory visibility of the construction. This should be established by other means.
		 *
		 * \param K The arity of the SNZI tree
		 * \param H The height of the SNZI tree
		 * \param T The number of threads to use this SNZI object
		 * \throws std::invalid_argument If at least one of the restrictions is not satisfied.
		 */
		no_contention_handling_snzi(size_type K, size_type H, size_type T){
			if (K < 2){
				throw std::invalid_argument("K parameter in snzi constructor must be >= 2");
			}

			// the total number of nodes in the K-ary tree of height H.
			total_nodes = nodes_count(K,H);
			total_leaf_nodes = leaves_count(K,H);
			threads_per_leaf = static_cast<size_type>(std::ceil((double)T/(double)total_leaf_nodes));
			if (!threads_per_leaf){
				threads_per_leaf = 1;
			}
			total_threads = T;
			arity = K;

			/**
			 * We place the root node in its own (in the local variable root because it is special). That means
			 * we are left with n-1 other nodes to allocate in local variable others.
			 * But, because the children of the root node which has index 0 are 1,2,... we do not use the 0 index of the
			 * others array and, thus, instead of n-1 nodes we allocate n nodes in the others array.
			 * This also allow us to index the leaf nodes in the others array with their normal indices.
			 */
			others.reset(new node[total_nodes]);

			// We must set the snzi pointer in the other nodes so that they can navigate in the tree (to find their parents)
			// and tell them which is their id
			for (size_type i = 1; i < total_nodes; ++i){
				others[i].snzi_tree = this;
				others[i].parent = parent(i);
			}
		}

		/**
		 * Called by a thread with identifier tid, which should be in the range [0,T), to declare its presence.
		 *
		 * An Arrive() operation by a thread should be matched by a Depart() operation.
		 */
		void Arrive(size_type tid){
			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Arrive();
				break;
			default:
				others[leaf].Arrive();
				break;
			}
		}

		/**
		 * Called by a thread with identifier id, which should be in the range [0,T), after it has called Arrive() to declare
		 * that is "departs".
		 */
		void Depart(size_type tid){
			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Depart();
				break;
			default:
				others[leaf].Depart();
				break;
			}
		}

		/**
		 * Tests whether there is an "active" thread that has arrived in the SNZI tree.
		 *
		 * \return True is there is a surplus of Arrive operations from Depart operations.
		 */
		bool Query() const{
			return root.Query();
		}

	private:
		size_type arity; //! The arity of the SNZI tree
		size_type total_nodes; //! Total number on nodes in the SNZI tree
		size_type total_leaf_nodes; //! Number of leaf nodes in the SNZI tree
		size_type threads_per_leaf; //! The range of threads allocated to each leaf node
		size_type total_threads; //! Number of threads to use this SNZI object
		root_node root; //! The root SNZI object of the tree
		std::unique_ptr<node[]> others{nullptr}; //! The other SNZI objects of the tree

		/**
		 * Returns the index of the leaf node in the others array where the thread with the given id is assigned (for Arrive and Depart operations).
		 *
		 * \param tid The id of the thread calling the Arrive or Depart operations.
		 * \return The index of the leaf where that thread should call its Arrive and Depart operations.
		 */
		size_type get_leaf_for_thread(size_type tid) const{
			/**
			 * Thread with this id will use the id/r-th leaf node.
			 * If we have n total nodes and l leaf nodes then the leaf nodes start at index
			 * l_offset=n-l in the others array, and thus the thread with this id will be assigned
			 * to leaf node l_offset + i/r in the other array. To avoid cases where i/r exceeds the possible range
			 * of leaf nodes we use i/r mod l.
			 */
			return total_nodes - total_leaf_nodes + ((tid/threads_per_leaf)%total_leaf_nodes);
		}

		/**
		 * Returns the index of the parent of the node with index id.
		 *
		 * \param id The index of the node
		 * \return The index of id's parent
		 */
		size_type parent(size_type id) const{
			return (id - 1)/arity;
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type nodes_count(size_type K, size_type H){
			assert(K != 1);
			return (pow_int(K,H+1) - 1)/(K-1);
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type leaves_count(size_type K, size_type H){
			return pow_int(K,H);
		}

		/**
		 * \return b raised to the power of e.
		 */
		static size_type pow_int(size_type b, size_type e){
			size_type result = 1;
			for (size_type i = 1; i <= e; ++i){
				result *= b;
			}
			return result;
		}
	};

	class semi_contention_handling_snzi{
	public:
		using size_type = std::size_t; //! For sizes and thread identifiers

	private:
		using counter_type = std::uint64_t; //! Type used for the counter at each SNZI node

		struct root_node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;

			root_node(){
				X.store(0);
			}

			void Arrive(){
				X.fetch_add(1);
			}

			void Depart(){
				X.fetch_sub(1);
			}

			bool Query() const{
				return X.load() != 0;
			}
		};

		struct node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;
			alignas(CACHE_LINE_SIZE) std::atomic<bool> announce;
			size_type parent;
			semi_contention_handling_snzi* snzi_tree;

			node(){
				X.store(0);
				announce.store(false);
			}

			void Arrive(){
				bool pArrInv = false;

				counter_type oldx = X.load();

				do{
					if (!oldx && !pArrInv){
						bool doArrive = true;
						if (announce.load()){
							exponential_backoff backoff;
							const int DelayAmount = 16;
							for (int i = 0; i < DelayAmount; ++i){
								oldx = X.load();
								if (oldx){ doArrive = false; break; }
								backoff.backoff();
							}
						}
						if (doArrive){
							announce.store(true);
							switch(parent){
							case 0:
								snzi_tree->root.Arrive();
								break;
							default:
								snzi_tree->others[parent].Arrive();
								break;
							}
							pArrInv = true;
						}
					}
				} while (!X.compare_exchange_weak(oldx, oldx + 1));

				if (pArrInv && oldx){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}

			void Depart(){
				counter_type oldx = X.load();

				do{
					if (oldx == 1){
						announce.store(false);
					}
					// use a strong version here to avoid the possibility of a spurious failure while oldx == 1
					// that would lead to two stores to announce
				} while (!X.compare_exchange_strong(oldx, oldx - 1));

				if (oldx == 1){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}
		};

	public:

		friend class root_node;
		friend class node;

		/**
		 * Constructs a SNZI perfect K-ary tree with height H. T specifies the maximum number of threads that will use the SNZI object.
		 *
		 * The following restrictions are applied to the parameters:
		 * 			+ K must be larger than or equal to 2.
		 * 			+ H must be larger than or equal to 0.
		 * If at least one of the above restrictions is not satisfied then an invalid_argument exception is thrown with a suitable error message string.
		 *
		 * Note that this constructor doesn't guarnatee memory visibility of the construction. This should be established by other means.
		 *
		 * \param K The arity of the SNZI tree
		 * \param H The height of the SNZI tree
		 * \param T The number of threads to use this SNZI object
		 * \throws std::invalid_argument If at least one of the restrictions is not satisfied.
		 */
		semi_contention_handling_snzi(size_type K, size_type H, size_type T){
			if (K < 2){
				throw std::invalid_argument("K parameter in snzi constructor must be >= 2");
			}

			// the total number of nodes in the K-ary tree of height H.
			total_nodes = nodes_count(K,H);
			total_leaf_nodes = leaves_count(K,H);
			threads_per_leaf = static_cast<size_type>(std::ceil((double)T/(double)total_leaf_nodes));
			if (!threads_per_leaf){
				threads_per_leaf = 1;
			}
			total_threads = T;
			arity = K;

			/**
			 * We place the root node in its own (in the local variable root because it is special). That means
			 * we are left with n-1 other nodes to allocate in local variable others.
			 * But, because the children of the root node which has index 0 are 1,2,... we do not use the 0 index of the
			 * others array and, thus, instead of n-1 nodes we allocate n nodes in the others array.
			 * This also allow us to index the leaf nodes in the others array with their normal indices.
			 */
			others.reset(new node[total_nodes]);

			// We must set the snzi pointer in the other nodes so that they can navigate in the tree (to find their parents)
			// and tell them which is their id
			for (size_type i = 1; i < total_nodes; ++i){
				others[i].snzi_tree = this;
				others[i].parent = parent(i);
			}
		}

		/**
		 * Called by a thread with identifier tid, which should be in the range [0,T), to declare its presence.
		 *
		 * An Arrive() operation by a thread should be matched by a Depart() operation.
		 */
		void Arrive(size_type tid){
			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Arrive();
				break;
			default:
				others[leaf].Arrive();
				break;
			}
		}

		/**
		 * Called by a thread with identifier id, which should be in the range [0,T), after it has called Arrive() to declare
		 * that is "departs".
		 */
		void Depart(size_type tid){
			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Depart();
				break;
			default:
				others[leaf].Depart();
				break;
			}
		}

		/**
		 * Tests whether there is an "active" thread that has arrived in the SNZI tree.
		 *
		 * \return True is there is a surplus of Arrive operations from Depart operations.
		 */
		bool Query() const{
			return root.Query();
		}

	private:
		size_type arity; //! The arity of the SNZI tree
		size_type total_nodes; //! Total number on nodes in the SNZI tree
		size_type total_leaf_nodes; //! Number of leaf nodes in the SNZI tree
		size_type threads_per_leaf; //! The range of threads allocated to each leaf node
		size_type total_threads; //! Number of threads to use this SNZI object
		root_node root; //! The root SNZI object of the tree
		std::unique_ptr<node[]> others{nullptr}; //! The other SNZI objects of the tree

		/**
		 * Returns the index of the leaf node in the others array where the thread with the given id is assigned (for Arrive and Depart operations).
		 *
		 * \param tid The id of the thread calling the Arrive or Depart operations.
		 * \return The index of the leaf where that thread should call its Arrive and Depart operations.
		 */
		size_type get_leaf_for_thread(size_type tid) const{
			/**
			 * Thread with this id will use the id/r-th leaf node.
			 * If we have n total nodes and l leaf nodes then the leaf nodes start at index
			 * l_offset=n-l in the others array, and thus the thread with this id will be assigned
			 * to leaf node l_offset + i/r in the other array. To avoid cases where i/r exceeds the possible range
			 * of leaf nodes we use i/r mod l.
			 */
			return total_nodes - total_leaf_nodes + ((tid/threads_per_leaf)%total_leaf_nodes);
		}

		/**
		 * Returns the index of the parent of the node with index id.
		 *
		 * \param id The index of the node
		 * \return The index of id's parent
		 */
		size_type parent(size_type id) const{
			return (id - 1)/arity;
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type nodes_count(size_type K, size_type H){
			assert(K != 1);
			return (pow_int(K,H+1) - 1)/(K-1);
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type leaves_count(size_type K, size_type H){
			return pow_int(K,H);
		}

		/**
		 * \return b raised to the power of e.
		 */
		static size_type pow_int(size_type b, size_type e){
			size_type result = 1;
			for (size_type i = 1; i <= e; ++i){
				result *= b;
			}
			return result;
		}
	};

	class full_contention_handling_snzi{
	public:
		using size_type = std::size_t; //! For sizes and thread identifiers

		struct contention_status{
			static const int MaxContentionNumFailures = 5;

			bool use_snzi_in_arrive{false};
			bool use_snzi_in_depart{false};
			bool use_snzi_tree_flag{false};
		};

	private:
		using counter_type = std::uint64_t; //! Type used for the counter at each SNZI node

		struct root_node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;

			root_node(){
				X.store(0);
			}

			void ArriveDirectly(contention_status& cont){
				counter_type oldx = X.load();

				exponential_backoff backoff;
				int num_failures{0};

				while (!X.compare_exchange_weak(oldx, oldx + 1)){
					++num_failures;
					backoff.backoff();
				}

				if (num_failures >= contention_status::MaxContentionNumFailures){
					// the next time use the snzi tree
					cont.use_snzi_tree_flag = true;
				}
			}

			void DepartDirectly(contention_status& cont){
				Depart();

				if (cont.use_snzi_tree_flag){
					cont.use_snzi_in_arrive = cont.use_snzi_in_depart = true;
				}
			}

			void Arrive(){
				X.fetch_add(1);
			}

			void Depart(){
				X.fetch_sub(1);
			}

			bool Query() const{
				return X.load() != 0;
			}
		};

		struct node{
			// to avoid false sharing with other snzi nodes
			alignas(CACHE_LINE_SIZE) std::atomic<counter_type> X;
			alignas(CACHE_LINE_SIZE) std::atomic<bool> announce;
			size_type parent;
			full_contention_handling_snzi* snzi_tree;

			node(){
				X.store(0);
				announce.store(false);
			}

			void Arrive(){
				bool pArrInv = false;

				counter_type oldx = X.load();

				do{
					if (!oldx && !pArrInv){
						bool doArrive = true;
						if (announce.load()){
							exponential_backoff backoff;
							const int DelayAmount = 16;
							for (int i = 0; i < DelayAmount; ++i){
								oldx = X.load();
								if (oldx){ doArrive = false; break; }
								backoff.backoff();
							}
						}
						if (doArrive){
							announce.store(true);
							switch(parent){
							case 0:
								snzi_tree->root.Arrive();
								break;
							default:
								snzi_tree->others[parent].Arrive();
								break;
							}
							pArrInv = true;
						}
					}
				} while (!X.compare_exchange_weak(oldx, oldx + 1));

				if (pArrInv && oldx){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}

			void Depart(){
				counter_type oldx = X.load();

				do{
					if (oldx == 1){
						announce.store(false);
					}
					// use a strong version here to avoid the possibility of a spurious failure while oldx == 1
					// that would lead to two stores to announce
				} while (!X.compare_exchange_strong(oldx, oldx - 1));

				if (oldx == 1){
					switch(parent){
					case 0:
						snzi_tree->root.Depart();
						break;
					default:
						snzi_tree->others[parent].Depart();
						break;
					}
				}
			}
		};

	public:

		friend class root_node;
		friend class node;

		/**
		 * Constructs a SNZI perfect K-ary tree with height H. T specifies the maximum number of threads that will use the SNZI object.
		 *
		 * The following restrictions are applied to the parameters:
		 * 			+ K must be larger than or equal to 2.
		 * 			+ H must be larger than or equal to 0.
		 * If at least one of the above restrictions is not satisfied then an invalid_argument exception is thrown with a suitable error message string.
		 *
		 * Note that this constructor doesn't guarnatee memory visibility of the construction. This should be established by other means.
		 *
		 * \param K The arity of the SNZI tree
		 * \param H The height of the SNZI tree
		 * \param T The number of threads to use this SNZI object
		 * \throws std::invalid_argument If at least one of the restrictions is not satisfied.
		 */
		full_contention_handling_snzi(size_type K, size_type H, size_type T){
			if (K < 2){
				throw std::invalid_argument("K parameter in snzi constructor must be >= 2");
			}

			// the total number of nodes in the K-ary tree of height H.
			total_nodes = nodes_count(K,H);
			total_leaf_nodes = leaves_count(K,H);
			threads_per_leaf = static_cast<size_type>(std::ceil((double)T/(double)total_leaf_nodes));
			if (!threads_per_leaf){
				threads_per_leaf = 1;
			}
			total_threads = T;
			arity = K;

			/**
			 * We place the root node in its own (in the local variable root because it is special). That means
			 * we are left with n-1 other nodes to allocate in local variable others.
			 * But, because the children of the root node which has index 0 are 1,2,... we do not use the 0 index of the
			 * others array and, thus, instead of n-1 nodes we allocate n nodes in the others array.
			 * This also allow us to index the leaf nodes in the others array with their normal indices.
			 */
			others.reset(new node[total_nodes]);

			// We must set the snzi pointer in the other nodes so that they can navigate in the tree (to find their parents)
			// and tell them which is their id
			for (size_type i = 1; i < total_nodes; ++i){
				others[i].snzi_tree = this;
				others[i].parent = parent(i);
			}
		}

		/**
		 * Called by a thread with identifier tid, which should be in the range [0,T), to declare its presence.
		 *
		 * An Arrive() operation by a thread should be matched by a Depart() operation.
		 */
		void Arrive(size_type tid, contention_status& cont){
			if (!cont.use_snzi_in_arrive){
				root.ArriveDirectly(cont);
				return ;
			}

			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Arrive();
				break;
			default:
				others[leaf].Arrive();
				break;
			}
		}

		/**
		 * Called by a thread with identifier id, which should be in the range [0,T), after it has called Arrive() to declare
		 * that is "departs".
		 */
		void Depart(size_type tid, contention_status& cont){
			if (!cont.use_snzi_in_depart){
				root.DepartDirectly(cont);
				return ;
			}

			size_type leaf = get_leaf_for_thread(tid);

			switch(leaf){
			case 0:
				root.Depart();
				break;
			default:
				others[leaf].Depart();
				break;
			}
		}

		/**
		 * Tests whether there is an "active" thread that has arrived in the SNZI tree.
		 *
		 * \return True is there is a surplus of Arrive operations from Depart operations.
		 */
		bool Query() const{
			return root.Query();
		}

	private:
		size_type arity; //! The arity of the SNZI tree
		size_type total_nodes; //! Total number on nodes in the SNZI tree
		size_type total_leaf_nodes; //! Number of leaf nodes in the SNZI tree
		size_type threads_per_leaf; //! The range of threads allocated to each leaf node
		size_type total_threads; //! Number of threads to use this SNZI object
		root_node root; //! The root SNZI object of the tree
		std::unique_ptr<node[]> others{nullptr}; //! The other SNZI objects of the tree

		/**
		 * Returns the index of the leaf node in the others array where the thread with the given id is assigned (for Arrive and Depart operations).
		 *
		 * \param tid The id of the thread calling the Arrive or Depart operations.
		 * \return The index of the leaf where that thread should call its Arrive and Depart operations.
		 */
		size_type get_leaf_for_thread(size_type tid) const{
			/**
			 * Thread with this id will use the id/r-th leaf node.
			 * If we have n total nodes and l leaf nodes then the leaf nodes start at index
			 * l_offset=n-l in the others array, and thus the thread with this id will be assigned
			 * to leaf node l_offset + i/r in the other array. To avoid cases where i/r exceeds the possible range
			 * of leaf nodes we use i/r mod l.
			 */
			return total_nodes - total_leaf_nodes + ((tid/threads_per_leaf)%total_leaf_nodes);
		}

		/**
		 * Returns the index of the parent of the node with index id.
		 *
		 * \param id The index of the node
		 * \return The index of id's parent
		 */
		size_type parent(size_type id) const{
			return (id - 1)/arity;
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type nodes_count(size_type K, size_type H){
			assert(K != 1);
			return (pow_int(K,H+1) - 1)/(K-1);
		}

		/**
		 * \return The number of nodes in a perfect K-ary tree of height H.
		 */
		static size_type leaves_count(size_type K, size_type H){
			return pow_int(K,H);
		}

		/**
		 * \return b raised to the power of e.
		 */
		static size_type pow_int(size_type b, size_type e){
			size_type result = 1;
			for (size_type i = 1; i <= e; ++i){
				result *= b;
			}
			return result;
		}
	};

} // namespace concurrent


#endif /* SNZI_HPP_ */
