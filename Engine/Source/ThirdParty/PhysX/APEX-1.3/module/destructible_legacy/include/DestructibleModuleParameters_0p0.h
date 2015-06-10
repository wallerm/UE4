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
// Created: 2015.01.18 19:26:28

#ifndef HEADER_DestructibleModuleParameters_0p0_h
#define HEADER_DestructibleModuleParameters_0p0_h

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

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to __declspec(align())

namespace DestructibleModuleParameters_0p0NS
{

struct GRBSettings_Type;

struct GRBSettings_Type
{
	physx::PxI32 gpuDeviceOrdinal;
	physx::PxF32 meshCellSize;
	physx::PxF32 skinWidth;
	physx::PxU32 nonPenSolverPosIterCount;
	physx::PxU32 frictionSolverPosIterCount;
	physx::PxU32 frictionSolverVelIterCount;
	physx::PxF32 maxLinAcceleration;
	physx::PxU32 gpuMemSceneSize;
	physx::PxU32 gpuMemTempDataSize;
};

struct ParametersStruct
{

	GRBSettings_Type gpuRigidBodySettings;
	physx::PxU32 maxDynamicChunkIslandCount;
	bool sortFIFOByBenefit;
	physx::PxF32 validBoundsPadding;
	physx::PxF32 maxChunkSeparationLOD;
	physx::PxU32 maxActorCreatesPerFrame;
	physx::PxU32 maxChunkDepthOffset;

};

static const physx::PxU32 checksum[] = { 0x8231bc89, 0x51f7449f, 0xc0e375f2, 0x4eeb99f7, };

} // namespace DestructibleModuleParameters_0p0NS

#ifndef NX_PARAMETERIZED_ONLY_LAYOUTS
class DestructibleModuleParameters_0p0 : public NxParameterized::NxParameters, public DestructibleModuleParameters_0p0NS::ParametersStruct
{
public:
	DestructibleModuleParameters_0p0(NxParameterized::Traits* traits, void* buf = 0, PxI32* refCount = 0);

	virtual ~DestructibleModuleParameters_0p0();

	virtual void destroy();

	static const char* staticClassName(void)
	{
		return("DestructibleModuleParameters");
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
		bits = 8 * sizeof(DestructibleModuleParameters_0p0NS::checksum);
		return DestructibleModuleParameters_0p0NS::checksum;
	}

	static void freeParameterDefinitionTable(NxParameterized::Traits* traits);

	const physx::PxU32* checksum(physx::PxU32& bits) const
	{
		return staticChecksum(bits);
	}

	const DestructibleModuleParameters_0p0NS::ParametersStruct& parameters(void) const
	{
		DestructibleModuleParameters_0p0* tmpThis = const_cast<DestructibleModuleParameters_0p0*>(this);
		return *(static_cast<DestructibleModuleParameters_0p0NS::ParametersStruct*>(tmpThis));
	}

	DestructibleModuleParameters_0p0NS::ParametersStruct& parameters(void)
	{
		return *(static_cast<DestructibleModuleParameters_0p0NS::ParametersStruct*>(this));
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

class DestructibleModuleParameters_0p0Factory : public NxParameterized::Factory
{
	static const char* const vptr;

public:
	virtual NxParameterized::Interface* create(NxParameterized::Traits* paramTraits)
	{
		// placement new on this class using mParameterizedTraits

		void* newPtr = paramTraits->alloc(sizeof(DestructibleModuleParameters_0p0), DestructibleModuleParameters_0p0::ClassAlignment);
		if (!NxParameterized::IsAligned(newPtr, DestructibleModuleParameters_0p0::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class DestructibleModuleParameters_0p0");
			paramTraits->free(newPtr);
			return 0;
		}

		memset(newPtr, 0, sizeof(DestructibleModuleParameters_0p0)); // always initialize memory allocated to zero for default values
		return NX_PARAM_PLACEMENT_NEW(newPtr, DestructibleModuleParameters_0p0)(paramTraits);
	}

	virtual NxParameterized::Interface* finish(NxParameterized::Traits* paramTraits, void* bufObj, void* bufStart, physx::PxI32* refCount)
	{
		if (!NxParameterized::IsAligned(bufObj, DestructibleModuleParameters_0p0::ClassAlignment)
		        || !NxParameterized::IsAligned(bufStart, DestructibleModuleParameters_0p0::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class DestructibleModuleParameters_0p0");
			return 0;
		}

		// Init NxParameters-part
		// We used to call empty constructor of DestructibleModuleParameters_0p0 here
		// but it may call default constructors of members and spoil the data
		NX_PARAM_PLACEMENT_NEW(bufObj, NxParameterized::NxParameters)(paramTraits, bufStart, refCount);

		// Init vtable (everything else is already initialized)
		*(const char**)bufObj = vptr;

		return (DestructibleModuleParameters_0p0*)bufObj;
	}

	virtual const char* getClassName()
	{
		return (DestructibleModuleParameters_0p0::staticClassName());
	}

	virtual physx::PxU32 getVersion()
	{
		return (DestructibleModuleParameters_0p0::staticVersion());
	}

	virtual physx::PxU32 getAlignment()
	{
		return (DestructibleModuleParameters_0p0::ClassAlignment);
	}

	virtual const physx::PxU32* getChecksum(physx::PxU32& bits)
	{
		return (DestructibleModuleParameters_0p0::staticChecksum(bits));
	}
};
#endif // NX_PARAMETERIZED_ONLY_LAYOUTS

} // namespace apex
} // namespace physx

#pragma warning(pop)

#endif
