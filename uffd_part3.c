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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8081
#define BUFF_SIZE 4096


#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE);	\
	} while (0)

static int page_size;

static void *
fault_handler_thread(void *arg)
{
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	/*static int fault_cnt = 0;*/     /* Number of faults so far handled */
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

		/*printf("\nfault_handler_thread():\n");
		printf("    poll() returns: nready = %d; "
                       "POLLIN = %d; POLLERR = %d\n", nready,
                       (pollfd.revents & POLLIN) != 0,
                       (pollfd.revents & POLLERR) != 0);*/

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
		printf("[x] PAGEFAULT\n");
		//printf("flags = %llx; ", msg.arg.pagefault.flags);
		//printf("address = %llx\n", msg.arg.pagefault.address);

		/* [H7: point 1]
		 * Copy the page into the faulting region and varying the contents copied in, so that each page fault is handled separately. 
		 
		memset(page, 'A' + fault_cnt % 20, page_size);
		fault_cnt++;*/

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
		 
		printf("(uffdio_copy.copy returned %lld)\n",uffdio_copy.copy);*/
	}
}

struct to_send{
	char *a;
	int b;
};

struct to_receive{
	char *a;
	int b;
};

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
        int server_fd, new_socket;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);
	int sock = 0;
	struct sockaddr_in serv_addr;
	char command;
        int pg_num;
	char *server = "server";
	char *client = "client";

	if (argc == 2 && strcmp(argv[1],server) == 0){
		char num_page[1];
		printf("Enter number of pages \n");
		scanf("%s", num_page);
		printf("Number of pages are: %s\n", num_page);

		if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
			perror("\n Socket failed \n");
			exit(EXIT_FAILURE);
		}

		if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
					&opt, sizeof(opt))) {
			perror("setsockopt");
			exit(EXIT_FAILURE);
		}

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons( PORT );

		if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
			printf("\nBind Failed \n");
			exit(EXIT_FAILURE);
		}


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
		char *a = (char *)num_page;	
		page_size = sysconf(_SC_PAGE_SIZE);
		len = strtoul(a, NULL, 0) * page_size;
			
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

		printf("Press key to send address and length\n");
		getchar();
		printf("Sending address\n");
      
		if (listen(server_fd, 3) < 0){
			perror("listen");
			exit(EXIT_FAILURE);
		}		

		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, 
						(socklen_t*)&addrlen)) < 0){
			perror("accept");
			exit(EXIT_FAILURE);
		}

		char *add = (char *)addr;	
		struct to_send buffer = {add, len};
        	send(new_socket, (char *)&buffer, sizeof(buffer), 0);
        	printf("Address and length sent to client: %p, %d\n", buffer.a, buffer.b);
	
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
		int pages = strtoul(a, NULL, 0);
		while(1){
			printf("Which command should I run ? (r:read, w:write):\n");
			scanf("%c", &command);
			while((getchar()) != '\n');
			printf("For which page? (0-%d, or -1 for all)\n", pages - 1);
		        scanf("%d", &pg_num);
			while((getchar()) != '\n');
			if (command == 'r'){
				if (pg_num == -1){
					for(int i = 0; i < pages; i++){
						char dest[len/pages];
						int l = (i*(len/pages)) + 0x0;
						memcpy(dest, addr + l, (len/pages));
						printf("[*] Page %d:\n%s\n", i, dest);
					}
				}
				else {
					int l = (pg_num*(len/pages)) + 0x0;
					char dest[4096];
					memcpy(dest, addr + l, 4096);
					printf("[*] Page %d:\n%s\n", pg_num, dest);
				}
			}

			else{
				if (pg_num == -1){
					char *buffer;
					size_t size = 4096;
					buffer = (char *)malloc(size);
					printf("Enter string to be written\n");
					int bytes_read = getline(&buffer, &size, stdin);
					printf("Number of bytes read %d:\n", bytes_read);
					for (int i = 0; i < pages; i++){				
						int l = (i*(len/pages)) + 0x0;
						memcpy(addr + l, buffer, size);
						printf("[*] Page %d written with %s: \n", i, buffer);
					}
				}

				else{
					char *buffer;
					size_t size = 4096;
					buffer = (char *)malloc(size);
					printf("Enter string to be written\n");				
					int bytes_read = getline(&buffer, &size, stdin);
					printf("Number of bytes read %d\n", bytes_read);
					int l = (pg_num*(len/pages)) + 0x0;
					memcpy(addr + l, buffer, 4096);
					printf("[*] Page %d written with %s: \n", pg_num, buffer);
				}
			}	
		}		
		
	}

	if (argc == 2 && strcmp(argv[1],client) == 0){
		if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
			printf("Socket creation error \n");
			return -1;
		}

		memset(&serv_addr, '0', sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(PORT);

		if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0){
			printf("Invalid address/ Address not supported \n");
			return -1;
		}

		while (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
			printf("Connecting to server \n");
			sleep(2);
		}
		printf("Connection established\n");

		page_size = sysconf(_SC_PAGE_SIZE);
		uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
		if (uffd == -1)
			errExit("userfaultfd");

		uffdio_api.api = UFFD_API;
		uffdio_api.features = 0;
		if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
			errExit("ioctl-UFFDIO_API");

		struct to_receive data;
		printf("Print any key to receive data \n");
		getchar();

		read(sock, (char *)&data, 1024);
		printf("Address received: %p\n", data.a);
		printf("Length received: %d\n", data.b);
		unsigned long len_rec = (long)data.b;

		char *add = mmap((char *)(data.a), len_rec, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (add == MAP_FAILED)
			errExit("mmap");

		printf("Address shared by mmap() = %p\n", add);

		uffdio_register.range.start = (unsigned long) add;
		uffdio_register.range.len = len_rec;
		uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
		if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
			errExit("ioctl-UFFDIO_REGISTER");

		printf("Memory Registered\n");
	}	

	exit(EXIT_SUCCESS);
}
