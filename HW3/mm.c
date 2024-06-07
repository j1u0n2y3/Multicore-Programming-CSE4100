/********************************************************************/
/*																	*/
/*		Multicore Programming Prj3 - Dynamic Memory Allocator		*/
/*						20211584 Junyeong JANG						*/
/*																	*/
/********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/*** My Info ***/
team_t team = {
    /* My student ID */
    "20211584",
    /* My full name */
    "Junyeong JANG",
    /* My email address */
    "j1u0n2y3@gmail.com",
};

/*** Macros, Inline Functions ***/
// static inline int MAX(int x, int y) {
//     return ((x) > (y)) ? (x) : (y);
// }

// static inline int MIN(int x, int y) {
//     return ((x) < (y)) ? (x) : (y);
// }

/* unsigned long long type maximum value */
#define ULLONG_MAX 0x7fffffffffffffffLL * 2ULL + 1ULL;
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8 // Double word alignment
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // Size round-up for double word alignment

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)
#define SEGCLASSES 8 // Number of classes of seg list. 20 >> 13 >> 8
#define SEGCHUNKSIZE (DSIZE * SEGCLASSES)

static inline size_t PACK(size_t size, int alloc)
{
    return ((size) | (alloc & 0x1));
}

#define GET(p) (*(size_t *)(p)) // Macro (lvalue problem)

static inline void PUT(void *p, size_t val)
{
    (*(size_t *)p) = val;
}

static inline size_t GET_SIZE(void *p)
{
    return GET(p) & ~0x7;
}

static inline int GET_ALLOC(void *p)
{
    return GET(p) & 0x1;
}

static inline void *HDRP(void *bp)
{
    return ((char *)bp - WSIZE);
}

static inline void *FTRP(void *bp)
{
    return ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE);
}

static inline void *NEXT_BLKP(void *bp)
{
    return ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)));
}

static inline void *PREV_BLKP(void *bp)
{
    return ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)));
}

#define SEG_NEXT(bp) (void *)GET(bp)                   // Next block (seg)
#define SEG_PREV(bp) (void *)GET((char *)(bp) + WSIZE) // Prev block (seg)
#define GET_SEG_NEXT(bp) GET(bp)                       // GET Next block (seg)
#define GET_SEG_PREV(bp) GET((char *)(bp) + WSIZE)     // GET Prev block (seg)
                                                       // Macros (lvalue problem)
/*** Global Variables ***/
static char *heap_listp;
static char **seg_list;

/*** Function Headers ***/
int mm_init();
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
static int class_num(size_t size);
static void *best_fit(size_t asize);
static void *coalesce(void *ptr, size_t size, int condition);
static void seg_push(void *node);
static void seg_pop(void *node);
static void *extend_heap(size_t asize);
static void *extend_seg(size_t asize);
// static int mm_check();						// Heap Consistency Checker

/*** Dynamic Memory Allocator ***/
int mm_init()
{
    /* INIT SEG LIST */
    if ((seg_list = extend_seg(SEGCLASSES * WSIZE)) == (void *)-1)
        return -1;
    for (int i = 0; i < SEGCLASSES; i++)
        *(seg_list + i) = NULL;

    /* INIT HEAP */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0x0);                          // X
    PUT(heap_listp + 1 * WSIZE, PACK(DSIZE, 0x1)); // 8/1
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 0x1)); // 8/1
    PUT(heap_listp + 3 * WSIZE, PACK(0, 0x1));     // 0/1
    heap_listp += (WSIZE * 2);

    // if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    //	return -1;

    return 0;
}

