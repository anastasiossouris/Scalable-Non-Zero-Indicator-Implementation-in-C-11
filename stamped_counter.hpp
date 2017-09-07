/*
 * stamped_counter.hpp
 *
 *  Created on: Dec 14, 2013
 *      Author: Tassos Souris
 */

#ifndef STAMPED_COUNTER_HPP_
#define STAMPED_COUNTER_HPP_

#include <cstdint>

namespace concurrent{

		/**
		 * A stamped_counter pairs a counter value and a stamp value together in a unified interface and most importantly
		 * into a single built-in type.
		 *
		 * In the current implementation, the stamped_counter uses a 64-bit unsigned integer and thus 32-bits are used
		 * for the representation of both the stamp and the counter values. The disadvantage of this this approach is that it limits
		 * the values of the stamp and the counter to positive (since unsigned types are used). Commonly, however, counters and stamps
		 * (e.g timestamps) are used as positive quantities and thus this restriction is not severe. Clients should be aware that a
		 * stamped_counter does not check for overflow of the packed values.
		 *
		 * stamped_counter provides access to the stamp and counter parts, through stamp_reference and counter_reference which are
		 * inner classes. Most useful arithmetic operators are overloaded for the stamp_reference and counter_reference type and thus
		 * arithmetic can be done with ease on the stamp and counter parts of a stamped_counter individually.
		 *
		 * A stamped_counter is not safe thread-safe.
		 */
		class stamped_counter{
		public:

			/**
			 * Implementation details:
			 *
			 * The stamped counter is packed into a unsigned 64 bit integer. The 32 lower-order bits represent the counter part
			 * of the stamped integer and the 32 higher-order bits represent the stamp part of the stamped integer.
			 * Graphically:
			 *
			 * 			<------------- 64-bits ---------->
			 * 			---------------------------------
			 * 			|	stamp		|	counter		|
			 * 			---------------------------------
			 * 			<----32-bits---> <----32-bits--->
			 *
			 * 	To retrieve the stamp value we use a bitwise and operation (&) with a mask to extract the 32 high-order bits and then
			 * 	shift them right by 32 bits.
			 * 	To retrieve the counter value we use a bitwise and operation (&) with a mask to extract the 32 low-order bits (no shift is
			 * 	necessary here).
			 *
			 * 	The mask for the 32 high-order bits are: 0xFFFFFFFF00000000
			 * 	The mask for the 32 low-order bits are: 0x00000000FFFFFFFF
			 */

			using value_type = std::uint64_t; /** Type of the stamped counter */
			using stamp_type = std::uint32_t; /** Type of the stamp part */
			using counter_type = std::uint32_t; /** Type of the integer part */

			friend class stamp_reference;
			friend class counter_reference;
			friend bool operator==(stamped_counter, stamped_counter);

			/**
			 * stamp_reference proxies the behavior of references to the stamp part of a stamped_counter.
			 */
			class stamp_reference{
			public:
				stamp_reference(const stamp_reference& other) : value{other.value}{}

				stamp_reference(stamp_reference&& other) : value{other.value}{}

				stamp_reference& operator=(stamp_reference&&) = delete;

				/**
				 * Assigns the given stamp value to the referenced stamp part of the stamped_counter.
				 */
				stamp_reference& operator=(stamp_type stamp){
					return value = stamped_counter::pack(stamp,
							stamped_counter::extract_counter(value)), *this;
				}
				stamp_reference& operator=(const stamp_reference& other){
					return *this = stamped_counter::extract_stamp(other.value);
				}

				counter_type get_stamp() const{ return stamped_counter::extract_stamp(value); }

				/**
				 * Returns the value of the referenced stamp.
				 */
				operator stamp_type() const{ return stamped_counter::extract_stamp(value); }

