/* userfaultfd_demo.c

   Licensed under the GNU General Public License version 2 or later.
*/
#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE);	\
	} while (0)

static int page_size;

static void *
fault_handler_thread(void *arg)
{
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	static int fault_cnt = 0;     /* Number of faults so far handled */
	long uffd;                    /* userfaultfd file descriptor */
	static char *page = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long) arg;

	/* [H1: point 1]
	 * Creates a new mapping in the virtual address space of the calling process. Since address is NULL
	 * kernel chooses page-aligned address to create the mapping.
	 */
	if (page == NULL) {
		page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			errExit("mmap");
	}

	/* [H2: point 1]
	 * Loop for handling events from the userfaultfd file descriptor.
	 */
	for (;;) {

		/* See what poll() tells us about the userfaultfd */

		struct pollfd pollfd;
		int nready;

		/* [H3: point 1]
		 * Get the data from the poll() function along with its status.
		 */
		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			errExit("poll");

		printf("\nfault_handler_thread():\n");
		printf("    poll() returns: nready = %d; "
                       "POLLIN = %d; POLLERR = %d\n", nready,
                       (pollfd.revents & POLLIN) != 0,
                       (pollfd.revents & POLLERR) != 0);

		/* [H4: point 1]
		 * Read the user specified argument and exit when reading is done or 
		 * conditions for end of file argument are met..
		 */
		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			errExit("read");

		/* [H5: point 1]
		 * Handle conditions when unexpected events occur on userfaultfd.
		 */
		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		/* [H6: point 1]
		 * Print the address and flags associated with the pagefault event.
		 */
		printf("    UFFD_EVENT_PAGEFAULT event: ");
		printf("flags = %llx; ", msg.arg.pagefault.flags);
		printf("address = %llx\n", msg.arg.pagefault.address);

		/* [H7: point 1]
		 * Copy the page into the faulting region and varying the contents copied in, so that each page fault is handled separately. 
		 */
		memset(page, 'A' + fault_cnt % 20, page_size);
		fault_cnt++;

		/* [H8: point 1]
		 * Handle page faults in unit of pages.
		 */
		uffdio_copy.src = (unsigned long) page;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
			~(page_size - 1);
		uffdio_copy.len = page_size;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;

		/* [H9: point 1]
		 * Round faulting address down to page boundary.
		 */
		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			errExit("ioctl-UFFDIO_COPY");

		/* [H10: point 1]
		 * Printing the copy returned from the page fault unit along with its length.
		 */
		printf("        (uffdio_copy.copy returned %lld)\n",
                       uffdio_copy.copy);
	}
}

