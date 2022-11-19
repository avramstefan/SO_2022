/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DIE(assertion, call_description)                                       \
    do {                                                                       \
        if (assertion) {                                                       \
            fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                 \
            perror(call_description);                                          \
            exit(errno);                                                       \
        }                                                                      \
    } while (0)

#include "exec_parser.h"

static so_exec_t *exec;
static int fd;

/*
* MIN function calculator.
*/
static int min(int a, int b) {
	return (a > b) ? b : a;
}

/*
* Function that executes the actual running of the default handler.
*
* @ SIG_DFL - flag for default handler
* @ err_case - 1 for invalid access permissions in case of accesing a page
*			 - 0 for the case when the page fault occurs in an unknown segment
*/
static void so_run_default_handler(uintptr_t pf_addr, unsigned int err_case) {
	signal(SIGSEGV, SIG_DFL); // default handler

	if (!err_case)
		fprintf(stderr, "[signal SIGSEGV: segmentation violation at addr %x].\n", pf_addr);
	else
		fprintf(stderr, "[signal SIGSEGV: invalid access permissions at %x].\n", pf_addr);
	
	raise(SIGSEGV);
}

/*
* Function that represents the condition of a page fault to belong
* to a memory segment.
*/
static int so_pf_in_segment(so_seg_t *segment, uintptr_t pf_addr) {
	return (segment->vaddr + segment->mem_size >= pf_addr) &&
			(segment->vaddr <= pf_addr);
}

/*
* Function used to map a page into memory.
* There are multiple steps that are followed in the process of
* a successfully mapping:
*	1) If the segment->data was never used, then it is NULL and needs
*	  to be allocated, for being used in monitorizing the pages that
*	  have been already mapped.
*	2) If the page that contains the page fault has already been mapped,
*	   then the program is trying to acces invalid memory and it triggers
*	   the default handler.
*	3) The actual mapping process is taking place using "mmap()" and the
*	   right parameters.
*	4) Making sure that the page fault takes place after "file_size",
*	   so that the program won't access "mem_size - file_size" portion
*	   of memory, which may contain .bss. 
*	5) The previous step is used for running the instructions from the
*	   page that contains the page fault address, so that the program
*	   will continue normaly, as "Demand Paging" says.
*/
static void so_map_page_in_memory(so_seg_t *segment, uintptr_t pf_addr) {
	/* sysconf(_SC_PAGESIZE) returns memory page size */
	int pagesz = sysconf(_SC_PAGESIZE);

	/* Allocate segment->data if NULL. */
	if (!segment->data) {
		segment->data = (void *)calloc(segment->mem_size / pagesz, sizeof(char));
		DIE(segment->data == NULL, "Calloc failed!\n");
	}

	/* 
	* page_idx - the index of the highest inferior page in relation
	*	 		 to page fault address
	* page_offset - number of bytes from the starting address of the segment
	* 				to the page with index page_idx
	*/
	int page_idx = (pf_addr - segment->vaddr) / pagesz;
	int page_offset = page_idx * pagesz;

	/* Checking if the page was already mapped. */
	if (*((char *)(segment->data + page_idx)) == 1)
		so_run_default_handler(pf_addr, 1);
	
	/* The address where mapping process will take place.*/
	void *map_start_addr = (void *)(segment->vaddr + page_offset);
	
	/* 
	* Actual mapping of page
	*
	* MAP_FIXED - used to force the mapping to start from the map_start_addr
	* MAP_SHARED - changes are shared to all the existing mappings
	* MAP_ANONYMOUS - will not cause virtual address space framentation and
	*				 the mapping won't be backed by a file.
	*/
	int map_flags = MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS;
	char *map_addr = mmap(map_start_addr, pagesz, PROT_WRITE, map_flags, -1, 0);
	DIE(map_addr == MAP_FAILED, "Error while trying to map the page.\n");

	/* Setting the page as being mapped. */
	memset(segment->data + page_idx, 1, 1);

	/* 
	* Continuing the instructions from the address where the page
	* fault occured.
	*
	* The "min" function is used for not accessing the "mem_size - file_size"
	* portion of memory, as if the mapped page was the last from the segment,
	* then allocating an entire page would've create the chance of accesing
	* this "invalid" part of memory.
	*/
	if (segment->file_size > page_offset) {
		lseek(fd, segment->offset + page_offset, SEEK_SET);
	    DIE(read(fd, map_addr, min(segment->file_size - page_offset, pagesz)) == -1,
			"Error while trying to read from file.\n");
	}

	/* Giving permmisions to the mapped area, using segment->perm. */
	mprotect(map_addr, pagesz, segment->perm);
}

static void segv_handler(int signum, siginfo_t *info, void *context)
{	
	/*
	* If the signal does not represent a SIGSEGV, 
	* the program uses a default sigaction.
	*/
	if (signum != SIGSEGV) {
		struct sigaction normal_action;
		normal_action.sa_sigaction(signum, info, context);
		return;
	}

	/*
	* si_addr - siginfo_t field which denotes page fault address
	* 
	* The address is stored in "pf_addr".
	*/
	uintptr_t pf_addr = (uintptr_t)info->si_addr;
	
	/*
	* Iterating through every executable's segment, searching for
	* the one that contains the page fault address, by using this formula:
	*
	* segment->vadress <= pf_addr && segment->vaddr + segment->mem_size >= pf_addr;
	*
	* If the condition is fulfilled, then the program proceeds to map
	* the page into the segment of memory.
	*
	* If the condition is never fulfilled, then the page fault happened
	* in an unknown segment, so the program runs the default handler.
	*/
	for (int seg_idx = 0; seg_idx < exec->segments_no; seg_idx++) {
		so_seg_t *segment = exec->segments + seg_idx;
		if (so_pf_in_segment(segment, pf_addr)) {
			so_map_page_in_memory(segment, pf_addr);
			return;
		}
	}

	so_run_default_handler(pf_addr, 0);
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, NULL);
	if (rc < 0) {
		perror("sigaction");
		return -1;
	}
	return 0;
}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	fd = open(path, O_RDONLY);
	so_start_exec(exec, argv);

	return -1;
}
