**Name: Avram Cristian Stefan**\
**Group: 321CA**

# Executable Loader

## Description
>Implemented an executable loader that illustrates the concept of **demand paging**. A page is going to be loaded in the moment when the program needs it. The steps that were taken in consideration for the implementation depend on using fundamental **operating systems** concepts, such as **treating a signal** and handling **virtual memory** using *mmap()*.

>>**SIGSEGV** is a well-known signal, used for displaying an invalid memory reference. This may be useful when the generated signal occupies an address that may not be found in any known segments or it is placed inside an already mapped page, without the necessary permissions.<br /> If the signal belongs to one of the previous cases, there is the default signal handler which will be called, using the following syntax:
```
    signal(SIGSEGV, SIG_DFL);
```
>>**SIG_DFL** represents a flag for triggering the default handler.<br />

>If the page where the signal has been generated is not mapped. First, there is the **segment data**, that is used for monitoring the pages that were mapped or not. Being a generic array, the page index is given by the number of bytes from the start address of the segment data. The number of pages is calculated as:
```
    pagesz = sysconf(_SC_PAGESIZE); // returns the number of bytes in a memory page
    number_of_pages = segment->mem_size / pagesz;
```
>There is a need for knowing the index of the page where the signal has been generated. This is useful for finding the address where the mapping process will be started. In other words, the page index will help in finding the **page offset** (the number of bytes from the **segment virtual address** to the interrogated page). So, the mapping start address will be the following:
```
    page_idx = (pf_addr - segment->vaddr) / pagesz;
	page_offset = page_idx * pagesz;

    map_start_addr = (void *)(segment->vaddr + page_offset);
```
>If the page was not mapped, then there will be no errors and the mapping process is started and fulfilled using *mmap()*, which creates a new memory mapping in the calling process' virtual address space.<br />
The length for mapping is given by the page size (*pagesz*) and the flags that are used are the following: **MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS**.<br />
<br />**MAP_FIXED** forces the mapping to start from the map_start_addr. If the mapping can not take place at the specified address, then it is not going to happen at all and the *mmap()* will fail.<br />
**MAP_SHARED** is used for modifications of the contents of the mapping to be visible to other processes that share the same mapping.<br />
**MAP_ANONYMOUS** will not cause virtual address space framentation and the mapping won't be backed by a file. The *fd* flag is ignored by using **MAP_ANONYMOUS**.