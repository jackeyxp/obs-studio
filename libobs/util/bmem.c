/*
 * Copyright (c) 2013 Hugh Bailey <obs.jim@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "base.h"
#include "bmem.h"
#include "platform.h"
#include "threading.h"
#include "darray.h"

/*
 * NOTE: totally jacked the mem alignment trick from ffmpeg, credit to them:
 *   http://www.ffmpeg.org/
 */

#define ALIGNMENT 32

/* TODO: use memalign for non-windows systems */
#if defined(_WIN32)
#define ALIGNED_MALLOC 1
#else
#define ALIGNMENT_HACK 1
#endif

static void *a_malloc(size_t size)
{
#ifdef ALIGNED_MALLOC
	return _aligned_malloc(size, ALIGNMENT);
#elif ALIGNMENT_HACK
	void *ptr = NULL;
	long diff;

	ptr = malloc(size + ALIGNMENT);
	if (ptr) {
		diff = ((~(long)ptr) & (ALIGNMENT - 1)) + 1;
		ptr = (char *)ptr + diff;
		((char *)ptr)[-1] = (char)diff;
	}

	return ptr;
#else
	return malloc(size);
#endif
}

static void *a_realloc(void *ptr, size_t size)
{
#ifdef ALIGNED_MALLOC
	return _aligned_realloc(ptr, size, ALIGNMENT);
#elif ALIGNMENT_HACK
	long diff;

	if (!ptr)
		return a_malloc(size);
	diff = ((char *)ptr)[-1];
	ptr = realloc((char *)ptr - diff, size + diff);
	if (ptr)
		ptr = (char *)ptr + diff;
	return ptr;
#else
	return realloc(ptr, size);
#endif
}

static void a_free(void *ptr)
{
#ifdef ALIGNED_MALLOC
	_aligned_free(ptr);
#elif ALIGNMENT_HACK
	if (ptr)
		free((char *)ptr - ((char *)ptr)[-1]);
#else
	free(ptr);
#endif
}

static struct base_allocator alloc = {a_malloc, a_realloc, a_free};
static long num_allocs = 0;

//#define DEBUG_LEAK

#ifdef DEBUG_LEAK
static DARRAY(void *)   g_mem;
static pthread_mutex_t  g_mutex;
#endif // DEBUG_LEAK

void bmem_init()
{
#ifdef DEBUG_LEAK
	da_init(g_mem);
	g_mem.da.capacity = 20000;
	// 这里必须用malloc，默认分配会造成死循环...
	g_mem.da.array = malloc(g_mem.da.capacity * 4);
	// 由于是多线程操作，需要进行线程互斥保护...
	pthread_mutex_init(&g_mutex, NULL);
#endif // DEBUG_LEAK
}

void bmem_free()
{
#ifdef DEBUG_LEAK
	// 由于是多线程操作，需要进行线程互斥保护...
	pthread_mutex_lock(&g_mutex);
	for (size_t i = 0; i < g_mem.num; i++) {
		void *compare = darray_item(sizeof(void*), &g_mem.da, i);
		blog(LOG_ERROR, "Leak memory: 0x%08X", *(uint32_t*)compare);
	}
	// 这里只能手动清理空间...
	free(g_mem.da.array);
	g_mem.da.array = NULL;
	g_mem.da.capacity = 0;
	g_mem.da.num = 0;
	// 解锁互斥，并销毁互斥对象...
	pthread_mutex_unlock(&g_mutex);
	pthread_mutex_destroy(&g_mutex);
#endif // DEBUG_LEAK
}

void base_set_allocator(struct base_allocator *defs)
{
	memcpy(&alloc, defs, sizeof(struct base_allocator));
}

void *new_bmalloc(size_t size, char * lpszFileName, size_t nFileLine)
{
	void *ptr = alloc.malloc(size);
	if (!ptr && !size)
		ptr = alloc.malloc(1);
	if (!ptr) {
		os_breakpoint();
		bcrash("Out of memory while trying to allocate %lu bytes", (unsigned long)size);
	}

	// 记录数量，放入动态数组，捕捉泄漏...
	os_atomic_inc_long(&num_allocs);

#ifdef DEBUG_LEAK
	// 由于是多线程操作，需要进行线程互斥保护...
	pthread_mutex_lock(&g_mutex);
	da_push_back(g_mem, &ptr);
	blog(LOG_ERROR, "%s(%d): Alloc Ptr: 0x%08X, Size: %lu, Mem: %lu, OS: %lu", lpszFileName, nFileLine, ptr, size, g_mem.da.num, num_allocs);
	pthread_mutex_unlock(&g_mutex);
#endif // DEBUG_LEAK

	return ptr;
}

void *new_brealloc(void *ptr, size_t size, char * lpszFileName, size_t nFileLine)
{
	// 源地址为空 => 数量增加...
	// 源地址不为空 => 删除记录...
	if (!ptr) {
		os_atomic_inc_long(&num_allocs);
	} else {
#ifdef DEBUG_LEAK
		// 由于是多线程操作，需要进行线程互斥保护...
		pthread_mutex_lock(&g_mutex);
		da_erase_item(g_mem, &ptr);
		pthread_mutex_unlock(&g_mutex);
#endif // DEBUG_LEAK
	}
	void * pSrc = ptr;
	ptr = alloc.realloc(ptr, size);
	if (!ptr && !size)
		ptr = alloc.realloc(ptr, 1);
	if (!ptr) {
		os_breakpoint();
		bcrash("Out of memory while trying to allocate %lu bytes", (unsigned long)size);
	}
#ifdef DEBUG_LEAK
	// 由于是多线程操作，需要进行线程互斥保护...
	pthread_mutex_lock(&g_mutex);
	da_push_back(g_mem, &ptr);
	blog(LOG_ERROR, "%s(%d): ReAlloc Src-Ptr: 0x%08X, Dst-Ptr: 0x%08X, Size: %lu, Mem: %lu, OS: %lu",
		lpszFileName, nFileLine, pSrc, ptr, size, g_mem.da.num, num_allocs);
	pthread_mutex_unlock(&g_mutex);
#endif // DEBUG_LEAK
	return ptr;
}

void new_bfree(void *ptr, char * lpszFileName, size_t nFileLine)
{
	if (ptr) {
		// 记录数量，放入动态数组，捕捉泄漏...
		os_atomic_dec_long(&num_allocs);
#ifdef DEBUG_LEAK
		// 由于是多线程操作，需要进行线程互斥保护...
		pthread_mutex_lock(&g_mutex);
		da_erase_item(g_mem, &ptr);
		blog(LOG_ERROR, "%s(%d): Free Ptr: 0x%08X, Mem: %lu, OS: %lu", lpszFileName, nFileLine, ptr, g_mem.da.num, num_allocs);
		pthread_mutex_unlock(&g_mutex);
#endif // DEBUG_LEAK
	}
	alloc.free(ptr);
}

long bnum_allocs(void)
{
	return num_allocs;
}

int base_get_alignment(void)
{
	return ALIGNMENT;
}

void *bmemdup(const void *ptr, size_t size)
{
	void *out = bmalloc(size);
	if (size)
		memcpy(out, ptr, size);

	return out;
}
