#if !defined(GENERAL_H)

// @TODO investigate why we are 4 times slower without crt
#ifndef BUILD_WITHOUT_CRT
#define BUILD_WITHOUT_CRT 0
#endif 

#ifndef INTERNAL_BUILD
#define INTERNAL_BUILD 1
#endif

#define CT_ASSERT(_expr) static_assert(_expr, "Assertion " #_expr " failed")

#include <stdint.h>
#include <stdarg.h>

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float  f32;
typedef double f64;
// Bool type. When doing low-level simd stuff, things that C++ bool type does 
// can actually hurt us (for example SSE comparsion functions use masks and not bools)
// And it actually is little to do difference in using dedicated bool type and integer 
// in terms of perfomance and interfacing (besides some corner cases in bit arithmetic)
typedef u32 b32;
#define TO_BOOL(_exp) ((b32)((_exp) ? true : false))

#define ARRAY_SIZE(_a) ((size_t)(sizeof(_a) / sizeof(*(_a))))
#define SQ(_a) ((_a) * (_a))
#define ADD_MODULO(_a, _b, _modulo) (((_a) + (_b)) % _modulo)
#define INC_MODULO(_a, _modulo) ADD_MODULO(_a, 1, _modulo)

#define BYTES(_n) ((size_t)_n)
#define KILOBYTES(_n) (BYTES(_n) << 10) 
#define MEGABYTES(_n) (KILOBYTES(_n) << 10) 

// This is just to follow the style
#define SIZE_OF(_v) (sizeof(_v))
#define STRUCT_FIELD(_struct, _field) (((_struct *)(0))->_field)
#define STRUCT_OFFSET(_struct, _field) ((size_t)((u8 *)(&STRUCT_FIELD(_struct, _field))))
#define MAX_VALUE(_variable) ((1llu << (8llu * sizeof(_variable))) - 1)
#define INVALID_DEFAULT_CASE default: assert(!"Invalid switch default case");
#define INVALID_CODE_PATH assert(!"Invalid code path")
#define UNREFERENCED_VARIABLE(_var) ((void)_var)
#define NOT_IMPLEMENTED assert(!"Not implemented")

// ID stuff
// In code there are several places where ids of different kinds are used
// But they all share same functionality and could as well just be integers 
// But decision was made to make them stucts to have some advantages of strong typing
// All id structs are defined with single field named value
#define IS_SAME(_a, _b) ((_a).value == (_b).value)
#define IS_NULL(_id) ((_id).value == 0)
#define IS_NOT_NULL(_id) ((_id).value != 0)

// Linked list
// Linked list entries must have .next field
#define LLIST_ITER(_name, _list) for (auto (_name) = (_list); (_name); (_name) = (_name)->next)
#define LLIST_ADD(_list, _node) do { (_node)->next = (_list); (_list) = (_node); } while (0);
#define LLIST_POP(_list) do { (_list) = (_list)->next; } while(0);
#define LLIST_ADD_OR_CREATE(_list_ptr, _node) do { \
if (*(_list_ptr)) { \
LLIST_ADD(*(_list_ptr), (_node)); \
} else { \
*(_list_ptr) = (_node); \
} \
} while (0);
// Double-linked list entries must have .next and .prev fields
// Ciricular double-linked list
// It works by defining single statically-allocated element, which serves as sentinel
// in points to no data but is used to make function calls easier (no need for double pointers)
// However, in more complex cases we want to first element still be pointer
#define CDLIST_ITER(_name, _list) for (auto (_name) = (_list)->next; (_name) != (_list); (_name) = (_name)->next)
#define CDLIST_INIT(_list) do { (_list)->next = (_list); (_list)->prev = (_list); } while (0);
#define CDLIST_ADD(_list, _node) do { \
(_node)->next = (_list)->next; \
(_node)->prev = (_list); \
(_node)->next->prev = (_node); \
(_node)->prev->next = (_node); \
} while (0);
#define CSLIST_ADD_LAST(_list, _node) do { \
(_node)->next = (_list); \
(_node)->prev = (_list)->prev; \
(_node)->next->prev = (_node); \
(_node)->prev->next = (_node); \
} while (0);
#define CDLIST_REMOVE(_node) do {\
(_node)->prev->next = (_node)->next;\
(_node)->next->prev = (_node)->prev;\
} while (0);

// All iterators are defined with 3 functions:
// next() - used internally (but can actually be made part of api???)
// is_valid()
// advance()
// This API allows use of this macro which saves space writing countless for loops
#define ITERATE(_iter_name, _iterator) for (auto (_iter_name) = (_iterator); is_valid(&(_iter_name)); advance(&(_iter_name)) )

#if BUILD_WITHOUT_CRT 

#if INTERNAL_BUILD
#define assert(_expr) do { if (!(_expr)) { *(u32 *)0 = 0; } } while (0);
#else 
#define assert(_expr) (_expr)
#endif 

#include "cephes.h"

#include "stb_sprintf.h"
#define snprintf stbsp_snprintf
#define vsnprintf stbsp_vsnprintf

extern "C" {
    void *__cdecl memset(void *ptr, int value, size_t num);
    void *__cdecl memcpy(void *dst_p, const void *src_p, size_t num);
    int __cdecl memcmp(const void *ptr1, const void *ptr2, size_t num);
}

extern f32 F32_INFINITY;

#else 

#if INTERNAL_BUILD
#include <assert.h>
#else 
#define assert(_expr) (_expr)
#endif 

#include <math.h>
#include <string.h>
#include <stdio.h>

#define F32_INFINITY INFINITY
#endif 

size_t outf(const char *format, ...);

#define GENERAL_H 1
#endif
