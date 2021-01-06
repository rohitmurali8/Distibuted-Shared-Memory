# Distibuted-Shared-Memory

The goal of this project is to implement page-granule MSI protocol among different machines using userfaultfd. The machines will communicate their page information using sockets.
Userfaultfd is used to create a file descriptor for handling page faults in user space. This project is split into four parts.

In the first part, we need to analyze of sample userfaultfd demo application in order to understand how to monitor page faults in the registered memory region in order
to read/write data to pages in the  respective memory region.

In the second part we create an application where two instances of it can be spun up. In the First instance we mmap a memory region according to the number of pages that
user specifies through stdin. The returned memory address for mmapped region and the size will be communicated to the second instance over socket communications. This 
will ensure that the the two instances of the application are paired with “shared” memory region with the same size. TCP/IP protocol is used to send multiple data 
information as a structure between the two instances.

In the third part, once you have a mmapped memory region we register the memory region with userfaultfd.Once paired, The code should repeatly ask user for two inputs for 
each iterations:

    ” > Which command should I run? (r:read, w:write): “
    
    ” > For which page? (0-%i, or -1 for all): “
    

 “%i” is replaced with the the number of pages specified by the user from Part 2. This will run the command accordingly to interact with mmaped region. When
a pagefault occurs, printf: " [x] PAGEFAULT\n". For each page, based on the command we print the contents such as: " [*] Page %i:\n%s\n" with %i being the page number and %s being the page content."

The output of the third part can be seen in the images below:

![alt tag](https://github.com/rohitmurali8/Distributed-Shared-Memory/blob/master/1.PNG)

![alt tag](https://github.com/rohitmurali8/Distributed-Shared-Memory/blob/master/2.PNG)