void *mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    /* ALLOCATION */
    size_t asize;
    void *ptr;

    asize = ALIGN(size + DSIZE);

    if ((ptr = best_fit(asize))) // Best Fit Search
        seg_pop(ptr);
    else
    {
        if ((ptr = extend_heap(asize)) == NULL) // No left heap space >> Increment
            return -1;
    }

    /* SPLIT */
    size_t fsize, rsize;
    void *free_ptr;

    fsize = GET_SIZE(HDRP(ptr));
    rsize = fsize - asize;

    if (rsize >= 4 * WSIZE)
    {                    // | H | N | P | F | Pointers (min)
        if (asize < 100) // Optimal Threshold (100) (Trick!)
            free_ptr = &(*(ptr + asize));
        else
        {
            free_ptr = ptr;
            ptr = &(*(ptr + rsize));
        }

        PUT(HDRP(ptr), PACK(asize, 0x1));
        PUT(FTRP(ptr), PACK(asize, 0x1));

        PUT(HDRP(free_ptr), PACK(rsize, 0x0));
        PUT(FTRP(free_ptr), PACK(rsize, 0x0));
        mm_free(free_ptr);
    }
    else
    {
        PUT(HDRP(ptr), PACK(fsize, 0x1));
        PUT(FTRP(ptr), PACK(fsize, 0x1));
    }

    return ptr;
}

void mm_free(void *ptr)
{
    if (ptr == NULL)
        return;

    size_t size = GET_SIZE(HDRP(ptr));

    /* FREE */
    PUT(HDRP(ptr), PACK(size, 0x0));
    PUT(FTRP(ptr), PACK(size, 0x0));

    /* COALESCE */
    int condition;
    if (GET_ALLOC(HDRP(NEXT_BLKP(ptr))) == 1 && GET_ALLOC(HDRP(PREV_BLKP(ptr))) == 1)
        condition = 0; // Case 1: 1 1
    else if (GET_ALLOC(HDRP(NEXT_BLKP(ptr))) == 0 && GET_ALLOC(HDRP(PREV_BLKP(ptr))) == 1)
        condition = 1; // Case 2: 0 1
    else if (GET_ALLOC(HDRP(NEXT_BLKP(ptr))) == 1 && GET_ALLOC(HDRP(PREV_BLKP(ptr))) == 0)
        condition = 2; // Case 3: 1 0
    else if (GET_ALLOC(HDRP(NEXT_BLKP(ptr))) == 0 && GET_ALLOC(HDRP(PREV_BLKP(ptr))) == 0)
        condition = 3; // Case 4: 0 0
    ptr = coalesce(ptr, size, condition);

    /* SEG PUSH */
    seg_push(ptr);
}

void *mm_realloc(void *ptr, size_t size)
{
    /* 1. ZERO REALLOC == FREE BLOCK */
    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    /** REALLOCATION **/
    size_t cur_size, realloc_size;

    cur_size = GET_SIZE(HDRP(ptr));
    realloc_size = ALIGN(size + DSIZE);

    if (realloc_size <= cur_size) // Smaller reallocation >> Do nothing!
        return ptr;

    /* 2. COVER WITH PREV OR NEXT BLOCK */
    size_t new_size, merge_prev_size, merge_next_size;
    void *new_block, *bigger_block;
    int bigger_alloc;

    merge_prev_size = GET_SIZE(HDRP(PREV_BLKP(ptr))) + cur_size;
    merge_next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr))) + cur_size;
    cur_size = (cur_size < size) ? cur_size : size;

    new_size = merge_prev_size; // Prev + (1)
    new_block = PREV_BLKP(ptr);
    bigger_block = PREV_BLKP(ptr);
    bigger_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));

    if (bigger_alloc == 0 && (new_size - realloc_size) >= DSIZE)
    {
        seg_pop(bigger_block);
        memmove(new_block, ptr, cur_size);

        PUT(HDRP(new_block), PACK(new_size, 0x1)); // No split!
        PUT(FTRP(new_block), PACK(new_size, 0x1)); // for better utilization

        return new_block;
    }

    new_size = merge_next_size; // + Next (2)
    new_block = ptr;
    bigger_block = NEXT_BLKP(ptr);
    bigger_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    if (bigger_alloc == 0 && (new_size - realloc_size) >= DSIZE)
    {
        seg_pop(bigger_block);
        memmove(new_block, ptr, cur_size);

        PUT(HDRP(new_block), PACK(new_size, 0x1)); // No split!
        PUT(FTRP(new_block), PACK(new_size, 0x1)); // for better utilization

        return new_block;
    }

    /* 3. CANNOT COVER WITH PREV OR NEXT BLOCK >> NEW BLOCK MALLOC */
    if ((new_block = mm_malloc(size)) == NULL)
        return -1;
    memmove(new_block, ptr, cur_size);
    mm_free(ptr);

    return new_block;
}

