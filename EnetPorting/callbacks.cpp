#include "callbacks.h"
#include "enet.h"
#include <utility>

static ENetCallbacks callbacks = { malloc, free, abort };


// 나중에 메모리풀로 변경해보자,,,
// 일단 소유권 넘길거임
int enet_initialize_with_callbacks(ENetVersion version, ENetCallbacks* inits)
{
    if (version < ENET_VERSION_CREATE(1, 3, 0))
        return -1;

    if (inits->malloc != nullptr || inits->free != nullptr )
    {
        if (inits->malloc == nullptr || inits->free == nullptr)
            return -1;

        callbacks.malloc = std::move(inits->malloc);
        callbacks.free = std::move(inits->free);
    }

    if ( inits->no_memory != nullptr )
        callbacks.no_memory = std::move(inits->no_memory);

    return enet_initialize();
}

ENetVersion enet_linked_version(void)
{
    return ENET_VERSION;
}

void* enet_malloc(size_t size)
{
    void* memory = callbacks.malloc(size);

    if (memory == nullptr)
        callbacks.no_memory();

    return memory;
}

void enet_free(void* memory)
{
    callbacks.free(memory);
}