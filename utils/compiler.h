#ifndef _COMPILER_H_
#define _COMPILER_H_

#ifdef __cplusplus
#define __I  volatile	    /**< Defines 'read-only'  permissions */
#else
#define __I  volatile const /**< Defines 'read-only'  permissions */
#endif
#define   __O  volatile	      /**< Defines 'write-only' permissions */
#define   __IO volatile	      /**< Defines 'read/write' permissions */

#define _CC_PRAGMA(x) _Pragma(#x)

#if defined(__ICCARM__)
	#define WEAK __weak
	#define USED __root
	#define CONSTRUCTOR
	#define SECTION(a) _CC_PRAGMA(location = a)
	#define ALIGNED(a) _CC_PRAGMA(data_alignment = a)
#elif defined(__GNUC__)
	#define WEAK __attribute__((weak))
	#define USED __attribute__((used))
	#define CONSTRUCTOR __attribute__((constructor))
	#define SECTION(a) __attribute__((__section__(a)))
	#define ALIGNED(a) __attribute__((__aligned__(a)))
#else
	#error Unknown compiler!
#endif

/* For packing structures */
#if defined (__ICCARM__)
    /* Setup PACKing macros for EWARM Tools */
    #define PACKED_STRUCT __packed struct
#elif defined (__GNUC__)
    /* Setup PACKing macros for GCC Tools */
    #define PACKED_STRUCT struct __attribute__((packed))
#else
	#error Unknown compiler!
#endif

/* For packing union */
#if defined (__ICCARM__)
    /* Setup PACKing macros for EWARM Tools */
    #define PACKED_UNION __packed union
#elif defined (__GNUC__)
    /* Setup PACKing macros for GCC Tools */
    #define PACKED_UNION union __attribute__((packed))
#else
	#error Unknown compiler!
#endif

#if defined(__ICCARM__) || defined(__GNUC__)
	#define COMPILER_BARRIER() asm volatile ("":::"memory")
#else
	#error Unknown compiler!
#endif

#if defined(__ICCARM__)
	#include "intrinsics.h"
	#define CLZ __CLZ
#elif defined(__GNUC__)
	#define CLZ __builtin_clz
#else
	#error Unknown compiler!
#endif

#ifndef NULL
	#define NULL ((void*)0)
#endif

#define IS_POWER_OF_TWO(n) ((n) && !((n) & ((n) - 1)))

#define ROUND_UP_MULT(x,m) (((x) + ((m)-1)) & ~((m)-1))

#define ROUND_INT_DIV(n,d) (((n)==0) ? 0 : ((((n)<=0) ^ ((d)<=0)) ? (((n)-(d)/2)/(d)) : (((n)+(d)/2)/(d))))

#define ARRAY_SIZE(x) (sizeof ((x)) / sizeof(*(x)))

#define FIELD_SIZE(type, field) (sizeof(((type*)0)->field))

#define FIELD_ARRAY_SIZE(type, field) ARRAY_SIZE(((type*)0)->field)

#define _STRINGY_EXPAND(x) #x
#define STRINGIFY(x) _STRINGY_EXPAND(x)

#if defined(__GNUC__) && \
		(__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 8))
	#define SWAP(a, b) do {		\
		__auto_type _swp = (a);	\
		(a) = (b);		\
		(b) = _swp; } while (0)
#else
	/* The compiler will replace memcpy calls with direct assignations */
	#define SWAP(a, b) do {                                                \
		uint8_t _swp[sizeof(a) == sizeof(b) ? (signed)sizeof(a) : -1]; \
		memcpy(_swp, &(a), sizeof(a));                                 \
		memcpy(&(a), &(b), sizeof(a));                                 \
		memcpy(&(b), _swp, sizeof(a)); } while(0)
#endif

#define BIG_ENDIAN_TO_HOST(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) \
		| (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24))

#define BIG_ENDIAN_TO_HOST_64(x) \
		(((uint64_t)BIG_ENDIAN_TO_HOST((uint32_t)((x) & 0xffffffff)) << 32) | \
		(((uint64_t)BIG_ENDIAN_TO_HOST((uint32_t)((x) >> 32)))))

#ifdef CONFIG_RAMCODE
	#define RAMCODE SECTION(".ramcode_section")
	#define RAMDATA SECTION(".ramdata_section")
#else
	#define RAMCODE
	#define RAMDATA
#endif

#endif /* _COMPILER_H_ */
