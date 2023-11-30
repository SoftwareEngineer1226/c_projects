#include "mmu.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

mmu *mmu_create() {
    mmu *my_mmu = calloc(1, sizeof(mmu));
    my_mmu->tlb = tlb_create();
    return my_mmu;
}

void mmu_read_from_virtual_address(mmu *this, addr32 virtual_address,
                                   size_t pid, void *buffer, size_t num_bytes) {
    assert(this);
    assert(pid < MAX_PROCESS_ID);
    assert(num_bytes + (virtual_address % PAGE_SIZE) <= PAGE_SIZE);
    // TODO: Implement me!

    //Start by following the pseudocode below:
    //o Use pid to check for a context switch. If there was a switch, flush the TLB
    if (pid != this->curr_pid) {
        tlb_flush(&this->tlb);
        this->curr_pid = pid;
    }
    //o Make sure that the address is in one of the segmentations. If not, raise a segfault and return
    if (!address_in_segmentations(this->segmentations[pid], virtual_address)) {
        mmu_raise_segmentation_fault(this);
        return;
    }
    //o Check the TLB for the page table entry. If it’s not there:
    addr32 base_virt_addr = virtual_address & 0xFFFFF000;
    page_table_entry *pte = tlb_get_pte(&this->tlb, base_virt_addr);
    if (!pte) {
        //▪ Raise a TLB miss
        mmu_tlb_miss(this);
        
        //▪ Get the page directory entry. If it’s not present in memory:
        uint32_t pd_idx = virtual_address >> 22;
        page_directory* page_dir = this->page_directories[pid];
        page_directory_entry* pde = &(page_dir->entries[pd_idx]);
        if(!pde->present) {
            //▪ Raise a page fault
            mmu_raise_page_fault(this);
            //▪ Ask the kernel for a frame
            pde->base_addr = (ask_kernel_for_frame(NULL) >> NUM_OFFSET_BITS);
            //▪ Update the page directory entry’s present, read_write, and user_supervisor flags
            pde->present = true;
            pde->read_write = true;
            pde->user_supervisor = true;
        }
        //▪ Get the page table using the PDE
        page_table *page_tab = (page_table*) get_system_pointer_from_pde(pde);
        //▪ Get the page table entry from the page table. Add the entry to the TLB
        addr32 pte_base = (virtual_address & 0x003FF000) >> NUM_OFFSET_BITS;
        pte = &(page_tab->entries[pte_base]);
        tlb_add_pte(&this->tlb, base_virt_addr, pte);
    }
    //o If the page table entry is not present in memory:
    if (!pte->present) {
        //▪ Raise a page fault
        mmu_raise_page_fault(this);
        //▪ Ask the kernel for a frame
        pte->base_addr = (ask_kernel_for_frame(pte) >> NUM_OFFSET_BITS);
        //▪ Update the page table entry’s present, read_write, and user_supervisor flags
        pte->present = true;
        pte->read_write = true;
        pte->user_supervisor = true;
        //▪ Read the page from disk
        read_page_from_disk((page_table_entry*) pte);
    }
    //o Check that the user has permission to perform the read or write operation. If not, raise a segfault and return
    vm_segmentation *seg = find_segment(this->segmentations[pid], virtual_address);
    if (!(seg->permissions & READ)) {
        mmu_raise_segmentation_fault(this);
        return;
    }
    //o Use the page table entry’s base address and the offset of the virtual address to compute the physical address. Get a physical pointer from this address
    void *ptr = (void*) get_system_pointer_from_pte(pte) + (virtual_address & 0xFFF);
    //o Perform the read or write operation
    memcpy(buffer, ptr, num_bytes);
    //o Mark the PTE as accessed. If writing, also mark it as dirty.
    pte->accessed = 1;
}

