#include "loader.h"
#include <signal.h>

// Typedef for _start function signature, assuming it returns an int
typedef int (*start_func_t)(void);

// made a struct for tracking statistics
static struct {
    int p_faults;
    int p_allocations;
    size_t i_frag;
} stats = {0, 0, 0};

// Global variables to store ELF information
static struct {
    Elf32_Ehdr *ehdr;
    Elf32_Phdr *phdr;
    int phnum;
    int fd;
    void *entry_point;
} elf_info = {0};

// Page size (4KB)
#define PAGE_SIZE 4096

#define ROUND_UP(x, y) ((x + y - 1) & ~(y - 1))

// Structure to track segment mappings
typedef struct {
    Elf32_Addr start_addr;
    Elf32_Addr end_addr;
    Elf32_Off offset;
    int prot;
    void *mapped_addr;
    size_t size;
    size_t filesz;
} segment_mapping_t;

#define MAX_SEGMENTS 10
static segment_mapping_t segments[MAX_SEGMENTS];
static int num_segments = 0;

static void handle_segfault(int sig, siginfo_t *si, void *unused);
static int load_page(void *fault_addr);
static segment_mapping_t *find_segment(void *addr);


static void setup_signal_handler() {
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handle_segfault;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}


static void handle_segfault(int sig, siginfo_t *si, void *unused) {
    void *fault_addr = si->si_addr;
    
    if (load_page(fault_addr) == 0) {
        return; 
    }
    
    // If we couldn't handle the page fault, we will terminate 
    fprintf(stderr, "Unhandled page fault at address: %p\n", fault_addr);
    exit(1);
}

static segment_mapping_t *find_segment(void *addr) {
    Elf32_Addr vaddr = (Elf32_Addr)addr;
    for (int i = 0; i < num_segments; i++) {
        if (vaddr >= segments[i].start_addr && vaddr < segments[i].end_addr) {
            return &segments[i];
        }
    }
    return NULL;
}

static int load_page(void *fault_addr) {
    segment_mapping_t *seg = find_segment(fault_addr);
    if (!seg) return -1;

    Elf32_Addr page_start = (Elf32_Addr)fault_addr & ~(PAGE_SIZE - 1);
    Elf32_Addr seg_offset = page_start - seg->start_addr;
    
    void *mapped_page = mmap((void*)page_start, PAGE_SIZE, 
                            PROT_READ | PROT_WRITE,  
                            MAP_PRIVATE | MAP_FIXED, 
                            elf_info.fd, seg->offset + seg_offset);
    
    if (mapped_page == MAP_FAILED) {
        perror("mmap in page fault handler");
        return -1;
    }

    size_t p_off = (Elf32_Addr)fault_addr & (PAGE_SIZE - 1);
    size_t f_rem = seg->filesz - (seg_offset + p_off);
    
    if (f_rem < PAGE_SIZE) {
        size_t zero_start = p_off + f_rem;
        size_t zero_size = PAGE_SIZE - zero_start;
        memset(mapped_page + zero_start, 0, zero_size);
    }

    if (mprotect(mapped_page, PAGE_SIZE, seg->prot) == -1) {
        perror("mprotect");
        return -1;
    }

    stats.p_faults++;
    stats.p_allocations++;
    
    if (page_start + PAGE_SIZE > seg->end_addr) {
        stats.i_frag += (page_start + PAGE_SIZE) - seg->end_addr;
    }
    
    return 0;
}

static int setup_segments(Elf32_Ehdr *ehdr, int fd) {
    Elf32_Phdr phdr;
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (lseek(fd, ehdr->e_phoff + (i * sizeof(Elf32_Phdr)), SEEK_SET) == -1) {
            perror("lseek");
            return -1;
        }

        if (read(fd, &phdr, sizeof(Elf32_Phdr)) != sizeof(Elf32_Phdr)) {
            perror("read");
            return -1;
        }

        if (phdr.p_type == PT_LOAD) {
            if (num_segments >= MAX_SEGMENTS) {
                fprintf(stderr, "Too many segments\n");
                return -1;
            }

            int prot = 0;
            if (phdr.p_flags & PF_R) prot |= PROT_READ;
            if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
            if (phdr.p_flags & PF_X) prot |= PROT_EXEC;

            segments[num_segments].start_addr = phdr.p_vaddr;
            segments[num_segments].end_addr = phdr.p_vaddr + phdr.p_memsz;
            segments[num_segments].offset = phdr.p_offset;
            segments[num_segments].size = phdr.p_memsz;
            segments[num_segments].filesz = phdr.p_filesz;
            segments[num_segments].prot = prot;
            segments[num_segments].mapped_addr = NULL;
            
            if (ehdr->e_entry >= phdr.p_vaddr && 
                ehdr->e_entry < phdr.p_vaddr + phdr.p_memsz) {
                elf_info.entry_point = (void*)ehdr->e_entry;
            }
            
            num_segments++;
        }
    }
    
    return 0;
}

void loader_cleanup() {
    for (int i = 0; i < num_segments; i++) {
        if (segments[i].mapped_addr != NULL) {
            size_t size = ROUND_UP(segments[i].size, PAGE_SIZE);
            munmap(segments[i].mapped_addr, size);
        }
    }
    
    if (elf_info.fd != -1) {
        close(elf_info.fd);
    }
}

void load_and_run_elf(char** exe) {
    memset(&stats, 0, sizeof(stats));
    
    elf_info.fd = open(exe[1], O_RDONLY);
    if (elf_info.fd == -1) {
        perror("Cannot open file");
        return;
    }
    // Reading ELF header
    Elf32_Ehdr ehdr;
    if (read(elf_info.fd, &ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
        perror("Error reading ELF header");
        close(elf_info.fd);
        return;
    }

    // Checking if its an elf file
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || 
        ehdr.e_ident[EI_MAG1] != ELFMAG1 || 
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || 
        ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "Not a valid ELF file\n");
        close(elf_info.fd);
        return;
    }


    // CHecking if its a 32 bit elf file
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "Not a 32-bit ELF file\n");
        close(elf_info.fd);
        return;
    }

    if (setup_segments(&ehdr, elf_info.fd) != 0) {
        fprintf(stderr, "Failed to setup segments\n");
        close(elf_info.fd);
        return;
    }

    setup_signal_handler();

    if (elf_info.entry_point == NULL) {
        fprintf(stderr, "Could not find entry point\n");
        close(elf_info.fd);
        return;
    }

 // 5. Typecast the address to that of function pointer matching "_start" method in fib.c.
    start_func_t _start = (start_func_t)elf_info.entry_point;
    int result = _start();

    // Results and stats
    printf("User _start return value = %d\n", result);
    printf("Statistics:\n");
    printf("  Page faults: %d\n", stats.p_faults);
    printf("  Page allocations: %d\n", stats.p_allocations);
    printf("  Internal fragmentation: %.2f KB\n", stats.i_frag / 1024.0);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("Usage: %s <ELF Executable>\n", argv[0]);
        exit(1);
    }
    
  // 1. carry out necessary checks on the input ELF file
  // 2. passing it to the loader for carrying out the loading/execution
  load_and_run_elf(argv);
  // 3. invoke the cleanup routine inside the loader
  loader_cleanup();
    return 0;
}