#include "loader.h"

// Typedef for _start function signature, assuming it returns an int
typedef int (*start_func_t)(void);

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;
void *virtual_mem = NULL;

/*
 * release memory and other cleanups
 */
void loader_cleanup() {
      if (virtual_mem != NULL) {
        munmap(virtual_mem, phdr->p_memsz);
    }
    if (fd != -1) {
        close(fd);
    }
}

/*
 * Load and run the ELF executable file
 */
void load_and_run_elf(char** exe) {
    // fd - file descriptor
  fd = open(exe[1], O_RDONLY); // getting file descriptor for the input binary (read-only mode)
  
      if (fd == -1) { //file descriptor assigned wrongly
        perror("Can't open file"); //Throwing error message to stderr
        return;
    }

    // Reading ELF header
    Elf32_Ehdr elf_header;
    ssize_t bytes_read = read(fd, &elf_header, sizeof(Elf32_Ehdr)); //number of bytes to read
    if (bytes_read != sizeof(Elf32_Ehdr)) {
        perror("Error reading ELF header");
        close(fd);
        return;
    }

    // Checking if its an elf file
    if (elf_header.e_ident[EI_MAG0] != ELFMAG0 ||  //Since first 4 bytes of e_ident contains the ELF magic number
        elf_header.e_ident[EI_MAG1] != ELFMAG1 ||   //EI_MAG0 - 0x7f
        elf_header.e_ident[EI_MAG2] != ELFMAG2 ||   //EI_MAG1 - E // EI_MAG2 - L
        elf_header.e_ident[EI_MAG3] != ELFMAG3) {   //EI_MAG3 - F
        fprintf(stderr, "Not a valid ELF file\n");  //fprintf: to print content in file
        close(fd);
        return;
    }

    // CHecking if its a 32 bit elf file
    if (elf_header.e_ident[EI_CLASS] != ELFCLASS32) { //If it were 64 bits it would have corresponded to ELFCLASS64 which comes from <elf.h>
        fprintf(stderr, "Not 32 bit file\n");
        close(fd);
        return;
    }

    // Checking endianness. We want little endian
    if (elf_header.e_ident[EI_DATA] != ELFDATA2LSB) { //If it were big-endian it would have corresponed to ELFDATA2MSB
        fprintf(stderr, "Wrong endianness\n");
        close(fd);
        return ;
    }

    // Getting the entry point address from ELF header
    Elf32_Addr entry_point = elf_header.e_entry;

    // Iterating through PHDR Table
    Elf32_Phdr prog_header;
    void *entry_seg = NULL;

    for (int i = 0; i < elf_header.e_phnum; i++) { //elf_header.e_phnum : Number of program headers
        // Seek to the program header
        if (lseek(fd, elf_header.e_phoff + (i * sizeof(Elf32_Phdr)), SEEK_SET) == -1) { //starts from program header offset and is pushed the size of an entry in each iteration
            perror("Couldn't reach program header");
            close(fd);
            return;
        }

        // Reading the program header
        bytes_read = read(fd, &prog_header, sizeof(Elf32_Phdr));
        if (bytes_read != sizeof(Elf32_Phdr)) {
            perror("Couldn't read program header");
            close(fd);
            return;
        }

        if (prog_header.p_type == PT_LOAD) { //We are looking for first Program header whose type is PT_LOAD
            // Allocating memory for the segment
            void *segment = mmap(NULL, prog_header.p_memsz,
                                 PROT_READ | PROT_WRITE | PROT_EXEC,
                                 MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
            if (segment == MAP_FAILED) {
                perror("Couldn't allocate memory for segment");
                close(fd);
                return;
            }



            // Seeking to the segment data in the file
            if (lseek(fd, prog_header.p_offset, SEEK_SET) == -1) { //SEEK_SET: tells lseek to put fd offset equal to prog_header.p_offset
                perror("Couldn't place file descirptor to beginning of header"); //p_offset : Offset from beginning of elf file to where the segment is located
                munmap(segment, prog_header.p_memsz); //Unmaps the memory region
                close(fd);
                return;
            }

            // Reading segment data
            bytes_read = read(fd, segment, prog_header.p_filesz); //reads segment data of size given by prog_header.p_filesz
            if (bytes_read != prog_header.p_filesz) {
                perror("Couldn't read segment data");
                munmap(segment, prog_header.p_memsz);
                close(fd);
                return;
            }

            // Checking if entry point is correctly located within the segment
            if (entry_point >= prog_header.p_vaddr && //p_vaddr gives address in virtual memory where segment is mapped when executed
                entry_point < prog_header.p_vaddr + prog_header.p_memsz) {
                entry_seg = (void *)(segment + (entry_point - prog_header.p_vaddr));
                //Method of finding entry point:
                //going to physical starting address of virtual memory allocated
                //Finding offset of entry point into segment by subtracting p_vaddr from entry_point
                //Adding this offset to the physical address to get the physical address of the starting point of function.

            }
        }
    }

    // Checking if the entry point was found
    if (entry_seg == NULL) {
        fprintf(stderr, "Couldn't find entry point\n");
        close(fd);
        return;
    }
  // 5. Typecast the address to that of function pointer matching "_start" method in fib.c.

    start_func_t _start = (start_func_t)entry_seg;
    int result = _start();
  // 6. Call the "_start" method and print the value returned from the "_start"
  printf("User _start return value = %d\n",result);
}

int main(int argc, char** argv)
{
  if(argc != 2) {
    printf("Usage: %s <ELF Executable> \n",argv[0]);
    exit(1);
  }
  // 1. carry out necessary checks on the input ELF file
  // 2. passing it to the loader for carrying out the loading/execution
  load_and_run_elf(argv);
  // 3. invoke the cleanup routine inside the loader
  loader_cleanup();
  return 0;
}
