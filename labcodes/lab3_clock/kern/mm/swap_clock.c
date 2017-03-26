#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <mmu.h>
#include <swap.h>
#include <swap_clock.h>
#include <list.h>

list_entry_t pra_list_head;
static list_entry_t *cur;
/*
 * (2) _clock_init_mm: init pra_list_head and let  mm->sm_priv point to the addr of pra_list_head.
 *              Now, From the memory control struct mm_struct, we can access FIFO PRA
 */
static int
_clock_init_mm(struct mm_struct *mm)
{     
     list_init(&pra_list_head);
     mm->sm_priv = &pra_list_head;
     cur = &pra_list_head;
     //cprintf(" mm->sm_priv %x in clock_init_mm\n",mm->sm_priv);
     return 0;
}
static void
print_list(list_entry_t * head){
    if ( head == NULL){
        extern struct mm_struct *check_mm_struct;
        head=(list_entry_t*) check_mm_struct->sm_priv;
    }
    cprintf("head---> %x\n", head);
    cprintf("head->prev---> %x\n", head->prev);
    list_entry_t *cur = head->next;
    while(cur != head){
        struct Page * page = le2page(cur, pra_page_link);
        cprintf("%x - %x - %x\n", cur, page, page->pra_vaddr);
        cur = cur->next;
    }
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
    list_add_before(head, entry);
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
    struct Page * cur_page = NULL;
    while(1){
        cur = cur->next;
        if (cur == head){   // The pointer of the clock has run for a whole circle. Skip the sentinel.
            continue;
        }
        cur_page = le2page(cur, pra_page_link);
        pte_t *pte_p = get_pte(mm->pgdir, cur_page->pra_vaddr, 0);
        if(pte_p == NULL){
            panic("The page table entry for the page 0x%08x in swap manager does not exist", cur_page);
        }
        if(PTE_DIRTY(pte_p)){
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
    *ptr_page = cur_page;
    list_del(cur);
    return 0;
}

static void diag(int avoid)__attribute__((unused));
static void
diag(int avoid){
    print_list(NULL);
    int i;
    for (i = 0; i < 5 ; i++){
        if (i == avoid) continue;
        cprintf("%x, ", *(unsigned char *)((1 + i) * 0x1000));
    }
}

static int
_clock_check_swap(void) {
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    assert(check_mm_struct != NULL);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    //diag(5);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    //diag(0);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    //diag(0);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    //diag(1);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==7);
    //diag(2);
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==8);
    //diag(3);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==9);
    //diag(4);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==10);
    //diag(1);
    cprintf("write Virt Page a in fifo_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    //diag(1);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==11);
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
