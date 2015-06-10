/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


// This file was generated by NxParameterized/scripts/GenParameterized.pl
// Created: 2015.01.18 19:26:21

#ifndef HEADER_ImpactEmitterAssetParameters_h
#define HEADER_ImpactEmitterAssetParameters_h

#include "NxParametersTypes.h"

#ifndef NX_PARAMETERIZED_ONLY_LAYOUTS
#include "NxParameterized.h"
#include "NxParameters.h"
#include "NxParameterizedTraits.h"
#include "NxTraitsInternal.h"
#endif

namespace physx
{
namespace apex
{
namespace emitter
{

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to __declspec(align())

namespace ImpactEmitterAssetParametersNS
{


struct REF_DynamicArray1D_Type
{
	NxParameterized::Interface** buf;
	bool isAllocated;
	physx::PxI32 elementSize;
	physx::PxI32 arraySizes[1];
};


struct ParametersStruct
{

	REF_DynamicArray1D_Type eventSetList;

};

static const physx::PxU32 checksum[] = { 0x5b78e967, 0xd4e18888, 0xaac77873, 0xef2e1cba, };

} // namespace ImpactEmitterAssetParametersNS

#ifndef NX_PARAMETERIZED_ONLY_LAYOUTS
class ImpactEmitterAssetParameters : public NxParameterized::NxParameters, public ImpactEmitterAssetParametersNS::ParametersStruct
{
public:
	ImpactEmitterAssetParameters(NxParameterized::Traits* traits, void* buf = 0, PxI32* refCount = 0);

	virtual ~ImpactEmitterAssetParameters();

	virtual void destroy();

	static const char* staticClassName(void)
	{
		return("ImpactEmitterAssetParameters");
	}

	const char* className(void) const
	{
		return(staticClassName());
	}

	static const physx::PxU32 ClassVersion = ((physx::PxU32)0 << 16) + (physx::PxU32)0;

	static physx::PxU32 staticVersion(void)
	{
		return ClassVersion;
	}

	physx::PxU32 version(void) const
	{
		return(staticVersion());
	}

	static const physx::PxU32 ClassAlignment = 8;

	static const physx::PxU32* staticChecksum(physx::PxU32& bits)
	{
		bits = 8 * sizeof(ImpactEmitterAssetParametersNS::checksum);
		return ImpactEmitterAssetParametersNS::checksum;
	}

	static void freeParameterDefinitionTable(NxParameterized::Traits* traits);

	const physx::PxU32* checksum(physx::PxU32& bits) const
	{
		return staticChecksum(bits);
	}

	const ImpactEmitterAssetParametersNS::ParametersStruct& parameters(void) const
	{
		ImpactEmitterAssetParameters* tmpThis = const_cast<ImpactEmitterAssetParameters*>(this);
		return *(static_cast<ImpactEmitterAssetParametersNS::ParametersStruct*>(tmpThis));
	}

	ImpactEmitterAssetParametersNS::ParametersStruct& parameters(void)
	{
		return *(static_cast<ImpactEmitterAssetParametersNS::ParametersStruct*>(this));
	}

	virtual NxParameterized::ErrorType getParameterHandle(const char* long_name, NxParameterized::Handle& handle) const;
	virtual NxParameterized::ErrorType getParameterHandle(const char* long_name, NxParameterized::Handle& handle);

	void initDefaults(void);

protected:

	virtual const NxParameterized::DefinitionImpl* getParameterDefinitionTree(void);
	virtual const NxParameterized::DefinitionImpl* getParameterDefinitionTree(void) const;


	virtual void getVarPtr(const NxParameterized::Handle& handle, void*& ptr, size_t& offset) const;

private:

	void buildTree(void);
	void initDynamicArrays(void);
	void initStrings(void);
	void initReferences(void);
	void freeDynamicArrays(void);
	void freeStrings(void);
	void freeReferences(void);

	static bool mBuiltFlag;
	static NxParameterized::MutexType mBuiltFlagMutex;
};

class ImpactEmitterAssetParametersFactory : public NxParameterized::Factory
{
	static const char* const vptr;

public:
	virtual NxParameterized::Interface* create(NxParameterized::Traits* paramTraits)
	{
		// placement new on this class using mParameterizedTraits

		void* newPtr = paramTraits->alloc(sizeof(ImpactEmitterAssetParameters), ImpactEmitterAssetParameters::ClassAlignment);
		if (!NxParameterized::IsAligned(newPtr, ImpactEmitterAssetParameters::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class ImpactEmitterAssetParameters");
			paramTraits->free(newPtr);
			return 0;
		}

		memset(newPtr, 0, sizeof(ImpactEmitterAssetParameters)); // always initialize memory allocated to zero for default values
		return NX_PARAM_PLACEMENT_NEW(newPtr, ImpactEmitterAssetParameters)(paramTraits);
	}

	virtual NxParameterized::Interface* finish(NxParameterized::Traits* paramTraits, void* bufObj, void* bufStart, physx::PxI32* refCount)
	{
		if (!NxParameterized::IsAligned(bufObj, ImpactEmitterAssetParameters::ClassAlignment)
		        || !NxParameterized::IsAligned(bufStart, ImpactEmitterAssetParameters::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class ImpactEmitterAssetParameters");
			return 0;
		}

		// Init NxParameters-part
		// We used to call empty constructor of ImpactEmitterAssetParameters here
		// but it may call default constructors of members and spoil the data
		NX_PARAM_PLACEMENT_NEW(bufObj, NxParameterized::NxParameters)(paramTraits, bufStart, refCount);

		// Init vtable (everything else is already initialized)
		*(const char**)bufObj = vptr;

		return (ImpactEmitterAssetParameters*)bufObj;
	}

	virtual const char* getClassName()
	{
		return (ImpactEmitterAssetParameters::staticClassName());
	}

	virtual physx::PxU32 getVersion()
	{
		return (ImpactEmitterAssetParameters::staticVersion());
	}

	virtual physx::PxU32 getAlignment()
	{
		return (ImpactEmitterAssetParameters::ClassAlignment);
	}

	virtual const physx::PxU32* getChecksum(physx::PxU32& bits)
	{
		return (ImpactEmitterAssetParameters::staticChecksum(bits));
	}
};
#endif // NX_PARAMETERIZED_ONLY_LAYOUTS

} // namespace emitter
} // namespace apex
} // namespace physx

#pragma warning(pop)

#endif
