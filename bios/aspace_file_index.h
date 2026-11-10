/* Included once by the selected MMU backend after FILE_BACKING is declared.
 * The list indexes backing+offset cache reuse; physical lookup uses the AVL
 * tree. Both are views of the same records, not additional resource owners. */
#ifndef ASPACE_FILE_INDEX_H
#define ASPACE_FILE_INDEX_H
#include "aspace_index.h"
typedef struct file_page {
    struct cow_index index; /* first member: stable intrusive index identity */
    struct file_page *next;
    struct file_page **previous;
    FILE_BACKING *backing;
    PADDR physical, first_root;
    ULONG offset, references;
} FILE_PAGE;
static FILE_PAGE *file_pages;
static struct cow_index *file_page_index;
static FILE_PAGE *file_page_find(PADDR physical)
{ return (FILE_PAGE *)cow_index_find(file_page_index,physical); }
static void file_page_publish(FILE_PAGE *page)
{
    page->index.key = page->physical;
    file_page_index = cow_index_add(file_page_index,&page->index);
    page->next = file_pages; page->previous = &file_pages;
    if (page->next) page->next->previous = &page->next;
    file_pages = page;
}
static void file_page_unpublish(FILE_PAGE *page)
{
    file_page_index = cow_index_remove(file_page_index,page->physical);
    *page->previous = page->next;
    if (page->next) page->next->previous = page->previous;
}
#endif
