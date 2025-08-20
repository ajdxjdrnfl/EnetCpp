#pragma once
#include <winsock2.h>
#include <assert.h>

#define ENET_API __declspec( dllexport )
#define ENET_HOST_TO_NET_32(value) (htonl (value))

#define IF_NULLPTR_THEN_CONTINUE(Target, Variable)	\
auto* Variable = Target;							\
if(Variable == nullptr)	continue;

#ifdef _DEBUG
	#define ASSERT(Expression) assert(Expression)			
#else
	#define ASSERT(Expression) ((void)0)
#endif