int
main(int argc, char *argv[])
{
	long uffd;          /* userfaultfd file descriptor */
	char *addr;         /* Start of region handled by userfaultfd */
	unsigned long len;  /* Length of region handled by userfaultfd */
	pthread_t thr;      /* ID of thread that handles page faults */
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	int s;
	int l;

	/* [M1: point 1]
	 * Check the arguments passed to the userfaultfd program
	 * contains number of pages whose page faults will be handled. 
	 */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s num-pages\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* [M2: point 1]
	 * Calculate the length of the region to be handled by userfaultfd.
	 */
	page_size = sysconf(_SC_PAGE_SIZE);
	len = strtoul(argv[1], NULL, 0) * page_size;

	/* [M3: point 1]
	 * Create userfaultfd object.
	 */
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1)
		errExit("userfaultfd");

	/* [M4: point 1]
	 * Enable the userfaultfd object.
	 */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		errExit("ioctl-UFFDIO_API");

	/* [M5: point 1]
	 * Create a private anonymous mapping. The memory will not be allocated by default,
	 * but will be allocated when we touch it via the userfaultfd. 
	 */
	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		errExit("mmap");

	printf("Address returned by mmap() = %p\n", addr);

	/* [M6: point 1]
	 * Register the memory range of the mapping we created for handling the userfaultfd object.
	 *In mode, we request to track missing pages.
	 */
	uffdio_register.range.start = (unsigned long) addr;
	uffdio_register.range.len = len;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		errExit("ioctl-UFFDIO_REGISTER");

	/* [M7: point 1]
	 * Create a thread that will process userfaultfd events.
	 */
	s = pthread_create(&thr, NULL, fault_handler_thread, (void *) uffd);
	if (s != 0) {
		errno = s;
		errExit("pthread_create");
	}

	/*<F4>
	 * [U1]: point 5
	 * Print the address and the character at that address. The address is associated with the start of the region
	 * registered with the userfaultfd, while the length of the region is greater than the variable l.
	 * Since, the page has not been accessed yet, accessing the first byte of the page creates a page fault.
	 * When this happens, the page fault handling thread sets the memory of the region handled by userfaultfd
	 * with the character A.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#1. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U2]: point 5
	 * Here, the variable l is reset and the character values in the region are printed again.
	 * Since, the values at this page have already been accessed once, a page fault does not occur and
	 * the values associated with the addresses in the region handled by the userfaultfd is A.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#2. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U3]: point 5
	 * The kernel takes advice about the use of memory handled by the userfaultfd object. Since the MADV_DONTNEED
	 * parameter is specified, the memory does not expect access in the near future. When an attempt to read the 
	 * memory is made, a page fault occurs again and the values in the memory region are changed by adding 1 to the character A,
	 * to represent a different page fault. This results in printing the address values with increments and the character B
	 * for the region handled by userfaultfd.
	 */
	printf("-----------------------------------------------------\n");
	if (madvise(addr, len, MADV_DONTNEED)) {
		errExit("fail to madvise");
	}
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#3. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U4]: point 5
	 * Here, the memory region for the userfaultfd is read again by reseting the variable l.
	 * Since, the page has been already accessed, while reading the page again, a page fault does not occur
	 * and the same character values with their respective addresses are printed.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#4. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U5]: point 5
	 * The kernel memory allocated for handling of userfaultfd does not expect access in the near future because 
	 * of using MADV_DONTNEED as parameter in madavise system call. Since, we access the region to write in it, a 
	 * page fault occurs because of access for the first time.The character @ is written to the memory region handled
	 * by the userfaultfd in steps by incrementing the variable l and the written value is printed out along with the 
	 * memory addresses.
	 */
	printf("-----------------------------------------------------\n");
	if (madvise(addr, len, MADV_DONTNEED)) {
		errExit("fail to madvise");
	}
	l = 0x0;
	while (l < len) {
		memset(addr+l, '@', 1024);
		printf("#5. write address %p in main(): ", addr + l);
		printf("%c\n", addr[l]);
		l += 1024;
	}

	/*
	 * [U6]: point 5
	 * The memory region for the userfaultfd is read by resetting the variable l. Since, the page for this region
	 * had been previously accessed, a page fault does not occur when the code starts reading it. Now instead of 
	 * 'A' + 2, the page in this region will have the value @ at addresses through out the page. Hence, the 
	 * addresses and the character at these addresses i.e. @ is printed as the output.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#6. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U7]: point 5
	 * The kernel memory allocated for handling of userfaultfd does not expect access in the near future because 
	 * of using MADV_DONTNEED as parameter in madavise system call. Since, we access the region to write in it, a 
	 * page fault occurs because of access for the first time. The character ^ is written to the memory region handled
	 * by the userfaultfd in steps by incrementing the variable l and the written value is printed out along with the 
	 * memory addresses.
	 */
  
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		memset(addr+l, '^', 1024);
		printf("#7. write address %p in main(): ", addr + l);
		printf("%c\n", addr[l]);
		l += 1024;
	}

	/*
	 * [U8]: point 5
	 * The memory region for the userfaultfd is read by resetting the variable l. Since, the page for this region
	 * had been previously accessed, a page fault does not occur when the code starts reading it. Now instead of 
	 * 'A' + 3, the page in this region will have the value ^ at addresses through out the page. Hence, the 
	 * addresses and the character at these addresses i.e. ^ is printed as the output.
	 */

	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#8. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	exit(EXIT_SUCCESS);
}
