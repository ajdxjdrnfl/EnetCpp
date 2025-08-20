/**
 @file list.c
 @brief ENet linked list functions
*/
#define ENET_BUILDING_LIB 1
#include "enet.h"
#include "list.h"
#include <list>

/**
    @defgroup list ENet linked list utility functions
    @ingroup private
    @{
*/

ENetListNode* ENetList::insert(iterator position, void* data)
{
    ENetListNode* result = reinterpret_cast<ENetListNode*>(data);

    result->previous = position->previous;
    result->next = &(*position);

    result->previous->next = result;
    position->previous = result;

    return result;
}

ENetListIterator enet_list_insert(ENetListIterator position, void* data)
{
    ENetListIterator result = (ENetListNode*)(data);

    result->previous = position->previous;
    result->next = &(*position);

    result->previous->next = &(*result);
    position->previous = &(*result);

    return result;
}

void* ENetList::remove( iterator position )
{
    position->previous->next = position->next;
    position->next->previous = position->previous;

    return &(*position);
}

void* enet_list_remove(ENetListIterator position)
{
    position->previous->next = position->next;
    position->next->previous = position->previous;

    return &(*position);
}

bool ENetList::move(iterator position, void* dataFirst, void* dataLast)
{
    ENetListNode* first = reinterpret_cast<ENetListNode*>(dataFirst);
    ENetListNode* last = reinterpret_cast<ENetListNode*>(dataLast);

    first->previous->next = last->next;
    last->next->previous = first->previous;

    first->previous = position->previous;
    last->next = &(*position);

    first->previous->next = first;
    position->previous = last;

    return first;
}

ENetList::ENetList()
{
    clear();
}

void ENetList::clear()
{
    sentinel.next = &sentinel;
    sentinel.previous = &sentinel;
}

size_t ENetList::size()
{
    size_t size = 0;

    for (auto position = begin(); position != end(); position++)
        ++size;

    return size;
}