#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_clock.h>
#include <list.h>

list_entry_t pra_list_head;
static list_entry_t *clock_arm;

static void
print_list(list_entry_t * head){
    if ( head == NULL){
        extern struct mm_struct *check_mm_struct;
        head=(list_entry_t*) check_mm_struct->sm_priv;
    }
    cprintf("clock_arm: 0x%08x.\n", clock_arm);
    cprintf("le addr      -   vaddr        -   DA bits\n");
    list_entry_t *cur = head->next;
    while(cur != head){
        struct Page * page = le2page(cur, pra_page_link);
        pte_t * pte_p = get_pte(check_mm_struct->pgdir, page->pra_vaddr, 0);
        cprintf("0x%08x   -   0x%08x   -   %d%d\n", &page->pra_page_link, page->pra_vaddr, PTE_DIRTY(pte_p), PTE_ACCESSED(pte_p));
        cur = cur->next;
    }
}

static void diag(void)__attribute__((unused));
static void
diag(void){
    print_list(NULL);
}

/*
 * (2) _clock_init_mm: init pra_list_head and let  mm->sm_priv point to the addr of pra_list_head.
 *              Now, From the memory control struct mm_struct, we can access FIFO PRA
 */
static int
_clock_init_mm(struct mm_struct *mm)
{     
     list_init(&pra_list_head);
     mm->sm_priv = &pra_list_head;
     clock_arm = &pra_list_head;
     //cprintf(" mm->sm_priv %x in clock_init_mm\n",mm->sm_priv);
     return 0;
}
/*
 * (3)_clock_map_swappable: According FIFO PRA, we should link the most recent arrival page at the back of pra_list_head qeueue
 */
static int
_clock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && head != NULL);
    //record the page access situlation
    //(1)link the most recent arrival page at the back of the pra_list_head qeueue.
    list_add(clock_arm, entry);
    clock_arm = clock_arm->next;
    return 0;
}

/*
 *  (4)_clock_swap_out_victim: According FIFO PRA, we should unlink the  earliest arrival page in front of pra_list_head qeueue,
 *                            then set the addr of addr of this page to ptr_page.
 */
static int
_clock_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
        assert(head != NULL);
    assert(in_tick==0);
    /* Select the victim */
    struct Page * clock_arm_page = NULL;
    while(1){
        clock_arm = clock_arm->next;
        if (clock_arm == head){   // The pointer of the clock has run for a whole circle. Skip the sentinel.
            continue;
        }
        clock_arm_page = le2page(clock_arm, pra_page_link);
        uintptr_t vaddr = clock_arm_page->pra_vaddr;
        tlb_invalidate(mm->pgdir, vaddr);
        pte_t *pte_p = get_pte(mm->pgdir, vaddr, 0);
        if(pte_p == NULL){
            panic("The page table entry for the page 0x%08x in swap manager does not exist", clock_arm_page);
        }
        if(PTE_DIRTY(pte_p)){
            //cprintf("Clearing dirty bit of 0x%08x.\n", clock_arm_page->pra_vaddr);
            PTE_CLEAR_DIRTY(pte_p);
            continue;
        }else{
            if(PTE_ACCESSED(pte_p)){
                PTE_CLEAR_ACCESSED(pte_p);
                continue;
            }else{  // the pte is neither accessed nor dirty
                break;
            }
        }
    }
    //(1)  unlink the page that is not accessed recently
    //(2)  set the addr of addr of this page to ptr_page
    cprintf("Swapping out 0x%08x.\n", clock_arm_page->pra_vaddr);
    *ptr_page = clock_arm_page;
    list_entry_t * le_to_del = clock_arm;
    clock_arm = clock_arm->prev;
    list_del(le_to_del);
    return 0;
}