void mmu_write_to_virtual_address(mmu *this, addr32 virtual_address, size_t pid,
                                  const void *buffer, size_t num_bytes) {
    assert(this);
    assert(pid < MAX_PROCESS_ID);
    assert(num_bytes + (virtual_address % PAGE_SIZE) <= PAGE_SIZE);
    // TODO: Implement me!

    //Start by following the pseudocode below:
    //o Use pid to check for a context switch. If there was a switch, flush the TLB
    if (pid != this->curr_pid) {
        tlb_flush(&this->tlb);
        this->curr_pid = pid;
    }
    //o Make sure that the address is in one of the segmentations. If not, raise a segfault and return
    if (!address_in_segmentations(this->segmentations[pid], virtual_address)) {
        mmu_raise_segmentation_fault(this);
        return;
    }
    //o Check the TLB for the page table entry. If it’s not there:
    addr32 base_virt_addr = virtual_address & 0xFFFFF000;
    page_table_entry *pte = tlb_get_pte(&this->tlb, base_virt_addr);
    if (!pte) {
        //▪ Raise a TLB miss
        mmu_tlb_miss(this);
        
        //▪ Get the page directory entry. If it’s not present in memory:
        uint32_t pd_idx = virtual_address >> 22;
        page_directory* page_dir = this->page_directories[pid];
        page_directory_entry* pde = &(page_dir->entries[pd_idx]);
        if(!pde->present) {
            //▪ Raise a page fault
            mmu_raise_page_fault(this);
            //▪ Ask the kernel for a frame
            pde->base_addr = (ask_kernel_for_frame(NULL) >> NUM_OFFSET_BITS);
            //▪ Update the page directory entry’s present, read_write, and user_supervisor flags
            pde->present = true;
            pde->read_write = true;
            pde->user_supervisor = true;
        }
        //▪ Get the page table using the PDE
        page_table *page_tab = (page_table*) get_system_pointer_from_pde(pde);
        //▪ Get the page table entry from the page table. Add the entry to the TLB
        addr32 pte_base = (virtual_address & 0x003FF000) >> NUM_OFFSET_BITS;
        pte = &(page_tab->entries[pte_base]);
        tlb_add_pte(&this->tlb, base_virt_addr, pte);
    }
    //o If the page table entry is not present in memory:
    if (!pte->present) {
        //▪ Raise a page fault
        mmu_raise_page_fault(this);
        //▪ Ask the kernel for a frame
        pte->base_addr = (ask_kernel_for_frame(pte) >> NUM_OFFSET_BITS);
        //▪ Update the page table entry’s present, read_write, and user_supervisor flags
        pte->present = true;
        pte->read_write = true;
        pte->user_supervisor = true;
        //▪ Read the page from disk
        read_page_from_disk((page_table_entry*) pte);
    }
    //o Check that the user has permission to perform the read or write operation. If not, raise a segfault and return
    vm_segmentation *seg = find_segment(this->segmentations[pid], virtual_address);
    if (!(seg->permissions & WRITE)) {
        mmu_raise_segmentation_fault(this);
        return;
    }
    //o Use the page table entry’s base address and the offset of the virtual address to compute the physical address. Get a physical pointer from this address
    void *ptr = (void*) get_system_pointer_from_pte(pte) + (virtual_address & 0xFFF);
    //o Perform the read or write operation
    memcpy(ptr, buffer, num_bytes);
    //o Mark the PTE as accessed. If writing, also mark it as dirty.
    pte->accessed = 1;
    pte->dirty = 1;
}

void mmu_tlb_miss(mmu *this) {
    this->num_tlb_misses++;
}

void mmu_raise_page_fault(mmu *this) {
    this->num_page_faults++;
}

void mmu_raise_segmentation_fault(mmu *this) {
    this->num_segmentation_faults++;
}

