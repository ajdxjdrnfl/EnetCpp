#pragma once
/**
 @file  list.h
 @brief ENet list management
*/
#ifndef __ENET_LIST_H__
#define __ENET_LIST_H__

#include <stdlib.h>

struct ENetListNode
{
	ENetListNode* next;
	ENetListNode* previous;
};

template<typename T>
struct ENetIterator
{
    using value_type = T;
    using pointer = T*;
    using reference = T&;

    pointer node;

    ENetIterator(pointer n = nullptr) : node(n) {}

    reference operator*() const {
        return *static_cast<pointer>(node);
    }

    pointer operator->() const {
        return static_cast<pointer>(node);
    }

    ENetListIterator& operator++() {
        node = node->next;
        return *this;
    }

    ENetListIterator operator++(int) {
        ENetListIterator tmp(*this);
        ++(*this);
        return tmp;
    }

    ENetListIterator& operator--() {
        node = node->previous;
        return *this;
    }

    ENetListIterator operator--(int) {
        ENetListIterator tmp(*this);
        --(*this);
        return tmp;
    }

    bool operator==(const ENetIterator& other) const {
        return node == other.node;
    }

    bool operator!=(const ENetIterator& other) const {
        return node != other.node;
    }
   
};

class ENetList
{
public: 
    using iterator = ENetIterator<ENetListNode>;
    using const_iterator = ENetIterator<const ENetListNode>;

public:
    ENetList() {
        sentinel.next = &sentinel;
        sentinel.previous = &sentinel;
    }

    iterator begin() { return iterator(sentinel.next); }
    iterator end() { return iterator(&sentinel); }

    const_iterator begin() const { return const_iterator(sentinel.next); }
    const_iterator end()   const { return const_iterator(&sentinel); }


    bool empty()                { return begin() == end();  }
    ENetListNode* insert(iterator position, void* data);
    void* remove(iterator position);

    // positionµÚ¿¡ dataFirst -- dataLast¸¦ »ðÀÔ
    bool move(iterator position, void* dataFirst, void* dataLast);
    
	void	clear();
	size_t	size();

public:
	ENetListNode sentinel;
};

using ENetListIterator = ENetList::iterator;

void* enet_list_remove(ENetListIterator position);
ENetListIterator enet_list_insert(ENetListIterator position, void* data);

#define enet_list_begin(list) ((list) -> sentinel.next)
#define enet_list_end(list) (& (list) -> sentinel)

#define enet_list_empty(list) (enet_list_begin (list) == enet_list_end (list))

#define enet_list_next(iterator) ((iterator) -> next)
#define enet_list_previous(iterator) ((iterator) -> previous)

#define enet_list_front(list) ((void *) (list) -> sentinel.next)
#define enet_list_back(list) ((void *) (list) -> sentinel.previous)

#endif /* __ENET_LIST_H__ */