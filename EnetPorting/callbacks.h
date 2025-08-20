#pragma once

#include <stdlib.h>

template<typename Ret ,typename... Args>
class EnetFunction
{
public:
    struct ICallable
    {
        virtual Ret Invoke(Args&&... args) = 0;
        virtual ICallable* Clone(void* Allocator) = 0;
        virtual void Destroy() = 0;
        virtual ~ICallable() {}
    };

    template<typename CallableType>
    struct TCallableImpl : ICallable
    {
        CallableType Callable;

        TCallableImpl(CallableType&& InCallable)
            : Callable(MoveTemp(InCallable)) {}

        virtual Ret Invoke(Args&&... args) override
        {
            return Callable(Forward<Args>(args)...);
        }

        virtual ICallable* Clone(void* Allocator) override
        {
            return new (Allocator) TCallableImpl<CallableType>(Callable);
        }

        virtual void Destroy() override
        {
            this->~TCallableImpl();
        }
    };

    ICallable* CallablePtr = nullptr;
};

template <typename MallocType, typename FreeType, typename NoMemoryType>
struct _EnetCallbacks
{
	MallocType malloc;
	FreeType free;
	NoMemoryType no_memory;
};

using ENetCallbacks = _EnetCallbacks<void*(*)(size_t), void(*)(void*), void(*)(void)>;

extern void*    enet_malloc(size_t);
extern void     enet_free(void*);