void mmu_add_process(mmu *this, size_t pid) {
    assert(pid < MAX_PROCESS_ID);
    addr32 page_directory_address = ask_kernel_for_frame(NULL);
    this->page_directories[pid] =
        (page_directory *)get_system_pointer_from_address(
            page_directory_address);
    page_directory *pd = this->page_directories[pid];
    this->segmentations[pid] = calloc(1, sizeof(vm_segmentations));
    vm_segmentations *segmentations = this->segmentations[pid];

    // Note you can see this information in a memory map by using
    // cat /proc/self/maps
    segmentations->segments[STACK] =
        (vm_segmentation){.start = 0xBFFFE000,
                          .end = 0xC07FE000, // 8mb stack
                          .permissions = READ | WRITE,
                          .grows_down = true};

    segmentations->segments[MMAP] =
        (vm_segmentation){.start = 0xC07FE000,
                          .end = 0xC07FE000,
                          // making this writeable to simplify the next lab.
                          // todo make this not writeable by default
                          .permissions = READ | EXEC | WRITE,
                          .grows_down = true};

    segmentations->segments[HEAP] =
        (vm_segmentation){.start = 0x08072000,
                          .end = 0x08072000,
                          .permissions = READ | WRITE,
                          .grows_down = false};

    segmentations->segments[BSS] =
        (vm_segmentation){.start = 0x0805A000,
                          .end = 0x08072000,
                          .permissions = READ | WRITE,
                          .grows_down = false};

    segmentations->segments[DATA] =
        (vm_segmentation){.start = 0x08052000,
                          .end = 0x0805A000,
                          .permissions = READ | WRITE,
                          .grows_down = false};

    segmentations->segments[TEXT] =
        (vm_segmentation){.start = 0x08048000,
                          .end = 0x08052000,
                          .permissions = READ | EXEC,
                          .grows_down = false};

    // creating a few mappings so we have something to play with (made up)
    // this segment is made up for testing purposes
    segmentations->segments[TESTING] =
        (vm_segmentation){.start = PAGE_SIZE,
                          .end = 3 * PAGE_SIZE,
                          .permissions = READ | WRITE,
                          .grows_down = false};
    // first 4 mb is bookkept by the first page directory entry
    page_directory_entry *pde = &(pd->entries[0]);
    // assigning it a page table and some basic permissions
    pde->base_addr = (ask_kernel_for_frame(NULL) >> NUM_OFFSET_BITS);
    pde->present = true;
    pde->read_write = true;
    pde->user_supervisor = true;

    // setting entries 1 and 2 (since each entry points to a 4kb page)
    // of the page table to point to our 8kb of testing memory defined earlier
    for (int i = 1; i < 3; i++) {
        page_table *pt = (page_table *)get_system_pointer_from_pde(pde);
        page_table_entry *pte = &(pt->entries[i]);
        pte->base_addr = (ask_kernel_for_frame(pte) >> NUM_OFFSET_BITS);
        pte->present = true;
        pte->read_write = true;
        pte->user_supervisor = true;
    }
}

void mmu_remove_process(mmu *this, size_t pid) {
    assert(pid < MAX_PROCESS_ID);
    // example of how to BFS through page table tree for those to read code.
    page_directory *pd = this->page_directories[pid];
    if (pd) {
        for (size_t vpn1 = 0; vpn1 < NUM_ENTRIES; vpn1++) {
            page_directory_entry *pde = &(pd->entries[vpn1]);
            if (pde->present) {
                page_table *pt = (page_table *)get_system_pointer_from_pde(pde);
                for (size_t vpn2 = 0; vpn2 < NUM_ENTRIES; vpn2++) {
                    page_table_entry *pte = &(pt->entries[vpn2]);
                    if (pte->present) {
                        void *frame = (void *)get_system_pointer_from_pte(pte);
                        return_frame_to_kernel(frame);
                    }
                    remove_swap_file(pte);
                }
                return_frame_to_kernel(pt);
            }
        }
        return_frame_to_kernel(pd);
    }

    this->page_directories[pid] = NULL;
    free(this->segmentations[pid]);
    this->segmentations[pid] = NULL;

    if (this->curr_pid == pid) {
        tlb_flush(&(this->tlb));
    }
}

void mmu_delete(mmu *this) {
    for (size_t pid = 0; pid < MAX_PROCESS_ID; pid++) {
        mmu_remove_process(this, pid);
    }

    tlb_delete(this->tlb);
    free(this);
    remove_swap_files();
}