static int
_clock_check_swap(void) {
    //  At this stage, there in page in the swap manager
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    assert(check_mm_struct != NULL);
    cprintf("write Virt Page a in clock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in clock_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in clock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    //  After 4 page fault, there are 4 page in the swap manager, and
    //  the arm of the clock is at the position where
    //  the last element is inserted, the structure is
    //  head->a(1,1)->b(1,1)->c(1,1)->d(1,1)->head
    //                                ^  
    diag();
    cprintf("write Virt Page e in clock_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    //  Now e is written. A page should be swapped out, the arm of the clock would 
    //  run 2 circles, clearing all bits, and find the first pte with two 0 bits
    //  head->a(0,0)->b(0,0)->c(0,0)->d(0,0)->head
    //        ^  
    //  Now a would be selected to be swapped out, and the resulting list is
    //  head->e(1,1)->b(0,0)->c(0,0)->d(0,0)->head
    //        ^  
    diag();
    cprintf("write Virt Page b in clock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    //  b is not swapped out, so no page fault happens, but its bits are set
    //  Note this behavior is different from FIFO
    //  head->e(1,1)->b(1,1)->c(0,0)->d(0,0)->head
    //        ^                  
    diag();
    cprintf("write Virt Page a in clock_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    //  a has been swapped out, another page should sacrifice
    //  head->e(1,1)->b(0,1)->c(0,0)->d(0,0)->head
    //                        ^    
    //  head->e(1,1)->b(0,1)->a(1,1)->d(0,0)->head
    //                        ^
    diag();
    cprintf("write Virt Page b in clock_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==6);
    //  b is not swapped out. But bits are set
    //  head->e(1,1)->b(1,1)->a(1,1)->d(0,0)->head
    //                        ^
    diag();
    cprintf("write Virt Page c in clock_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==7);
    //  c has been swapped out, find sacrifice page and swap it out
    //  head->e(1,1)->b(1,1)->a(1,1)->c(1,1)->head
    //                                ^    
    diag();
    cprintf("write Virt Page d in clock_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==8);
    //  d is not in the list, find sacrifice page
    //  head->e(0,0)->b(0,0)->a(0,0)->c(0,0)->head
    //        ^                                    
    //  Then swap it out
    //  head->d(1,1)->b(0,0)->a(0,0)->c(0,0)->head
    //        ^                                    
    diag();
    cprintf("write Virt Page e in clock_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==9);
    //  e is not in the list, swap a page out
    //  head->d(1,1)->e(1,1)->a(0,0)->c(0,1)->head
    //                ^                                          
    diag();
    cprintf("write Virt Page a in clock_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==9);
    //  a is in the list
    //  head->d(1,1)->e(1,1)->a(1,1)->c(0,0)->head
    //                ^                                            
    diag();
    cprintf("read Virt Page c in clock_check_swap\n");
    unsigned char tmp_char __attribute__((unused));
    tmp_char = *(unsigned char *)0x3000;
    assert(pgfault_num==9);
    //  a is in the list
    //  head->d(1,1)->e(1,1)->a(1,1)->c(0,1)->head
    //                ^                                            
    diag();
    cprintf("read Virt Page b in clock_check_swap\n");
    tmp_char= *(unsigned char *)0x2000;
    assert(pgfault_num==10);
    //  b is not in the list, find a sacrifice page 
    //  head->d(0,1)->e(0,1)->a(0,0)->c(0,0)->head
    //                                ^                             
    //  then swap it out
    //  head->d(0,1)->e(0,1)->a(0,0)->b(0,1)->head
    diag();
    return 0;
}

static int
_clock_init(void)
{
    return 0;
}

static int
_clock_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_clock_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_clock =
{
     .name            = "clock swap manager",
     .init            = &_clock_init,
     .init_mm         = &_clock_init_mm,
     .tick_event      = &_clock_tick_event,
     .map_swappable   = &_clock_map_swappable,
     .set_unswappable = &_clock_set_unswappable,
     .swap_out_victim = &_clock_swap_out_victim,
     .check_swap      = &_clock_check_swap,
};
