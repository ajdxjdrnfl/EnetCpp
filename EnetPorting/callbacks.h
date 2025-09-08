#pragma once

#include <stdlib.h>
#include <memory>

namespace ENetFunction
{
    template<typename Signature>
    class TFunction;

    template<typename... Args>
    class TFunction<void(Args...)>
    {
        class ICallable
        {
        public:
            virtual ~ICallable() {};
            virtual void Invoke(Args&&... args) = 0;
        };

        template<typename Callable>
        class TLambdaCallableImpl : public ICallable
        {
        public:
            TLambdaCallableImpl(Callable&& InFunc) : Func(std::move(InFunc)) {}

            virtual void Invoke(Args&&... args) override
            {
                Func(std::forward<Args>(args)...);
            }
            virtual ~TLambdaCallableImpl() {}

        private:
            Callable Func;
        };

        template<typename Class >
        class TMemberCallableImpl : public ICallable
        {
        public:
            using Method = Ret(Class::*)(Args...);

            TMemberCallableImpl(Class* InInstance, Method InFunc)
                : Instance(InInstance), Func(InFunc) {
            }

            Ret Invoke(Args&&... args) override
            {
                (Instance->*Func)(std::forward<Args>(args)...);
            }
            virtual ~TMemberCallableImpl() {}

        private:
            Class* Instance;
            Method Func;
        };

        class TStaticCallableImpl : public ICallable
        {
        public:
            TStaticCallableImpl(void(*InFunc)(Args...)) : Func(std::move(InFunc)) {}

            virtual void Invoke(Args&&... args) override
            {
                Func(std::forward<Args>(args)...);
            }
            virtual ~TStaticCallableImpl() {}

        private:
            void(*Func)(Args...);
        };


        std::unique_ptr<ICallable> Ptr;

    public:
        TFunction() = default;

        
        TFunction(void(*InFunc)(Args)...)
            : Ptr(std::make_unique<TStaticCallableImpl>(std::forward<TStaticCallableImpl>(InFunc)))
        {

        }
        // 람다
        template<typename Callable>
        TFunction(Callable&& InFunc)
            : Ptr(std::make_unique<TLambdaCallableImpl<Callable>>(std::forward<Callable>(InFunc)))
        {
        };

        // 멤버함수
        template<class Class>
        TFunction(Class* InInstance, void(Class::* InFunc)(Args...))
            : Ptr(std::make_unique<TMemberCallableImpl<Class>>(InInstance, InFunc))
        {
        };

        void operator()(Args... args)
        {
            Ptr->Invoke(std::forward<Args>(args)...);
        };

        bool operator!=(std::nullptr_t ptr) const
        {
            return Ptr != ptr;
        }

        bool operator==(std::nullptr_t ptr) const
        {
            return Ptr == ptr;
        }

        TFunction& operator=(std::nullptr_t ptr)
        {
            Ptr.reset();
            return *this;
        }

    public:
        // 일단 복사는 막고
        TFunction(const TFunction&) = default;
        TFunction& operator=(const TFunction&) = default;

        // 일단 소유권 이전은 가능하게
        TFunction(TFunction&&) noexcept = default;
        TFunction& operator=(TFunction&&) noexcept = default;
    };

    template<typename Ret, typename... Args>
    class TFunction<Ret(Args...)>
    {
        class ICallable
        {
        public:
            virtual ~ICallable() {};
            virtual Ret Invoke(Args&&... args) = 0;
        };

        template<typename Callable>
        class TLambdaCallableImpl : public ICallable
        {
        public:
            TLambdaCallableImpl(Callable&& InFunc) : Func(std::move(InFunc)) {}

            virtual Ret Invoke(Args&&... args) override
            {
                if constexpr (std::is_void_v<Ret>)
                {
                    Func(std::forward<Args>(args)...);
                }
                else
                {
                    return Func(std::forward<Args>(args)...);
                }
            }
            virtual ~TLambdaCallableImpl() { }

        private:
            Callable Func;
        };

        template<typename Class >
        class TMemberCallableImpl : public ICallable
        {
        public:
            using Method = Ret(Class::*)(Args...);

            TMemberCallableImpl(Class* InInstance, Method InFunc)
                : Instance(InInstance), Func(InFunc) {}

            Ret Invoke(Args&&... args) override
            {
                if constexpr (std::is_void_v<Ret>)
                {
                    (Instance->*Func)(std::forward<Args>(args)...);
                }
                else
                {
                    return (Instance->*Func)(std::forward<Args>(args)...);
                }
            }
            virtual ~TMemberCallableImpl() { }

        private:
            Class* Instance;
            Method Func;
        };


        std::unique_ptr<ICallable> Ptr;

    public:
        TFunction() = default;

        // 람다
        template<typename Callable>
        TFunction(Callable&& InFunc) 
            : Ptr(std::make_unique<TLambdaCallableImpl<Callable>>(std::forward<Callable>(InFunc)))
        {
        };

        // 멤버함수
        template<class Class>
        TFunction(Class* InInstance, Ret(Class::* InFunc)(Args...))
            : Ptr(std::make_unique<TMemberCallableImpl<Class>>(InInstance, InFunc))
        {
        };

        Ret operator()(Args... args)
        {
            return Ptr->Invoke(std::forward<Args>(args)...);
        };

        bool operator!=(std::nullptr_t ptr) const
        {
            return Ptr != ptr;
        }

        bool operator==(std::nullptr_t ptr) const
        {
            return Ptr == ptr;
        }

    public:
        // 일단 복사는 막고
        TFunction(const TFunction&) = default;
        TFunction& operator=(const TFunction&) = default;

        // 일단 소유권 이전은 가능하게
        TFunction(TFunction&&) noexcept = default;
        TFunction& operator=(TFunction&&) noexcept = default;
    };

};

struct ENetCallbacks
{
	ENetFunction::TFunction<void*(size_t)> malloc;
    ENetFunction::TFunction<void(void*)> free;
    ENetFunction::TFunction<void(void)> no_memory;
};

extern void*    enet_malloc(size_t size);
extern void     enet_free(void* memory);