static int class_num(size_t size)
{
    int classNum = 0;
    int thresh = (1 << (SEGCLASSES + 2));

    while (classNum < SEGCLASSES - 1 && size > thresh)
    {
        classNum++;
        thresh >>= 2;
    }

    return classNum;
}

static void *best_fit(size_t asize)
{
    void *best = NULL;
    size_t best_size = ULLONG_MAX;

    for (int i = class_num(asize); i < SEGCLASSES; i++)
    { // Check every possible classes
        for (void *node = seg_list[i]; node; node = SEG_NEXT(node))
        { // Check every possible nodes
            if (!GET_ALLOC(HDRP(node)) && GET_SIZE(HDRP(node)) >= asize && GET_SIZE(HDRP(node)) < best_size)
            { // Find minimal cover for asize
                best = node;
                best_size = GET_SIZE(HDRP(node));
            }
        }
    }

    return best;
}

static void *coalesce(void *ptr, size_t size, int condition)
{
    if (condition == 0) // case 1
        return ptr;

    void *front, *rear;
    void *next = NEXT_BLKP(ptr), *prev = PREV_BLKP(ptr);
    size_t merged_size, next_size = GET_SIZE(HDRP(next)), prev_size = GET_SIZE(HDRP(prev));

    if (condition == 1)
    { // case 2
        front = ptr;
        rear = next;
        seg_pop(next);
        merged_size = size + next_size;
    }
    else if (condition == 2)
    { // case 3
        front = prev;
        rear = ptr;
        seg_pop(prev);
        merged_size = prev_size + size;
    }
    else if (condition == 3)
    { // case 4
        front = prev;
        seg_pop(prev);
        rear = next;
        seg_pop(next);
        merged_size = prev_size + size + next_size;
    }
    else
        return -1;

    PUT(HDRP(front), PACK(merged_size, 0x0));
    PUT(FTRP(rear), PACK(merged_size, 0x0));

    return front;
}

static void seg_push(void *node)
{ // LIFO (Push at first of linked list)
    void *first_node = seg_list[class_num(GET_SIZE(HDRP(node)))];

    GET_SEG_PREV(node) = (size_t)NULL;
    GET_SEG_NEXT(node) = (size_t)first_node;

    if (first_node)
        GET_SEG_PREV(first_node) = (size_t)node;

    seg_list[class_num(GET_SIZE(HDRP(node)))] = node;
}

static void seg_pop(void *node)
{ // LIFO
    if (SEG_PREV(node))
        GET_SEG_NEXT(SEG_PREV(node)) = (size_t)SEG_NEXT(node);
    else
        seg_list[class_num(GET_SIZE(HDRP(node)))] = SEG_NEXT(node);

    if (SEG_NEXT(node) != NULL)
        GET_SEG_PREV(SEG_NEXT(node)) = (size_t)SEG_PREV(node);
}

static void *extend_heap(size_t asize)
{
    size_t incr;
    void *ptr;

    if (asize > CHUNKSIZE)
        asize /= WSIZE;
    else
        asize = CHUNKSIZE / WSIZE;
    incr = (asize + (asize % 2)) * WSIZE;

    if ((ptr = mem_sbrk(incr)) == (void *)-1)
        return NULL;

    PUT(HDRP(ptr), PACK(incr, 0x0));
    PUT(FTRP(ptr), PACK(incr, 0x0));
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 0x1));

    return ptr;
}

static void *extend_seg(size_t asize)
{
    size_t incr;
    void *ptr;

    if (asize > SEGCHUNKSIZE)
        asize /= WSIZE;
    else
        asize = SEGCHUNKSIZE / WSIZE;
    incr = (asize + (asize % 2)) * WSIZE;

    if ((ptr = mem_sbrk(incr)) == (void *)-1)
        return NULL;

    PUT(HDRP(ptr), PACK(incr, 0x0));
    PUT(FTRP(ptr), PACK(incr, 0x0));
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 0x1));

    return ptr;
}