				/**
				 * Compound assignment operators.
				 */
				stamp_reference& operator+=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp += stamp, *this = tmp;
				}
				stamp_reference& operator-=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp -= stamp, *this = tmp;
				}
				stamp_reference& operator*=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp *= stamp, *this = tmp;
				}
				stamp_reference& operator/=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp /= stamp, *this = tmp;
				}
				stamp_reference& operator%=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp %= stamp, *this = tmp;
				}
				stamp_reference& operator&=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp &= stamp, *this = tmp;
				}
				stamp_reference& operator|=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp |= stamp, *this = tmp;
				}
				stamp_reference& operator^=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp ^= stamp, *this = tmp;
				}
				stamp_reference& operator<<=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp <<= stamp, *this = tmp;
				}
				stamp_reference& operator>>=(stamp_type stamp){
					stamp_type tmp = stamped_counter::extract_stamp(value);
					return tmp >>= stamp, *this = tmp;
				}

				stamp_reference& operator+=(const stamp_reference& other){ return (*this) += stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator-=(const stamp_reference& other){ return (*this) -= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator*=(const stamp_reference& other){ return (*this) *= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator/=(const stamp_reference& other){ return (*this) /= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator%=(const stamp_reference& other){ return (*this) %= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator&=(const stamp_reference& other){ return (*this) &= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator|=(const stamp_reference& other){ return (*this) |= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator^=(const stamp_reference& other){ return (*this) ^= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator<<=(const stamp_reference& other){ return (*this) <<= stamped_counter::extract_stamp(other.value); }
				stamp_reference& operator>>=(const stamp_reference& other){ return (*this) >>= stamped_counter::extract_stamp(other.value); }

				/**
				 * Prefix and postfix forms of increment and decrement operators.
				 */
				stamp_reference& operator++(){ return *this += static_cast<stamp_type>(1), *this; }
				stamp_type operator++(int){
					stamp_type old = *this;

					++(*this);

					return old;
				}
				stamp_reference& operator--(){ return *this -= static_cast<stamp_type>(1), *this; }
				stamp_type operator--(int){
					stamp_type old = *this;

					--(*this);

					return old;
				}
			private:

				friend class stamped_counter;

				/**
				 * Constructs a stamp_reference for the stamped_counter passed as parameter. This constructor is accessible
				 * only to stamped_counter.
				 */
				stamp_reference(stamped_counter& sc) : value{sc.value}{}
				stamp_reference(value_type& v) : value{v}{}

				value_type& value;
			};

			/**
			 * counter_reference proxies the behavior of references to the counter part of a stamped_counter.
			 */
			class counter_reference{
			public:
				counter_reference(const counter_reference& other) : value{other.value}{}

				// non-movable
				counter_reference(counter_reference&& other) : value{other.value}{}
				counter_reference& operator=(counter_reference&&) = delete;

				/**
				 * Assigns the given counter value to the referenced counter part of the stamped_counter.
				 */
				counter_reference& operator=(counter_type counter){
					return value = stamped_counter::pack(stamped_counter::extract_stamp(value),
							counter), *this;
				}
				counter_reference& operator=(const counter_reference& other){
					return *this = stamped_counter::extract_counter(other.value);
				}


				counter_type get_counter() const{ return stamped_counter::extract_counter(value); }

				/**
				 * Returns the value of the referenced counter.
				 */
				operator counter_type() const{ return stamped_counter::extract_counter(value); }

				/**
				 * Compound assignment operators.
				 */
				counter_reference& operator+=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp += counter, *this = tmp;
				}
				counter_reference& operator-=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp -= counter, *this = tmp;
				}
				counter_reference& operator*=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp *= counter, *this = tmp;
				}
				counter_reference& operator/=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp /= counter, *this = tmp;
				}
				counter_reference& operator%=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp %= counter, *this = tmp;
				}
				counter_reference& operator&=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp &= counter, *this = tmp;
				}
				counter_reference& operator|=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp |= counter, *this = tmp;
				}
				counter_reference& operator^=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp ^= counter, *this = tmp;
				}
				counter_reference& operator<<=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp <<= counter, *this = tmp;
				}
				counter_reference& operator>>=(counter_type counter){
					counter_type tmp = stamped_counter::extract_counter(value);
					return tmp >>= counter, *this = tmp;
				}

				counter_reference& operator+=(const counter_reference& other){ return (*this) += stamped_counter::extract_counter(other.value); }
				counter_reference& operator-=(const counter_reference& other){ return (*this) -= stamped_counter::extract_counter(other.value); }
				counter_reference& operator*=(const counter_reference& other){ return (*this) *= stamped_counter::extract_counter(other.value); }
				counter_reference& operator/=(const counter_reference& other){ return (*this) /= stamped_counter::extract_counter(other.value); }
				counter_reference& operator%=(const counter_reference& other){ return (*this) %= stamped_counter::extract_counter(other.value); }
				counter_reference& operator&=(const counter_reference& other){ return (*this) &= stamped_counter::extract_counter(other.value); }
				counter_reference& operator|=(const counter_reference& other){ return (*this) |= stamped_counter::extract_counter(other.value); }
				counter_reference& operator^=(const counter_reference& other){ return (*this) ^= stamped_counter::extract_counter(other.value); }
				counter_reference& operator<<=(const counter_reference& other){ return (*this) <<= stamped_counter::extract_counter(other.value); }
				counter_reference& operator>>=(const counter_reference& other){ return (*this) >>= stamped_counter::extract_counter(other.value); }

				/**
				 * Prefix and postfix forms of increment and decrement operators.
				 */
				counter_reference& operator++(){ return *this += static_cast<counter_type>(1), *this; }
				counter_type operator++(int){
					counter_type old = *this;

					++(*this);

					return old;
				}
				counter_reference& operator--(){ return *this -= static_cast<counter_type>(1), *this; }
				counter_type operator--(int){
					counter_type old = *this;

					--(*this);

					return old;
				}
			private:

				friend class stamped_counter;

				/**
				 * Constructs a stamp_reference for the stamped_counter passed as parameter. This constructor is accessible
				 * only to stamped_counter.
				 */
				counter_reference(stamped_counter& sc) : value{sc.value}{}
				counter_reference(value_type& v) : value{v}{}


				value_type& value;
			};

			/**
			 * Constructs a stamped_counter object initialized with a given stamp value and counter value.
			 */
			explicit stamped_counter(stamp_type stamp, counter_type counter) : value{pack(stamp, counter)} {}

			/**
			 * Constructs a stamped_counter object initialized with a given packed value val.
			 */
			explicit stamped_counter(value_type val = value_type{}) : value{val}{}

			stamped_counter(const stamped_counter&) = default;
			stamped_counter& operator=(const stamped_counter&) = default;
			stamped_counter(stamped_counter&&) = default;
			stamped_counter& operator=(stamped_counter&&) = default;
			~stamped_counter() = default;

			/**
			 * Access the stamp value.
			 */
			stamp_reference stamp(){ return stamp_reference(*this); }
			stamp_type stamp() const{ return extract_stamp(value); }

			/**
			 * Access the counter value.
			 */
			counter_reference counter(){ return counter_reference(*this); }
			counter_type counter() const{ return extract_counter(value); }

			/**
			 * Retrieve the packed value
			 */
			value_type get_value() const{ return value; }

			/**
			 * Retrieve the packed value.
			 */
			operator value_type() const{ return value; }

		private:
			value_type value;

			/**
			 * Returns the stamp part of the stamped_counter passed as parameter (val).
			 */
			static stamp_type extract_stamp(value_type val){
				return (val & 0xFFFFFFFF00000000) >> 32;
			}

			/**
			 * Returns the counter part of the stamped_counter passed as parameter (val).
			 */
			static counter_type extract_counter(value_type val){
				return (val & 0x00000000FFFFFFFF);
			}

			/**
			 * Packs the stamp and the counter passed as parameters in a single stamped_counter value.
			 */
			static value_type pack(stamp_type stamp, counter_type counter){
				return static_cast<value_type>(stamp) << 32 | counter;
			}
		};

		/**
		 * Returns true if lhs and rhs are equal; otherwise, false is returned.
		 */
		inline bool operator==(stamped_counter lhs, stamped_counter rhs){
			return lhs.value == rhs.value;
		}

		/**
		 * Returns true if lhs and rhs are not equal; otherwise, false is returned.
		 */
		inline bool operator!=(stamped_counter lhs, stamped_counter rhs){
			return !(lhs == rhs);
		}


} // namespace pspp



#endif /* STAMPED_INTEGER_HPP_ */
