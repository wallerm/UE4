// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.


#include "UnrealEd.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Animation/SkeletalMeshActor.h"
#include "EditorSupportDelegates.h"
#include "SoundDefinitions.h"
#include "CameraController.h"
#include "MouseDeltaTracker.h"
#include "ScopedTransaction.h"
#include "HModel.h"
#include "BSPOps.h"
#include "LevelUtils.h"
#include "Layers/ILayers.h"
#include "../Private/StaticLightingSystem/StaticLightingPrivate.h"
#include "EditorLevelUtils.h"
#include "Engine.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Editor/LevelEditor/Public/LevelViewportActions.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "AssetSelection.h"
#include "BlueprintUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Collision.h"
#include "StaticMeshResources.h"
#include "AssetRegistryModule.h"
#include "IPlacementModeModule.h"
#include "Editor/GeometryMode/Public/EditorGeometry.h"
#include "ActorEditorUtils.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Editor/Matinee/Public/IMatinee.h"
#include "Editor/Matinee/Public/MatineeConstants.h"
#include "MainFrame.h"
#include "SnappingUtils.h"
#include "LevelViewportClickHandlers.h"
#include "DragTool_BoxSelect.h"
#include "DragTool_FrustumSelect.h"
#include "DragTool_Measure.h"
#include "LevelEditorActions.h"
#include "BrushBuilderDragDropOp.h"
#include "AssetRegistryModule.h"
#include "Animation/VertexAnim/VertexAnimation.h"
#include "InstancedFoliage.h"
#include "DynamicMeshBuilder.h"
#include "Editor/ActorPositioning.h"
#include "NotificationManager.h"
#include "SNotificationList.h"
#include "SLevelViewport.h"

DEFINE_LOG_CATEGORY(LogEditorViewport);

#define LOCTEXT_NAMESPACE "LevelEditorViewportClient"

static const float MIN_ACTOR_BOUNDS_EXTENT	= 1.0f;

TArray< TWeakObjectPtr< AActor > > FLevelEditorViewportClient::DropPreviewActors;

/** Static: List of objects we're hovering over */
TSet<FViewportHoverTarget> FLevelEditorViewportClient::HoveredObjects;

IMPLEMENT_HIT_PROXY( HLevelSocketProxy, HHitProxy );


///////////////////////////////////////////////////////////////////////////////////////////////////
//
//	FViewportCursorLocation
//	Contains information about a mouse cursor position within a viewport, transformed into the correct
//	coordinate system for the viewport.
//
///////////////////////////////////////////////////////////////////////////////////////////////////
FViewportCursorLocation::FViewportCursorLocation( const FSceneView* View, FEditorViewportClient* InViewportClient, int32 X, int32 Y )
	:	Origin(ForceInit), Direction(ForceInit), CursorPos(X, Y)
{

	FVector4 ScreenPos = View->PixelToScreen(X, Y, 0);

	const FMatrix InvViewMatrix = View->ViewMatrices.GetInvViewMatrix();
	const FMatrix InvProjMatrix = View->ViewMatrices.GetInvProjMatrix();

	const float ScreenX = ScreenPos.X;
	const float ScreenY = ScreenPos.Y;

	ViewportClient = InViewportClient;

	if ( ViewportClient->IsPerspective() )
	{
		Origin = View->ViewMatrices.ViewOrigin;
		Direction = InvViewMatrix.TransformVector(FVector(InvProjMatrix.TransformFVector4(FVector4(ScreenX * GNearClippingPlane,ScreenY * GNearClippingPlane,0.0f,GNearClippingPlane)))).GetSafeNormal();
	}
	else
	{
		Origin = InvViewMatrix.TransformFVector4(InvProjMatrix.TransformFVector4(FVector4(ScreenX,ScreenY,0.5f,1.0f)));
		Direction = InvViewMatrix.TransformVector(FVector(0,0,1)).GetSafeNormal();
	}
}

ELevelViewportType FViewportCursorLocation::GetViewportType() const
{
	return ViewportClient->GetViewportType();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//
//	FViewportClick::FViewportClick - Calculates useful information about a click for the below ClickXXX functions to use.
//
///////////////////////////////////////////////////////////////////////////////////////////////////
FViewportClick::FViewportClick(const FSceneView* View,FEditorViewportClient* ViewportClient,FKey InKey,EInputEvent InEvent,int32 X,int32 Y)
	:	FViewportCursorLocation(View, ViewportClient, X, Y)
	,	Key(InKey), Event(InEvent)
{
	ControlDown = ViewportClient->IsCtrlPressed();
	ShiftDown = ViewportClient->IsShiftPressed();
	AltDown = ViewportClient->IsAltPressed();
}

/** Helper function to compute a new location that is snapped to the origin plane given the users cursor location and camera angle */
static FVector4 AttemptToSnapLocationToOriginPlane( const FViewportCursorLocation& Cursor, FVector4 Location )
{
	ELevelViewportType ViewportType = Cursor.GetViewportType();
	if ( ViewportType == LVT_Perspective )
	{
		FVector CamPos = Cursor.GetViewportClient()->GetViewLocation();

		FVector NewLocFloor( Location.X, Location.Y, 0 );

		bool CamBelowOrigin = CamPos.Z < 0;

		FPlane CamPlane( CamPos, FVector::UpVector );
		// If the camera is looking at the floor place the brush on the floor
		if ( !CamBelowOrigin && CamPlane.PlaneDot( Location ) < 0 )
		{
			Location = NewLocFloor;
		}
		else if ( CamBelowOrigin && CamPlane.PlaneDot( Location ) > 0 )
		{
			Location = NewLocFloor;
		}
	}
	else if ( ViewportType == LVT_OrthoXY )
	{
		// In ortho place the brush at the origin of the hidden axis
		Location.Z = 0;
	}
	else if ( ViewportType == LVT_OrthoXZ )
	{
		// In ortho place the brush at the origin of the hidden axis
		Location.Y = 0;
	}
	else if ( ViewportType == LVT_OrthoYZ )
	{
		// In ortho place the brush at the origin of the hidden axis
		Location.X = 0;
	}

	return Location;
}

/**
 * Helper function that attempts to use the provided object/asset to create an actor to place.
 *
 * @param	InLevel			Level in which to drop actor
 * @param	ObjToUse		Asset to attempt to use for an actor to place
 * @param	CursorLocation	Location of the cursor while dropping
 * @param	bSelectActors	If true, select the newly dropped actors (defaults: true)
 * @param	ObjectFlags		The flags to place on the actor when it is spawned
 * @param	FactoryToUse	The preferred actor factory to use (optional)
 *
 * @return	true if the object was successfully used to place an actor; false otherwise
 */
static TArray<AActor*> AttemptDropObjAsActors( ULevel* InLevel, UObject* ObjToUse, const FViewportCursorLocation& CursorLocation, bool bSelectActors, EObjectFlags ObjectFlags, UActorFactory* FactoryToUse, const FName Name = NAME_None )
{
	TArray<AActor*> PlacedActors;

	UClass* ObjectClass = Cast<UClass>(ObjToUse);

	if ( ObjectClass == NULL )
	{
		ObjectClass = ObjToUse->GetClass();
	}

	AActor* PlacedActor = NULL;
	if ( ObjectClass != NULL && ObjectClass->IsChildOf( AActor::StaticClass() ) )
	{
		//Attempting to drop a UClass object
		UActorFactory* ActorFactory = FactoryToUse;
		if ( ActorFactory == NULL )
		{
			ActorFactory = GEditor->FindActorFactoryForActorClass( ObjectClass );
		}

		if ( ActorFactory != NULL )
		{
			PlacedActor = FActorFactoryAssetProxy::AddActorFromSelection( ObjectClass, NULL, bSelectActors, ObjectFlags, ActorFactory, Name );
		}

		if ( PlacedActor == NULL && ActorFactory != NULL )
		{
			PlacedActor = FActorFactoryAssetProxy::AddActorForAsset( ObjToUse, bSelectActors, ObjectFlags, ActorFactory, Name );
		}
		
		if ( PlacedActor == NULL && !ObjectClass->HasAnyClassFlags(CLASS_NotPlaceable | CLASS_Abstract) )
		{
			// If no actor factory was found or failed, add the actor directly.
			const FTransform ActorTransform = FActorPositioning::GetCurrentViewportPlacementTransform(*ObjectClass->GetDefaultObject<AActor>());
			PlacedActor = GEditor->AddActor( InLevel, ObjectClass, ActorTransform, /*bSilent=*/false, ObjectFlags );
		}

		if ( PlacedActor != NULL )
		{
			FVector Collision = ObjectClass->GetDefaultObject<AActor>()->GetPlacementExtent();
			PlacedActors.Add(PlacedActor);
		}
	}
	
	if ( (NULL == PlacedActor) && ObjToUse->IsA( UExportTextContainer::StaticClass() ) )
	{
		UExportTextContainer* ExportContainer = CastChecked<UExportTextContainer>(ObjToUse);
		const TArray<AActor*> NewActors = GEditor->AddExportTextActors( ExportContainer->ExportText, /*bSilent*/false, ObjectFlags);
		PlacedActors.Append(NewActors);
	}
	else if ( (NULL == PlacedActor) && ObjToUse->IsA( UBrushBuilder::StaticClass() ) )
	{
		UBrushBuilder* BrushBuilder = CastChecked<UBrushBuilder>(ObjToUse);
		UWorld* World = InLevel->OwningWorld;
		BrushBuilder->Build(World);

		ABrush* DefaultBrush = World->GetDefaultBrush();
		if (DefaultBrush != NULL)
		{
			FVector ActorLoc = GEditor->ClickLocation + GEditor->ClickPlane * (FVector::BoxPushOut(GEditor->ClickPlane, DefaultBrush->GetPlacementExtent()));
			FSnappingUtils::SnapPointToGrid(ActorLoc, FVector::ZeroVector);

			DefaultBrush->SetActorLocation(ActorLoc);
			PlacedActor = DefaultBrush;
			PlacedActors.Add(DefaultBrush);
		}
	}
	else if (NULL == PlacedActor)
	{
		bool bPlace = true;
		if (ObjectClass->IsChildOf(UBlueprint::StaticClass()))
		{
			UBlueprint* BlueprintObj = StaticCast<UBlueprint*>(ObjToUse);
			bPlace = BlueprintObj->GeneratedClass != NULL;
			if(bPlace)
			{
				check(BlueprintObj->ParentClass == BlueprintObj->GeneratedClass->GetSuperClass());
				if (BlueprintObj->GeneratedClass->HasAnyClassFlags(CLASS_NotPlaceable | CLASS_Abstract))
				{
					bPlace = false;
				}
			}
		}

		if (bPlace)
		{
			PlacedActor = FActorFactoryAssetProxy::AddActorForAsset( ObjToUse, bSelectActors, ObjectFlags, FactoryToUse, Name );
			if ( PlacedActor != NULL )
			{
				PlacedActors.Add(PlacedActor);
				PlacedActor->PostEditMove(true);
			}
		}
	}

	return PlacedActors;
}

namespace EMaterialKind
{
	enum Type
	{
		Unknown = 0,
		Base,
		Normal,
		Specular,
		Emissive,
	};
}

static FString GetSharedTextureNameAndKind( FString TextureName, EMaterialKind::Type& Kind)
{
	// Try and strip the suffix from the texture name, if we're successful it must be of that type.
	bool hasBaseSuffix = TextureName.RemoveFromEnd( "_D" ) || TextureName.RemoveFromEnd( "_Diff" ) || TextureName.RemoveFromEnd( "_Diffuse" ) || TextureName.RemoveFromEnd( "_Detail" ) || TextureName.RemoveFromEnd( "_Base" );
	if ( hasBaseSuffix )
	{
		Kind = EMaterialKind::Base;
		return TextureName;
	}

	bool hasNormalSuffix = TextureName.RemoveFromEnd( "_N" ) || TextureName.RemoveFromEnd( "_Norm" ) || TextureName.RemoveFromEnd( "_Normal" );
	if ( hasNormalSuffix )
	{
		Kind = EMaterialKind::Normal;
		return TextureName;
	}
	
	bool hasSpecularSuffix = TextureName.RemoveFromEnd( "_S" ) || TextureName.RemoveFromEnd( "_Spec" ) || TextureName.RemoveFromEnd( "_Specular" );
	if ( hasSpecularSuffix )
	{
		Kind = EMaterialKind::Specular;
		return TextureName;
	}

	bool hasEmissiveSuffix = TextureName.RemoveFromEnd( "_E" ) || TextureName.RemoveFromEnd( "_Emissive" );
	if ( hasEmissiveSuffix )
	{
		Kind = EMaterialKind::Emissive;
		return TextureName;
	}

	Kind = EMaterialKind::Unknown;
	return TextureName;
}

static UTexture* GetTextureWithNameVariations( const FString& BasePackageName, const TArray<FString>& Suffixes )
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( TEXT( "AssetRegistry" ) );

	// Try all the variations of suffixes, if we find a package matching the suffix, return it.
	for ( int i = 0; i < Suffixes.Num(); i++ )
	{
		TArray<FAssetData> OutAssetData;
		if ( AssetRegistryModule.Get().GetAssetsByPackageName( *( BasePackageName + Suffixes[i] ), OutAssetData ) && OutAssetData.Num() > 0 )
		{
			if ( OutAssetData[0].AssetClass == "Texture2D" )
			{
				return Cast<UTexture>(OutAssetData[0].GetAsset());
			}
		}
	}

	return nullptr;
}

static bool TryAndCreateMaterialInput( UMaterial* UnrealMaterial, EMaterialKind::Type TextureKind, UTexture* UnrealTexture, FExpressionInput& MaterialInput, int X, int Y )
{
	// Ignore null textures.
	if ( UnrealTexture == nullptr )
	{
		return false;
	}

	bool bSetupAsNormalMap = UnrealTexture->IsNormalMap();

	// Create a new texture sample expression, this is our texture input node into the material output.
	UMaterialExpressionTextureSample* UnrealTextureExpression = ConstructObject<UMaterialExpressionTextureSample>( UMaterialExpressionTextureSample::StaticClass(), UnrealMaterial );
	UnrealMaterial->Expressions.Add( UnrealTextureExpression );
	MaterialInput.Expression = UnrealTextureExpression;
	UnrealTextureExpression->Texture = UnrealTexture;
	UnrealTextureExpression->SamplerType = bSetupAsNormalMap ? SAMPLERTYPE_Normal : SAMPLERTYPE_Color;
	UnrealTextureExpression->MaterialExpressionEditorX += X;
	UnrealTextureExpression->MaterialExpressionEditorY += Y;

	// If we know for a fact this is a normal map, it can only legally be placed in the normal map slot.
	// Ignore the Material kind for, but for everything else try and match it to the right slot, fallback
	// to the BaseColor if we don't know.
	if ( !bSetupAsNormalMap )
	{
		if ( TextureKind == EMaterialKind::Base )
		{
			UnrealMaterial->BaseColor.Expression = UnrealTextureExpression;
		}
		else if ( TextureKind == EMaterialKind::Specular )
		{
			UnrealMaterial->Specular.Expression = UnrealTextureExpression;
		}
		else if ( TextureKind == EMaterialKind::Emissive )
		{
			UnrealMaterial->EmissiveColor.Expression = UnrealTextureExpression;
		}
		else
		{
			UnrealMaterial->BaseColor.Expression = UnrealTextureExpression;
		}
	}
	else
	{
		UnrealMaterial->Normal.Expression = UnrealTextureExpression;
	}

	return true;
}

static UObject* GetOrCreateMaterialFromTexture( UTexture* UnrealTexture )
{
	FString TextureShortName = FPackageName::GetShortName( UnrealTexture->GetOutermost()->GetName() );

	// See if we can figure out what kind of material it is, based on a suffix, like _S for Specular, _D for Base/Detail/Diffuse.
	// if it can determine which type of texture it was, it will return the base name of the texture minus the suffix.
	EMaterialKind::Type MaterialKind;
	TextureShortName = GetSharedTextureNameAndKind( TextureShortName, MaterialKind );
	
	FString MaterialFullName = TextureShortName + "_Mat";
	FString NewPackageName = FPackageName::GetLongPackagePath( UnrealTexture->GetOutermost()->GetName() ) + TEXT( "/" ) + MaterialFullName;
	NewPackageName = PackageTools::SanitizePackageName( NewPackageName );
	UPackage* Package = CreatePackage( NULL, *NewPackageName );

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>( TEXT( "AssetRegistry" ) );

	// See if the material asset already exists with the expected name, if it does, just return
	// an instance of it.
	TArray<FAssetData> OutAssetData;
	if ( AssetRegistryModule.Get().GetAssetsByPackageName( *NewPackageName, OutAssetData ) && OutAssetData.Num() > 0 )
	{
		// TODO Check if is material?
		return OutAssetData[0].GetAsset();
	}

	// create an unreal material asset
	UMaterialFactoryNew* MaterialFactory = new UMaterialFactoryNew( FObjectInitializer() );

	UMaterial* UnrealMaterial = (UMaterial*)MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, *MaterialFullName, RF_Standalone | RF_Public, NULL, GWarn );

	const int HSpace = -300;

	// If we were able to figure out the material kind, we need to try and build a complex material
	// involving multiple textures.  If not, just try and connect what we found to the base map.
	if ( MaterialKind == EMaterialKind::Unknown )
	{
		TryAndCreateMaterialInput( UnrealMaterial, EMaterialKind::Base, UnrealTexture, UnrealMaterial->BaseColor, HSpace, 0 );
	}
	else
	{
		// Variations for Base Maps.
		TArray<FString> BaseSuffixes;
		BaseSuffixes.Add( "_D" );
		BaseSuffixes.Add( "_Diff" );
		BaseSuffixes.Add( "_Diffuse" );
		BaseSuffixes.Add( "_Detail" );
		BaseSuffixes.Add( "_Base" );

		// Variations for Normal Maps.
		TArray<FString> NormalSuffixes;
		NormalSuffixes.Add( "_N" );
		NormalSuffixes.Add( "_Norm" );
		NormalSuffixes.Add( "_Normal" );

		// Variations for Specular Maps.
		TArray<FString> SpecularSuffixes;
		SpecularSuffixes.Add( "_S" );
		SpecularSuffixes.Add( "_Spec" );
		SpecularSuffixes.Add( "_Specular" );

		// Variations for Emissive Maps.
		TArray<FString> EmissiveSuffixes;
		EmissiveSuffixes.Add( "_E" );
		EmissiveSuffixes.Add( "_Emissive" );

		// The asset path for the base texture, we need this to try and append different suffixes to to find
		// other textures we can use.
		FString BaseTexturePackage = FPackageName::GetLongPackagePath( UnrealTexture->GetOutermost()->GetName() ) + TEXT( "/" ) + TextureShortName;

		// Try and find different variations
		UTexture* BaseTexture = GetTextureWithNameVariations( BaseTexturePackage, BaseSuffixes );
		UTexture* NormalTexture = GetTextureWithNameVariations( BaseTexturePackage, NormalSuffixes );
		UTexture* SpecularTexture = GetTextureWithNameVariations( BaseTexturePackage, SpecularSuffixes );
		UTexture* EmissiveTexture = GetTextureWithNameVariations( BaseTexturePackage, EmissiveSuffixes );

		// Connect and layout any textures we find into their respective inputs in the material.
		const int VSpace = 170;
		TryAndCreateMaterialInput( UnrealMaterial, EMaterialKind::Base, BaseTexture, UnrealMaterial->BaseColor, HSpace, VSpace * -1 );
		TryAndCreateMaterialInput( UnrealMaterial, EMaterialKind::Specular, SpecularTexture, UnrealMaterial->Specular, HSpace, VSpace * 0 );
		TryAndCreateMaterialInput( UnrealMaterial, EMaterialKind::Emissive, EmissiveTexture, UnrealMaterial->EmissiveColor, HSpace, VSpace * 1 );
		TryAndCreateMaterialInput( UnrealMaterial, EMaterialKind::Normal, NormalTexture, UnrealMaterial->Normal, HSpace, VSpace * 2 );
	}

	if ( UnrealMaterial != NULL )
	{
		// Notify the asset registry
		FAssetRegistryModule::AssetCreated( UnrealMaterial );

		// Set the dirty flag so this package will get saved later
		Package->SetDirtyFlag( true );
	}

	UnrealMaterial->ForceRecompileForRendering();

	// Warn users that a new material has been created
	FNotificationInfo Info( FText::Format( LOCTEXT( "DropTextureMaterialCreated", "Material '{0}' Created" ), FText::FromString(MaterialFullName)) );
	Info.ExpireDuration = 4.0f;
	Info.bUseLargeFont = true;
	Info.bUseSuccessFailIcons = false;
	Info.Image = FEditorStyle::GetBrush( "ClassThumbnail.Material" );
	FSlateNotificationManager::Get().AddNotification( Info );

	return UnrealMaterial;
}

/**
 * Helper function that attempts to apply the supplied object to the supplied actor.
 *
 * @param	ObjToUse				Object to attempt to apply as specific asset
 * @param	ActorToApplyTo			Actor to whom the asset should be applied
 * @param   TargetMaterialSlot      When dealing with submeshes this will represent the target section/slot to apply materials to.
 * @param	bTest					Whether to test if the object would be successfully applied without actually doing it.
 * @param	bCreateDropPreview		Whether this is just a drop preview.
 *
 * @return	true if the provided object was successfully applied to the provided actor
 */
static bool AttemptApplyObjToActor( UObject* ObjToUse, AActor* ActorToApplyTo, int32 TargetMaterialSlot = -1, bool bTest = false, bool bCreateDropPreview = false )
{
	bool bResult = false;

	if ( ActorToApplyTo )
	{
		UTexture* DroppedObjAsTexture = Cast<UTexture>( ObjToUse );
		if ( DroppedObjAsTexture != NULL )
		{
			if ( bTest || bCreateDropPreview )
			{
				bResult = true;
			}
			else
			{
				ObjToUse = GetOrCreateMaterialFromTexture( DroppedObjAsTexture );
			}
		}

		// Ensure the provided object is some form of material
		UMaterialInterface* DroppedObjAsMaterial = Cast<UMaterialInterface>( ObjToUse );
		if ( DroppedObjAsMaterial )
		{
			if (bTest || bCreateDropPreview)
			{
				bResult = true;
			}
			else
			{
				// Apply the material to the actor
				FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "DragDrop_Transaction_ApplyMaterialToActor", "Apply Material to Actor") );
				bResult = FActorFactoryAssetProxy::ApplyMaterialToActor( ActorToApplyTo, DroppedObjAsMaterial, TargetMaterialSlot );
			}
		}

		USkeletalMesh* DroppedObjAsSkeletalMesh = Cast<USkeletalMesh>( ObjToUse );
		USkeleton* DroppedObjAsSkeleton = Cast<USkeleton>( ObjToUse );
		if ( DroppedObjAsSkeletalMesh ||
			 DroppedObjAsSkeleton )
		{
			if ( bTest )
			{
				if ( ActorToApplyTo->IsA(ASkeletalMeshActor::StaticClass()) )
				{
					bResult = true;
				}
			}
			else
			{
				if ( ASkeletalMeshActor* SkelMeshActor = Cast<ASkeletalMeshActor>(ActorToApplyTo) )
				{
					const FScopedTransaction Transaction( LOCTEXT( "DropSkelMeshOnObject", "Drop Skeletal Mesh On Object" ) );
					USkeletalMeshComponent* SkelMeshComponent = SkelMeshActor->GetSkeletalMeshComponent();
					SkelMeshComponent->Modify();
					if ( DroppedObjAsSkeletalMesh )
					{
						SkelMeshComponent->SetSkeletalMesh(DroppedObjAsSkeletalMesh);
					}
					else if ( DroppedObjAsSkeleton )
					{
						SkelMeshComponent->SetSkeletalMesh(DroppedObjAsSkeleton->GetPreviewMesh(true));
					}
					bResult = true;
				}
			}
		}

		UAnimBlueprint* DroppedObjAsAnimBlueprint = Cast<UAnimBlueprint>( ObjToUse );
		if ( DroppedObjAsAnimBlueprint )
		{
			USkeleton* NeedsSkeleton = DroppedObjAsAnimBlueprint->TargetSkeleton;
			if ( NeedsSkeleton )
			{
				if(bTest)
				{
					if(ActorToApplyTo->IsA(ASkeletalMeshActor::StaticClass()))
					{
						bResult = true;
					}
				}
				else
				{
					if(ASkeletalMeshActor* SkelMeshActor = Cast<ASkeletalMeshActor>(ActorToApplyTo))
					{
						const FScopedTransaction Transaction(LOCTEXT("DropAnimBlueprintOnObject", "Drop Anim Blueprint On Object"));

						USkeletalMeshComponent* SkelMeshComponent = SkelMeshActor->GetSkeletalMeshComponent();
						// if anim blueprint skeleton and mesh skeleton does not match or component does not have any mesh, then change mesh
						bool bShouldChangeMesh = (SkelMeshComponent->SkeletalMesh == NULL ||
								!NeedsSkeleton->IsCompatible(SkelMeshComponent->SkeletalMesh->Skeleton));

						if(bShouldChangeMesh)
						{
							SkelMeshComponent->SetSkeletalMesh(NeedsSkeleton->GetPreviewMesh(true));
						}

						// make sure if it's compabile now, if not we're not going to change anim blueprint
						if(SkelMeshComponent->SkeletalMesh &&
							NeedsSkeleton->IsCompatible(SkelMeshComponent->SkeletalMesh->Skeleton))
						{
							SkelMeshComponent->SetAnimInstanceClass(DroppedObjAsAnimBlueprint->GeneratedClass);
							bResult = true;
						}
					}
				}
			}
		}

		UAnimationAsset* DroppedObjAsAnimationAsset = Cast<UAnimationAsset>( ObjToUse );
		UVertexAnimation* DroppedObjAsVertexAnimation = Cast<UVertexAnimation>( ObjToUse );
		// block anything else than just anim sequence
		if( DroppedObjAsAnimationAsset != NULL )
		{
			if( ! DroppedObjAsAnimationAsset->IsA(UAnimSequenceBase::StaticClass()) )
			{
				DroppedObjAsAnimationAsset = NULL;
			}
		}

		if ( DroppedObjAsAnimationAsset ||
			 DroppedObjAsVertexAnimation)
		{
			USkeleton* NeedsSkeleton = DroppedObjAsAnimationAsset? DroppedObjAsAnimationAsset->GetSkeleton() :
				(DroppedObjAsVertexAnimation && DroppedObjAsVertexAnimation->BaseSkelMesh? DroppedObjAsVertexAnimation->BaseSkelMesh->Skeleton : NULL);

			if (NeedsSkeleton)
			{
				if(bTest)
				{
					if(ActorToApplyTo->IsA(ASkeletalMeshActor::StaticClass()))
					{
						bResult = true;
					}
				}
				else
				{
					if(ASkeletalMeshActor* SkelMeshActor = Cast<ASkeletalMeshActor>(ActorToApplyTo))
					{
						const FScopedTransaction Transaction(LOCTEXT("DropAnimationOnObject", "Drop Animation On Object"));
						USkeletalMeshComponent* SkelComponent = SkelMeshActor->GetSkeletalMeshComponent();
						SkelComponent->Modify();
						// if asset skeleton and mesh skeleton does not match or component does not have any mesh, then change mesh
						bool bShouldChangeMesh = SkelComponent->SkeletalMesh == NULL || 
							!NeedsSkeleton->IsCompatible(SkelComponent->SkeletalMesh->Skeleton);

						if(bShouldChangeMesh)
						{
							SkelComponent->SetSkeletalMesh(NeedsSkeleton->GetAssetPreviewMesh(DroppedObjAsAnimationAsset));
						}

						if(DroppedObjAsAnimationAsset)
						{
							SkelComponent->SetAnimationMode(EAnimationMode::Type::AnimationSingleNode);
							SkelComponent->AnimationData.AnimToPlay = DroppedObjAsAnimationAsset;

							// set runtime data
							SkelComponent->SetAnimation(DroppedObjAsAnimationAsset);
						}
						if(DroppedObjAsVertexAnimation)
						{
							SkelComponent->SetAnimationMode(EAnimationMode::Type::AnimationSingleNode);
							SkelComponent->AnimationData.VertexAnimToPlay = DroppedObjAsVertexAnimation;

							// set runtime data
							SkelComponent->SetVertexAnimation(DroppedObjAsVertexAnimation);
						}
						if(SkelComponent && SkelComponent->SkeletalMesh)
						{
							bResult = true;
							SkelComponent->InitAnim(true);
						}
					}
				}
			}
		}
		
		// Notification hook for dropping asset onto actor
		if(!bTest)
		{
			FEditorDelegates::OnApplyObjectToActor.Broadcast(ObjToUse, ActorToApplyTo);
		}
	}

	return bResult;
}


/**
 * Helper function that attempts to apply the supplied object as a material to the BSP surface specified by the
 * provided model and index.
 *
 * @param	ObjToUse				Object to attempt to apply as a material
 * @param	ModelHitProxy			Hit proxy containing the relevant model
 * @param	SurfaceIndex			The index in the model's surface array of the relevant
 *
 * @return	true if the supplied object was successfully applied to the specified BSP surface
 */
bool FLevelEditorViewportClient::AttemptApplyObjAsMaterialToSurface( UObject* ObjToUse, HModel* ModelHitProxy, FViewportCursorLocation& Cursor )
{
	bool bResult = false;

	UTexture* DroppedObjAsTexture = Cast<UTexture>( ObjToUse );
	if ( DroppedObjAsTexture != NULL )
	{
		ObjToUse = GetOrCreateMaterialFromTexture( DroppedObjAsTexture );
	}

	// Ensure the dropped object is a material
	UMaterialInterface* DroppedObjAsMaterial = Cast<UMaterialInterface>( ObjToUse );

	if ( DroppedObjAsMaterial && ModelHitProxy )
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Viewport, 
			GetScene(),
			EngineShowFlags)
			.SetRealtimeUpdate( IsRealtime() ));
		FSceneView* View = CalcSceneView( &ViewFamily );


		UModel* Model = ModelHitProxy->GetModel();
	
		TArray<uint32> SelectedSurfaces;

		bool bDropedOntoSelectedSurface = false;
		const int32 DropX = Cursor.GetCursorPos().X;
		const int32 DropY = Cursor.GetCursorPos().Y;

		{
		uint32 SurfaceIndex;
		ModelHitProxy->ResolveSurface(View, DropX, DropY, SurfaceIndex);
		if (SurfaceIndex != INDEX_NONE)
		{
			if ((Model->Surfs[SurfaceIndex].PolyFlags & PF_Selected) == 0)
			{
				// Surface was not selected so only apply to this surface
				SelectedSurfaces.Add(SurfaceIndex);
			}
			else
			{
				bDropedOntoSelectedSurface = true;
			}
		}
		}

		if( bDropedOntoSelectedSurface )
		{
			for (int32 SurfaceIndex = 0; SurfaceIndex < Model->Surfs.Num(); ++SurfaceIndex)
			{
				FBspSurf& Surf = Model->Surfs[SurfaceIndex];

				if (Surf.PolyFlags & PF_Selected)
				{
					SelectedSurfaces.Add(SurfaceIndex);
				}
			}
		}

		if( SelectedSurfaces.Num() )
		{
			
			// Apply the material to the specified surface
			FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "DragDrop_Transaction_ApplyMaterialToSurface", "Apply Material to Surface"));

			// Modify the component so that PostEditUndo can reregister the model after undo
			ModelHitProxy->GetModelComponent()->Modify();

			for( int32 SurfListIndex = 0; SurfListIndex < SelectedSurfaces.Num(); ++SurfListIndex )
			{
				uint32 SelectedSurfIndex = SelectedSurfaces[SurfListIndex];

				check(Model->Surfs.IsValidIndex(SelectedSurfIndex));

				Model->ModifySurf(SelectedSurfIndex, true);
				Model->Surfs[SelectedSurfIndex].Material = DroppedObjAsMaterial;
				GEditor->polyUpdateMaster(Model, SelectedSurfIndex, false);
			
			}

			bResult = true;
		}
	}

	return bResult;
}


static bool AreAllDroppedObjectsBrushBuilders(const TArray<UObject*>& DroppedObjects)
{
	for (UObject* DroppedObject : DroppedObjects)
	{
		if (!DroppedObject->IsA(UBrushBuilder::StaticClass()))
		{
			return false;
		}
	}

	return true;
}

bool FLevelEditorViewportClient::DropObjectsOnBackground(FViewportCursorLocation& Cursor, const TArray<UObject*>& DroppedObjects, EObjectFlags ObjectFlags, TArray<AActor*>& OutNewActors, bool bCreateDropPreview, bool bSelectActors, UActorFactory* FactoryToUse)
{
	if (DroppedObjects.Num() == 0)
	{
		return false;
	}

	bool bSuccess = false;

	const bool bTransacted = !bCreateDropPreview && !AreAllDroppedObjectsBrushBuilders(DroppedObjects);

	// Create a transaction if not a preview drop
	if (bTransacted)
	{
		GEditor->BeginTransaction(NSLOCTEXT("UnrealEd", "CreateActors", "Create Actors"));
	}

	for ( int32 DroppedObjectsIdx = 0; DroppedObjectsIdx < DroppedObjects.Num(); ++DroppedObjectsIdx )
	{
		UObject* AssetObj = DroppedObjects[DroppedObjectsIdx];
		ensure( AssetObj );

		// Attempt to create actors from the dropped object
		TArray<AActor*> NewActors = AttemptDropObjAsActors(GetWorld()->GetCurrentLevel(), AssetObj, Cursor, bSelectActors, ObjectFlags, FactoryToUse);

		if ( NewActors.Num() > 0 )
		{
			OutNewActors.Append(NewActors);
			bSuccess = true;
		}
	}

	if (bTransacted)
	{
		GEditor->EndTransaction();
	}

	return bSuccess;
}

bool FLevelEditorViewportClient::DropObjectsOnActor(FViewportCursorLocation& Cursor, const TArray<UObject*>& DroppedObjects, AActor* DroppedUponActor, int32 DroppedUponSlot, EObjectFlags ObjectFlags, TArray<AActor*>& OutNewActors, bool bCreateDropPreview, bool bSelectActors, UActorFactory* FactoryToUse)
{
	if ( !DroppedUponActor || DroppedObjects.Num() == 0 )
	{
		return false;
	}

	bool bSuccess = false;

	const bool bTransacted = !bCreateDropPreview && !AreAllDroppedObjectsBrushBuilders(DroppedObjects);

	// Create a transaction if not a preview drop
	if (bTransacted)
	{
		GEditor->BeginTransaction(NSLOCTEXT("UnrealEd", "CreateActors", "Create Actors"));
	}

	for ( auto DroppedObject : DroppedObjects )
	{
		const bool bTest = false;
		const bool bAppliedToActor = ( FactoryToUse == NULL ) ? AttemptApplyObjToActor( DroppedObject, DroppedUponActor, DroppedUponSlot, bTest, bCreateDropPreview ) : false;

		if (!bAppliedToActor)
		{
			// Attempt to create actors from the dropped object
			TArray<AActor*> NewActors = AttemptDropObjAsActors(GetWorld()->GetCurrentLevel(), DroppedObject, Cursor, bSelectActors, ObjectFlags, FactoryToUse);

			if ( NewActors.Num() > 0 )
			{
				OutNewActors.Append( NewActors );
				bSuccess = true;
			}
		}
		else
		{
			bSuccess = true;
		}
	}

	if (bTransacted)
	{
		GEditor->EndTransaction();
	}

	return bSuccess;
}

bool FLevelEditorViewportClient::DropObjectsOnBSPSurface(FSceneView* View, FViewportCursorLocation& Cursor, const TArray<UObject*>& DroppedObjects, HModel* TargetProxy, EObjectFlags ObjectFlags, TArray<AActor*>& OutNewActors, bool bCreateDropPreview, bool bSelectActors, UActorFactory* FactoryToUse)
{
	if (DroppedObjects.Num() == 0)
	{
		return false;
	}

	bool bSuccess = false;

	const bool bTransacted = !bCreateDropPreview && !AreAllDroppedObjectsBrushBuilders(DroppedObjects);

	// Create a transaction if not a preview drop
	if (bTransacted)
	{
		GEditor->BeginTransaction(NSLOCTEXT("UnrealEd", "CreateActors", "Create Actors"));
	}

	for (auto DroppedObject : DroppedObjects)
	{
		const bool bAppliedToActor = (!bCreateDropPreview && FactoryToUse == NULL) ? AttemptApplyObjAsMaterialToSurface(DroppedObject, TargetProxy, Cursor) : false;

		if (!bAppliedToActor)
		{
			// Attempt to create actors from the dropped object
			TArray<AActor*> NewActors = AttemptDropObjAsActors(GetWorld()->GetCurrentLevel(), DroppedObject, Cursor, bSelectActors, ObjectFlags, FactoryToUse);

			if (NewActors.Num() > 0)
			{
				OutNewActors.Append(NewActors);
				bSuccess = true;
			}
		}
		else
		{
			bSuccess = true;
		}
	}

	if (bTransacted)
	{
		GEditor->EndTransaction();
	}

	return bSuccess;
}

/**
 * Called when an asset is dropped upon a manipulation widget.
 *
 * @param	View				The SceneView for the dropped-in viewport
 * @param	Cursor				Mouse cursor location
 * @param	DroppedObjects		Array of objects dropped into the viewport
 *
 * @return	true if the drop operation was successfully handled; false otherwise
 */
bool FLevelEditorViewportClient::DropObjectsOnWidget(FSceneView* View, FViewportCursorLocation& Cursor, const TArray<UObject*>& DroppedObjects, bool bCreateDropPreview)
{
	bool bResult = false;

	// Axis translation/rotation/scale widget - find out what's underneath the axis widget

	// Modify the ShowFlags for the scene so we can re-render the hit proxies without any axis widgets. 
	// Store original ShowFlags and assign them back when we're done
	const bool bOldModeWidgets1 = EngineShowFlags.ModeWidgets;
	const bool bOldModeWidgets2 = View->Family->EngineShowFlags.ModeWidgets;

	EngineShowFlags.ModeWidgets = 0;
	FSceneViewFamily* SceneViewFamily = const_cast< FSceneViewFamily* >( View->Family );
	SceneViewFamily->EngineShowFlags.ModeWidgets = 0;

	// Invalidate the hit proxy map so it will be rendered out again when GetHitProxy is called
	Viewport->InvalidateHitProxy();

	// This will actually re-render the viewport's hit proxies!
	FIntPoint DropPos = Cursor.GetCursorPos();

	HHitProxy* HitProxy = Viewport->GetHitProxy(DropPos.X, DropPos.Y);

	// We should never encounter a widget axis.  If we do, then something's wrong
	// with our ShowFlags (or the widget drawing code)
	check( !HitProxy || ( HitProxy && !HitProxy->IsA( HWidgetAxis::StaticGetType() ) ) );

	// Try this again, but without the widgets this time!
	TArray< AActor* > TemporaryActors;
	const FIntPoint& CursorPos = Cursor.GetCursorPos();
	const bool bOnlyDropOnTarget = false;
	bResult = DropObjectsAtCoordinates(CursorPos.X, CursorPos.Y, DroppedObjects, TemporaryActors, bOnlyDropOnTarget, bCreateDropPreview);

	// Restore the original flags
	EngineShowFlags.ModeWidgets = bOldModeWidgets1;
	SceneViewFamily->EngineShowFlags.ModeWidgets = bOldModeWidgets2;

	return bResult;
}

bool FLevelEditorViewportClient::HasDropPreviewActors() const
{
	return DropPreviewActors.Num() > 0;
}

bool FLevelEditorViewportClient::UpdateDropPreviewActors(int32 MouseX, int32 MouseY, const TArray<UObject*>& DroppedObjects, bool& out_bDroppedObjectsVisible, UActorFactory* FactoryToUse)
{
	out_bDroppedObjectsVisible = false;
	if( !HasDropPreviewActors() )
	{
		return false;
	}

	// While dragging actors, allow viewport updates
	bNeedsRedraw = true;

	// If the mouse did not move, there is no need to update anything
	if ( MouseX == DropPreviewMouseX && MouseY == DropPreviewMouseY )
	{
		return false;
	}

	// Update the cached mouse position
	DropPreviewMouseX = MouseX;
	DropPreviewMouseY = MouseY;

	// Get the center point between all the drop preview actors for use in calculations below
	// Also, build a list of valid AActor* pointers
	FVector ActorOrigin = FVector::ZeroVector;
	TArray<AActor*> DraggingActors;
	for ( auto ActorIt = DropPreviewActors.CreateConstIterator(); ActorIt; ++ActorIt )
	{
		AActor* DraggingActor = (*ActorIt).Get();

		if ( DraggingActor )
		{
			DraggingActors.Add(DraggingActor);
			ActorOrigin += DraggingActor->GetActorLocation();
		}
	}

	// If there were not valid actors after all, there is nothing to update
	if ( DraggingActors.Num() == 0 )
	{
		return false;
	}

	// Finish the calculation of the actors origin now that we know we are not dividing by zero
	ActorOrigin /= DraggingActors.Num();

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, 
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate( IsRealtime() ));
	FSceneView* View = CalcSceneView( &ViewFamily );
	FViewportCursorLocation Cursor(View, this, MouseX, MouseY);

	const FActorPositionTraceResult TraceResult = FActorPositioning::TraceWorldForPositionWithDefault(Cursor, *View, &DraggingActors);

	GEditor->ClickLocation = TraceResult.Location;
	GEditor->ClickPlane = FPlane(TraceResult.Location, TraceResult.SurfaceNormal);

	// Snap the new location if snapping is enabled
	FSnappingUtils::SnapPointToGrid(GEditor->ClickLocation, FVector::ZeroVector);

	AActor* DroppedOnActor = TraceResult.HitActor.Get();
	
	if (DroppedOnActor)
	{
		// We indicate that the dropped objects are visible if *any* of them are not applicable to other actors
		out_bDroppedObjectsVisible = DroppedObjects.ContainsByPredicate([&](UObject* AssetObj){
			return !AttemptApplyObjToActor(AssetObj, DroppedOnActor, -1, true);
		});
	}
	else
	{
		// All dropped objects are visible if we're not dropping on an actor
		out_bDroppedObjectsVisible = true;
	}

	for (AActor* Actor : DraggingActors)
	{
		const UActorFactory* ActorFactory = FactoryToUse ? FactoryToUse : GEditor->FindActorFactoryForActorClass(Actor->GetClass());

		const FSnappedPositioningData PositioningData = FSnappedPositioningData(this, TraceResult.Location, TraceResult.SurfaceNormal)
			.DrawSnapHelpers(true)
			.UseFactory(ActorFactory)
			.UseStartTransform(PreDragActorTransforms.FindRef(Actor))
			.UsePlacementExtent(Actor->GetPlacementExtent());

		const FTransform ActorTransform = FActorPositioning::GetSnappedSurfaceAlignedTransform(PositioningData);

		Actor->SetActorTransform(ActorTransform);
		Actor->SetIsTemporarilyHiddenInEditor(false);
		Actor->PostEditMove(false);
	}

	return true;
}

void FLevelEditorViewportClient::DestroyDropPreviewActors()
{
	if ( HasDropPreviewActors() )
	{
		for ( auto ActorIt = DropPreviewActors.CreateConstIterator(); ActorIt; ++ActorIt )
		{
			AActor* PreviewActor = (*ActorIt).Get();
			if (PreviewActor && PreviewActor != GetWorld()->GetDefaultBrush())
			{
				GetWorld()->DestroyActor(PreviewActor);
			}
		}
		DropPreviewActors.Empty();
	}
}


/**
* Checks the viewport to see if the given object can be dropped using the given mouse coordinates local to this viewport
*
* @param MouseX			The position of the mouse's X coordinate
* @param MouseY			The position of the mouse's Y coordinate
* @param AssetData			Asset in question to be dropped
*/
FDropQuery FLevelEditorViewportClient::CanDropObjectsAtCoordinates(int32 MouseX, int32 MouseY, const FAssetData& AssetData)
{
	FDropQuery Result;

	if ( !ObjectTools::IsAssetValidForPlacing( GetWorld(), AssetData.ObjectPath.ToString() ) )
	{
		return Result;
	}

	UObject* AssetObj = AssetData.GetAsset();
	UClass* ClassObj = Cast<UClass>( AssetObj );

	if ( ClassObj != NULL )
	{
		AssetObj = ClassObj->GetDefaultObject();
	}

	if( ensureMsgf( AssetObj != NULL, TEXT("AssetObj was null (%s)"), *AssetData.GetFullName() ) )
	{
		// Check if the asset has an actor factory
		bool bHasActorFactory = FActorFactoryAssetProxy::GetFactoryForAsset( AssetData ) != NULL;

		if ( AssetObj->IsA( AActor::StaticClass() ) || bHasActorFactory )
		{
			Result.bCanDrop = true;
			bPivotMovedIndependantly = false;
		}
		else if( AssetObj->IsA( UBrushBuilder::StaticClass()) )
		{
			Result.bCanDrop = true;
			bPivotMovedIndependantly = false;
		}
		else
		{
			HHitProxy* HitProxy = Viewport->GetHitProxy(MouseX, MouseY);
			if( HitProxy != NULL && CanApplyMaterialToHitProxy(HitProxy))
			{
				if ( AssetObj->IsA( UMaterialInterface::StaticClass() ) || AssetObj->IsA( UTexture::StaticClass() ) )
				{
					// If our asset is a material and the target is a valid recipient
					Result.bCanDrop = true;
					bPivotMovedIndependantly = false;

					//if ( HitProxy->IsA(HActor::StaticGetType()) )
					//{
					//	Result.HintText = LOCTEXT("Material_Shift_Hint", "Hold [Shift] to apply material to every slot");
					//}
				}
			}
		}
	}

	return Result;
}

bool FLevelEditorViewportClient::DropObjectsAtCoordinates(int32 MouseX, int32 MouseY, const TArray<UObject*>& DroppedObjects, TArray<AActor*>& OutNewActors, bool bOnlyDropOnTarget/* = false*/, bool bCreateDropPreview/* = false*/, bool SelectActors, UActorFactory* FactoryToUse )
{
	bool bResult = false;

	// Make sure the placement dragging actor is cleaned up.
	DestroyDropPreviewActors();

	if(DroppedObjects.Num() > 0)
	{
		Viewport->InvalidateHitProxy();

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Viewport, 
			GetScene(),
			EngineShowFlags)
			.SetRealtimeUpdate( IsRealtime() ));
		FSceneView* View = CalcSceneView( &ViewFamily );
		FViewportCursorLocation Cursor(View, this, MouseX, MouseY);

		HHitProxy* HitProxy = Viewport->GetHitProxy(Cursor.GetCursorPos().X, Cursor.GetCursorPos().Y);

		const FActorPositionTraceResult TraceResult = FActorPositioning::TraceWorldForPositionWithDefault(Cursor, *View);
		
		GEditor->ClickLocation = TraceResult.Location;
		GEditor->ClickPlane = FPlane(TraceResult.Location, TraceResult.SurfaceNormal);

		// Snap the new location if snapping is enabled
		FSnappingUtils::SnapPointToGrid(GEditor->ClickLocation, FVector::ZeroVector);

		EObjectFlags ObjectFlags = bCreateDropPreview ? RF_Transient : RF_Transactional;
		if ( HitProxy == nullptr )
		{
			bResult = DropObjectsOnBackground(Cursor, DroppedObjects, ObjectFlags, OutNewActors, bCreateDropPreview, SelectActors, FactoryToUse);
		}
		else if (HitProxy->IsA(HActor::StaticGetType()) || HitProxy->IsA(HBSPBrushVert::StaticGetType()))
		{
			AActor* TargetActor = NULL;
			int32 TargetMaterialSlot = -1;

			if (HitProxy->IsA(HActor::StaticGetType()))
			{
				HActor* TargetProxy = static_cast<HActor*>(HitProxy);
				TargetActor = TargetProxy->Actor;
				TargetMaterialSlot = TargetProxy->MaterialIndex;
			}
			else if (HitProxy->IsA(HBSPBrushVert::StaticGetType()))
			{
				HBSPBrushVert* TargetProxy = static_cast<HBSPBrushVert*>(HitProxy);
				TargetActor = TargetProxy->Brush.Get();
			}

			// If shift is pressed set the material slot to -1, so that it's applied to every slot.
			// We have to request it from the platform application directly because IsShiftPressed gets 
			// the cached state, when the viewport had focus
			if ( FSlateApplication::Get().GetPlatformApplication()->GetModifierKeys().IsShiftDown() )
			{
				TargetMaterialSlot = -1;
			}

			if (TargetActor != NULL)
			{
				FNavigationLockContext LockNavigationUpdates(TargetActor->GetWorld(), ENavigationLockReason::SpawnOnDragEnter, bCreateDropPreview);

				// if the target actor is selected, we should drop onto all selected items
				// otherwise, we should drop only onto this object
				bool bDropOntoSelected = TargetActor->IsSelected();

				if( !bDropOntoSelected || 
					bOnlyDropOnTarget || 
					FactoryToUse != NULL ||
					!AttemptApplyObjToActor(DroppedObjects[0], TargetActor, TargetMaterialSlot, true) )
				{
					bResult = DropObjectsOnActor(Cursor, DroppedObjects, TargetActor, TargetMaterialSlot, ObjectFlags, OutNewActors, bCreateDropPreview, SelectActors, FactoryToUse);
				}
				else
				{
					for ( FSelectionIterator It(*GEditor->GetSelectedActors()) ; It ; ++It )
					{
						TargetActor = static_cast<AActor*>(*It);
						if( TargetActor )
						{
							DropObjectsOnActor(Cursor, DroppedObjects, TargetActor, TargetMaterialSlot, ObjectFlags, OutNewActors, bCreateDropPreview, SelectActors, FactoryToUse);
							bResult = true;
						}
					}
				}
			}
		}
		else if (HitProxy->IsA(HModel::StaticGetType()))
		{
			// BSP surface
			bResult = DropObjectsOnBSPSurface(View, Cursor, DroppedObjects, static_cast<HModel*>(HitProxy), ObjectFlags, OutNewActors, bCreateDropPreview, SelectActors, FactoryToUse);
		}
		else if( HitProxy->IsA( HWidgetAxis::StaticGetType() ) )
		{
			// Axis translation/rotation/scale widget - find out what's underneath the axis widget
			bResult = DropObjectsOnWidget(View, Cursor, DroppedObjects);
		}

		if ( bResult )
		{
			// If we are creating a drop preview actor instead of a normal actor, we need to disable collision, selection, and make sure it is never saved.
			if ( bCreateDropPreview && OutNewActors.Num() > 0 )
			{
				DropPreviewActors.Empty();

				for ( auto ActorIt = OutNewActors.CreateConstIterator(); ActorIt; ++ActorIt )
				{
					AActor* NewActor = *ActorIt;
					DropPreviewActors.Add(NewActor);
					
					PreDragActorTransforms.Add(NewActor, NewActor->GetTransform());

					NewActor->SetActorEnableCollision(false);

					// Deselect if selected
					if( NewActor->IsSelected() )
					{
						GEditor->SelectActor( NewActor, /*InSelected=*/false, /*bNotify=*/true );
					}

					// Prevent future selection. This also prevents the hit proxy from interfering with placement logic.
					TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
					NewActor->GetComponents(PrimitiveComponents);

					for ( auto CompIt = PrimitiveComponents.CreateConstIterator(); CompIt; ++CompIt )
					{
						(*CompIt)->bSelectable = false;
					}
				}

				// Set the current MouseX/Y to prime the preview update
				DropPreviewMouseX = MouseX;
				DropPreviewMouseY = MouseY;

				// Invalidate the hit proxy now so the drop preview will be accurate.
				// We don't invalidate the hit proxy in the drop preview update itself because it is slow.
				//Viewport->InvalidateHitProxy();
			}
			// Dropping the actors rather than a preview? Probably want to select them all then. 
			else if(!bCreateDropPreview && SelectActors && OutNewActors.Num() > 0)
			{
				for (auto It = OutNewActors.CreateConstIterator(); It; ++It)
				{
					GEditor->SelectActor( (*It), true, true );
				}
			}

			// Give the viewport focus
			//SetFocus( static_cast<HWND>( Viewport->GetWindow() ) );

			SetCurrentViewport();
		}
	}

	if (bResult)
	{
		if ( !bCreateDropPreview && IPlacementModeModule::IsAvailable() )
		{
			IPlacementModeModule::Get().AddToRecentlyPlaced( DroppedObjects, FactoryToUse );
		}

		if (!bCreateDropPreview)
		{
			FEditorDelegates::OnNewActorsDropped.Broadcast(DroppedObjects, OutNewActors);
		}
	}

	return bResult;
}

/**
 *	Called to check if a material can be applied to an object, given the hit proxy
 */
bool FLevelEditorViewportClient::CanApplyMaterialToHitProxy( const HHitProxy* HitProxy ) const
{
	return ( HitProxy->IsA(HModel::StaticGetType()) || HitProxy->IsA(HActor::StaticGetType()) );
}

FTrackingTransaction::FTrackingTransaction()
	: TransCount(0)
	, ScopedTransaction(NULL)
	, TrackingTransactionState(ETransactionState::Inactive)
{}

FTrackingTransaction::~FTrackingTransaction()
{
	End();
}

void FTrackingTransaction::Begin(const FText& Description)
{
	End();
	ScopedTransaction = new FScopedTransaction( Description );

	TrackingTransactionState = ETransactionState::Active;

	TSet<AGroupActor*> GroupActors;

	// Modify selected actors to record their state at the start of the transaction
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = CastChecked<AActor>(*It);

		Actor->Modify();

		if (GEditor->bGroupingActive)
		{
			// if this actor is in a group, add the GroupActor into a list to be modified shortly
			AGroupActor* ActorLockedRootGroup = AGroupActor::GetRootForActor(Actor, true);
			if (ActorLockedRootGroup != nullptr)
			{
				GroupActors.Add(ActorLockedRootGroup);
			}
		}
	}

	// Modify unique group actors
	for (auto* GroupActor : GroupActors)
	{
		GroupActor->Modify();
	}
}

void FTrackingTransaction::End()
{
	if( ScopedTransaction )
	{
		delete ScopedTransaction;
		ScopedTransaction = NULL;
	}
	TrackingTransactionState = ETransactionState::Inactive;
}

void FTrackingTransaction::Cancel()
{
	if(ScopedTransaction)
	{
		ScopedTransaction->Cancel();
	}
	End();
}

void FTrackingTransaction::BeginPending(const FText& Description)
{
	End();

	PendingDescription = Description;
	TrackingTransactionState = ETransactionState::Pending;
}

void FTrackingTransaction::PromotePendingToActive()
{
	if(IsPending())
	{
		Begin(PendingDescription);
		PendingDescription = FText();
	}
}

FLevelEditorViewportClient::FLevelEditorViewportClient(const TSharedPtr<SLevelViewport>& InLevelViewport)
	: FEditorViewportClient(&GLevelEditorModeTools(), nullptr, StaticCastSharedPtr<SEditorViewport>(InLevelViewport))
	, ViewHiddenLayers()
	, VolumeActorVisibility()
	, LastEditorViewLocation( FVector::ZeroVector )
	, LastEditorViewRotation( FRotator::ZeroRotator )
	, ColorScale( FVector(1,1,1) )
	, FadeColor( FColor(0,0,0) )
	, FadeAmount(0.f)	
	, bEnableFading(false)
	, bEnableColorScaling(false)
	, bEditorCameraCut(false)
	, bDrawBaseInfo(false)
	, bDuplicateActorsOnNextDrag( false )
	, bDuplicateActorsInProgress( false )
	, bIsTrackingBrushModification( false )
	, bLockedCameraView(true)
	, SpriteCategoryVisibility()
	, World(nullptr)
	, TrackingTransaction()
	, DropPreviewMouseX(0)
	, DropPreviewMouseY(0)
	, bWasControlledByOtherViewport(false)
	, ActorLockedByMatinee(nullptr)
	, ActorLockedToCamera(nullptr)
{
	// By default a level editor viewport is pointed to the editor world
	SetReferenceToWorldContext(GEditor->GetEditorWorldContext());

	GEditor->LevelViewportClients.Add(this);

	// The level editor fully supports mode tools and isn't doing any incompatible stuff with the Widget
	ModeTools->SetWidgetMode(FWidget::WM_Translate);
	Widget->SetUsesEditorModeTools(ModeTools);

	// Register for editor cleanse events so we can release references to hovered actors
	FEditorSupportDelegates::CleanseEditor.AddRaw(this, &FLevelEditorViewportClient::OnEditorCleanse);

	// Add a delegate so we get informed when an actor has moved.
	GEngine->OnActorMoved().AddRaw(this, &FLevelEditorViewportClient::OnActorMoved);

	// GEditorModeTools serves as our draw helper
	bUsesDrawHelper = false;

	InitializeVisibilityFlags();

	// Sign up for notifications about users changing settings.
	GetMutableDefault<ULevelEditorViewportSettings>()->OnSettingChanged().AddRaw(this, &FLevelEditorViewportClient::HandleViewportSettingChanged);
}

//
//	FLevelEditorViewportClient::~FLevelEditorViewportClient
//

FLevelEditorViewportClient::~FLevelEditorViewportClient()
{
	// Unregister for all global callbacks to this object
	FEditorSupportDelegates::CleanseEditor.RemoveAll(this);

	// Remove our move delegate
	GEngine->OnActorMoved().RemoveAll(this);

	// make sure all actors have this view removed from their visibility bits
	GEditor->Layers->RemoveViewFromActorViewVisibility( this );

	//make to clean up the global "current" & "last" clients when we delete the active one.
	if (GCurrentLevelEditingViewportClient == this)
	{
		GCurrentLevelEditingViewportClient = NULL;
	}
	if (GLastKeyLevelEditingViewportClient == this)
	{
		GLastKeyLevelEditingViewportClient = NULL;
	}

	GetMutableDefault<ULevelEditorViewportSettings>()->OnSettingChanged().RemoveAll(this);

	GEditor->LevelViewportClients.Remove(this);

	RemoveReferenceToWorldContext(GEditor->GetEditorWorldContext());
}

void FLevelEditorViewportClient::InitializeVisibilityFlags()
{
	// make sure all actors know about this view for per-view layer vis
	GEditor->Layers->UpdatePerViewVisibility(this);

	// Get the number of volume classes so we can initialize our bit array
	TArray<UClass*> VolumeClasses;
	UUnrealEdEngine::GetSortedVolumeClasses(&VolumeClasses);
	VolumeActorVisibility.Init(true, VolumeClasses.Num());

	// Initialize all sprite categories to visible
	SpriteCategoryVisibility.Init(true, GUnrealEd->SpriteIDToIndexMap.Num());
}

FSceneView* FLevelEditorViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily)
{
	bWasControlledByOtherViewport = false;

	// set all other matching viewports to my location, if the LOD locking is enabled,
	// unless another viewport already set me this frame (otherwise they fight)
	if (GEditor->bEnableLODLocking && !bWasControlledByOtherViewport)
	{
		for (int32 ViewportIndex = 0; ViewportIndex < GEditor->LevelViewportClients.Num(); ViewportIndex++)
		{
			FLevelEditorViewportClient* ViewportClient = GEditor->LevelViewportClients[ViewportIndex];

			//only change camera for a viewport that is looking at the same scene
			if (GetScene() != ViewportClient->GetScene())
			{
				continue;
			}

			// go over all other level viewports
			if (ViewportClient && ViewportClient->Viewport && ViewportClient != this)
			{
				// force camera of same-typed viewports
				if (ViewportClient->GetViewportType() == GetViewportType())
				{
					ViewportClient->SetViewLocation( GetViewLocation() );
					ViewportClient->SetViewRotation( GetViewRotation() );
					ViewportClient->SetOrthoZoom( GetOrthoZoom() );

					// don't let this other viewport update itself in its own CalcSceneView
					ViewportClient->bWasControlledByOtherViewport = true;
				}
				// when we are LOD locking, ortho views get their camera position from this view, so make sure it redraws
				else if (IsPerspective() && !ViewportClient->IsPerspective())
				{
					// don't let this other viewport update itself in its own CalcSceneView
					ViewportClient->bWasControlledByOtherViewport = true;
				}
			}

			// if the above code determined that this viewport has changed, delay the update unless
			// an update is already in the pipe
			if (ViewportClient->bWasControlledByOtherViewport && ViewportClient->TimeForForceRedraw == 0.0)
			{
				ViewportClient->TimeForForceRedraw = FPlatformTime::Seconds() + 0.9 + FMath::FRand() * 0.2;
			}
		}
	}

	FSceneView* View = FEditorViewportClient::CalcSceneView(ViewFamily);

	View->SpriteCategoryVisibility = SpriteCategoryVisibility;
	View->bCameraCut = bEditorCameraCut;

	return View;

}

ELevelViewportType FLevelEditorViewportClient::GetViewportType() const
{
	const UCameraComponent* ActiveCameraComponent = GetCameraComponentForView();
	
	if (ActiveCameraComponent != NULL)
	{
		return (ActiveCameraComponent->ProjectionMode == ECameraProjectionMode::Perspective) ? LVT_Perspective : LVT_OrthoFreelook;
	}
	else
	{
		return FEditorViewportClient::GetViewportType();
	}
}

void FLevelEditorViewportClient::OverridePostProcessSettings( FSceneView& View )
{
	const UCameraComponent* CameraComponent = GetCameraComponentForView();
	if (CameraComponent)
	{
		View.OverridePostProcessSettings(CameraComponent->PostProcessSettings, CameraComponent->PostProcessBlendWeight);
	}
}

bool FLevelEditorViewportClient::ShouldLockPitch() const 
{
	return FEditorViewportClient::ShouldLockPitch() || !ModeTools->GetActiveMode(FBuiltinEditorModes::EM_InterpEdit) ;
}

void FLevelEditorViewportClient::PerspectiveCameraMoved()
{
	// Update the locked actor (if any) from the camera
	MoveLockedActorToCamera();

	// If any other viewports have this actor locked too, we need to update them
	if( GetActiveActorLock().IsValid() )
	{
		UpdateLockedActorViewports(GetActiveActorLock().Get(), false);
	}

	// Tell the editing mode that the camera moved, in case its interested.
	FEdMode* Mode = ModeTools->GetActiveMode(FBuiltinEditorModes::EM_InterpEdit);
	if( Mode )
	{
		((FEdModeInterpEdit*)Mode)->CamMoveNotify(this);
	}

	// Broadcast 'camera moved' delegate
	FEditorDelegates::OnEditorCameraMoved.Broadcast(GetViewLocation(), GetViewRotation(), ViewportType, ViewIndex);
}

/**
 * Reset the camera position and rotation.  Used when creating a new level.
 */
void FLevelEditorViewportClient::ResetCamera()
{
	// Initialize perspective view transform
	ViewTransformPerspective.SetLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	ViewTransformPerspective.SetRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);
	ViewTransformPerspective.SetLookAt(FVector::ZeroVector);

	FMatrix OrbitMatrix = ViewTransformPerspective.ComputeOrbitMatrix();
	OrbitMatrix = OrbitMatrix.InverseFast();

	ViewTransformPerspective.SetRotation(OrbitMatrix.Rotator());
	ViewTransformPerspective.SetLocation(OrbitMatrix.GetOrigin());

	ViewTransformPerspective.SetOrthoZoom(DEFAULT_ORTHOZOOM);

	// Initialize orthographic view transform
	ViewTransformOrthographic.SetLocation(FVector::ZeroVector);
	ViewTransformOrthographic.SetRotation(FRotator::ZeroRotator);
	ViewTransformOrthographic.SetOrthoZoom(DEFAULT_ORTHOZOOM);

	ViewFOV = FOVAngle;

	// If interp mode is active, tell it about the camera movement.
	FEdMode* Mode = ModeTools->GetActiveMode(FBuiltinEditorModes::EM_InterpEdit);
	if( Mode )
	{
		((FEdModeInterpEdit*)Mode)->CamMoveNotify(this);
	}

	// Broadcast 'camera moved' delegate
	FEditorDelegates::OnEditorCameraMoved.Broadcast(GetViewLocation(), GetViewRotation(), ViewportType, ViewIndex);
}

void FLevelEditorViewportClient::ResetViewForNewMap()
{
	ResetCamera();
	bForcingUnlitForNewMap = true;
}

void FLevelEditorViewportClient::PrepareCameraForPIE()
{
	LastEditorViewLocation = GetViewLocation();
	LastEditorViewRotation = GetViewRotation();
}

void FLevelEditorViewportClient::RestoreCameraFromPIE()
{
	const bool bRestoreEditorCamera = GEditor != NULL && !GetDefault<ULevelEditorViewportSettings>()->bEnableViewportCameraToUpdateFromPIV;

	//restore the camera position if this is an ortho viewport OR if PIV camera dropping is undesired
	if ( IsOrtho() || bRestoreEditorCamera )
	{
		SetViewLocation( LastEditorViewLocation );
		SetViewRotation( LastEditorViewRotation );
	}

	if( IsPerspective() )
	{
		ViewFOV = FOVAngle;
		RemoveCameraRoll();
	}
}


//
//	FLevelEditorViewportClient::ProcessClick
//

void FLevelEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	// We clicked, allow the pivot to reposition itself.
	bPivotMovedIndependantly = false;

	static FName ProcessClickTrace = FName(TEXT("ProcessClickTrace"));

	const FViewportClick Click(&View,this,Key,Event,HitX,HitY);
	if (!ModeTools->HandleClick(this, HitProxy,Click))
	{
		if (HitProxy == NULL)
		{
			ClickHandlers::ClickBackdrop(this,Click);
		}
		else if (HitProxy->IsA(HWidgetAxis::StaticGetType()))
		{
			// The user clicked on an axis translation/rotation hit proxy.  However, we want
			// to find out what's underneath the axis widget.  To do this, we'll need to render
			// the viewport's hit proxies again, this time *without* the axis widgets!

			// OK, we need to be a bit evil right here.  Basically we want to hijack the ShowFlags
			// for the scene so we can re-render the hit proxies without any axis widgets.  We'll
			// store the original ShowFlags and modify them appropriately
			const bool bOldModeWidgets1 = EngineShowFlags.ModeWidgets;
			const bool bOldModeWidgets2 = View.Family->EngineShowFlags.ModeWidgets;

			EngineShowFlags.ModeWidgets = 0;
			FSceneViewFamily* SceneViewFamily = const_cast<FSceneViewFamily*>(View.Family);
			SceneViewFamily->EngineShowFlags.ModeWidgets = 0;
			bool bWasWidgetDragging = Widget->IsDragging();
			Widget->SetDragging(false);

			// Invalidate the hit proxy map so it will be rendered out again when GetHitProxy
			// is called
			Viewport->InvalidateHitProxy();

			// This will actually re-render the viewport's hit proxies!
			HHitProxy* HitProxyWithoutAxisWidgets = Viewport->GetHitProxy(HitX, HitY);
			if (HitProxyWithoutAxisWidgets != NULL && !HitProxyWithoutAxisWidgets->IsA(HWidgetAxis::StaticGetType()))
			{
				// Try this again, but without the widget this time!
				ProcessClick(View, HitProxyWithoutAxisWidgets, Key, Event, HitX, HitY);
			}

			// Undo the evil
			EngineShowFlags.ModeWidgets = bOldModeWidgets1;
			SceneViewFamily->EngineShowFlags.ModeWidgets = bOldModeWidgets2;

			Widget->SetDragging(bWasWidgetDragging);

			// Invalidate the hit proxy map again so that it'll be refreshed with the original
			// scene contents if we need it again later.
			Viewport->InvalidateHitProxy();
		}
		else if (GUnrealEd->ComponentVisManager.HandleClick(this, HitProxy, Click))
		{
			// Component vis manager handled the click
		}
		else if (HitProxy->IsA(HActor::StaticGetType()))
		{
			ClickHandlers::ClickActor(this,((HActor*)HitProxy)->Actor,Click,true);
		}
		else if (HitProxy->IsA(HInstancedStaticMeshInstance::StaticGetType()))
		{
			ClickHandlers::ClickActor(this, ((HInstancedStaticMeshInstance*)HitProxy)->Component->GetOwner(), Click, true);
		}
		else if (HitProxy->IsA(HBSPBrushVert::StaticGetType()) && ((HBSPBrushVert*)HitProxy)->Brush.IsValid())
		{
			ClickHandlers::ClickBrushVertex(this,((HBSPBrushVert*)HitProxy)->Brush.Get(),((HBSPBrushVert*)HitProxy)->Vertex,Click);
		}
		else if (HitProxy->IsA(HStaticMeshVert::StaticGetType()))
		{
			ClickHandlers::ClickStaticMeshVertex(this,((HStaticMeshVert*)HitProxy)->Actor,((HStaticMeshVert*)HitProxy)->Vertex,Click);
		}
		else if (HitProxy->IsA(HGeomPolyProxy::StaticGetType()))
		{
			FHitResult CheckResult(ForceInit);
			FCollisionQueryParams BoxParams(ProcessClickTrace, false, ((HGeomPolyProxy*)HitProxy)->GeomObject->ActualBrush);
			bool bHit = GWorld->SweepSingle(CheckResult, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * HALF_WORLD_MAX, FQuat::Identity, FCollisionShape::MakeBox(FVector(1.f)), BoxParams, FCollisionObjectQueryParams(ECC_WorldStatic));

			if( bHit )
			{	
				GEditor->ClickLocation = CheckResult.Location;
				GEditor->ClickPlane = FPlane(CheckResult.Location,CheckResult.Normal);
			}

			if( !ClickHandlers::ClickActor(this,((HGeomPolyProxy*)HitProxy)->GeomObject->ActualBrush,Click,false) )
			{
				ClickHandlers::ClickGeomPoly(this,(HGeomPolyProxy*)HitProxy,Click);
			}

			Invalidate( true, true );
		}
		else if (HitProxy->IsA(HGeomEdgeProxy::StaticGetType()))
		{
			if( !ClickHandlers::ClickGeomEdge(this,(HGeomEdgeProxy*)HitProxy,Click) )
			{
				ClickHandlers::ClickActor(this,((HGeomEdgeProxy*)HitProxy)->GeomObject->ActualBrush,Click,true);
			}
		}
		else if (HitProxy->IsA(HGeomVertexProxy::StaticGetType()))
		{
			ClickHandlers::ClickGeomVertex(this,(HGeomVertexProxy*)HitProxy,Click);
		}
		else if (HitProxy->IsA(HModel::StaticGetType()))
		{
			HModel* ModelHit = (HModel*)HitProxy;

			// Compute the viewport's current view family.
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues( Viewport, GetScene(), EngineShowFlags ));
			FSceneView* SceneView = CalcSceneView( &ViewFamily );

			uint32 SurfaceIndex = INDEX_NONE;
			if(ModelHit->ResolveSurface(SceneView,HitX,HitY,SurfaceIndex))
			{
				ClickHandlers::ClickSurface(this,ModelHit->GetModel(),SurfaceIndex,Click);
			}
		}
		else if (HitProxy->IsA(HLevelSocketProxy::StaticGetType()))
		{
			ClickHandlers::ClickLevelSocket(this, HitProxy, Click);
		}
	}
}

// Frustum parameters for the perspective view.
static float GPerspFrustumAngle=90.f;
static float GPerspFrustumAspectRatio=1.77777f;
static float GPerspFrustumStartDist=GNearClippingPlane;
static float GPerspFrustumEndDist=HALF_WORLD_MAX;
static FMatrix GPerspViewMatrix;


void FLevelEditorViewportClient::Tick(float DeltaTime)
{
	FEditorViewportClient::Tick(DeltaTime);

	if( !bPivotMovedIndependantly && GCurrentLevelEditingViewportClient == this &&
		bIsRealtime &&
		( Widget == NULL || !Widget->IsDragging() ) )
	{
		// @todo SIE: May be very expensive for lots of actors
		GUnrealEd->UpdatePivotLocationForSelection();
	}

	// Update the preview mesh for the preview mesh mode. 
	GEditor->UpdatePreviewMesh();

	// Copy perspective views to the global if this viewport is a view parent or has streaming volume previs enabled
	if ( ViewState.GetReference()->IsViewParent() || (IsPerspective() && GetDefault<ULevelEditorViewportSettings>()->bLevelStreamingVolumePrevis && Viewport->GetSizeXY().X > 0) )
	{
		GPerspFrustumAngle=ViewFOV;
		GPerspFrustumAspectRatio=AspectRatio;
		GPerspFrustumStartDist=GetNearClipPlane();

		GPerspFrustumEndDist= HALF_WORLD_MAX;

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Viewport,
			GetScene(),
			EngineShowFlags)
			.SetRealtimeUpdate( IsRealtime() ) );
		FSceneView* View = CalcSceneView(&ViewFamily);
		GPerspViewMatrix = View->ViewMatrices.ViewMatrix;
	}

	UpdateViewForLockedActor();
}


void FLevelEditorViewportClient::UpdateViewForLockedActor()
{
	// We can't be locked to a matinee actor if this viewport doesn't allow matinee control
	if ( !bAllowMatineePreview && ActorLockedByMatinee.IsValid() )
	{
		ActorLockedByMatinee = nullptr;
	}

	bUseControllingActorViewInfo = false;
	ControllingActorViewInfo = FMinimalViewInfo();

	const AActor* Actor = ActorLockedByMatinee.IsValid() ? ActorLockedByMatinee.Get() : ActorLockedToCamera.Get();
	if( Actor != NULL )
	{
		// Update transform
		if( Actor->GetAttachParentActor() != NULL )
		{
			// Actor is parented, so use the actor to world matrix for translation and rotation information.
			SetViewLocation( Actor->GetActorLocation() );
			SetViewRotation( Actor->GetActorRotation() );				
		}
		else if( Actor->GetRootComponent() != NULL )
		{
			// No attachment, so just use the relative location, so that we don't need to
			// convert from a quaternion, which loses winding information.
			SetViewLocation( Actor->GetRootComponent()->RelativeLocation );
			SetViewRotation( Actor->GetRootComponent()->RelativeRotation );
		}

		if( bLockedCameraView )
		{
			// If this is a camera actor, then inherit some other settings
			UCameraComponent* CameraComponent = Actor->FindComponentByClass<UCameraComponent>();
			if( CameraComponent != NULL )
			{
				bUseControllingActorViewInfo = true;
				CameraComponent->GetCameraView(0.0f, ControllingActorViewInfo);

				// Post processing is handled by OverridePostProcessingSettings
				ViewFOV = ControllingActorViewInfo.FOV;
				AspectRatio = ControllingActorViewInfo.AspectRatio;
				SetViewLocation(ControllingActorViewInfo.Location);
				SetViewRotation(ControllingActorViewInfo.Rotation);
			}
		}
	}
}


/*namespace ViewportDeadZoneConstants
{
	enum
	{
		NO_DEAD_ZONE,
		STANDARD_DEAD_ZONE
	};
};*/

/** Trim the specified line to the planes of the frustum */
void TrimLineToFrustum(const FConvexVolume& Frustum, FVector& Start, FVector& End)
{
	FVector Intersection;
	for (const auto& Plane : Frustum.Planes)
	{
		if (FMath::SegmentPlaneIntersection(Start, End, Plane, Intersection))
		{
			// Chop the line inside the frustum
			if ((static_cast<const FVector&>(Plane) | (Intersection - End)) > 0.0f)
			{
				Start = Intersection;
			}
			else
			{
				End = Intersection;
			}
		}
	}
}

void FLevelEditorViewportClient::ProjectActorsIntoWorld(const TArray<AActor*>& Actors, FViewport* Viewport, const FVector& Drag, const FRotator& Rot)
{
	// Compile an array of selected actors
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, 
		GetScene(),
		EngineShowFlags)
		.SetRealtimeUpdate( IsRealtime() ));
	// SceneView is deleted with the ViewFamily
	FSceneView* SceneView = CalcSceneView( &ViewFamily );

	// Calculate the frustum so we can trim rays to it
	const FConvexVolume Frustum;
	GetViewFrustumBounds(const_cast<FConvexVolume&>(Frustum), SceneView->ViewProjectionMatrix, true);

	const FMatrix InputCoordSystem = GetWidgetCoordSystem();
	const EAxisList::Type CurrentAxis = GetCurrentWidgetAxis();
	
	const FVector DeltaTranslation = (ModeTools->PivotLocation - ModeTools->CachedLocation) + Drag;


	// Loop over all the actors and attempt to snap them along the drag axis normal
	for (auto* Actor : Actors)
	{
		// Use the Delta of the Mode tool with the actor pre drag location to avoid accumulating snapping offsets
		FVector NewActorPosition;
		if (const FTransform* PreDragTransform = PreDragActorTransforms.Find(Actor))
		{
			NewActorPosition = PreDragTransform->GetLocation() + DeltaTranslation;
		}
		else
		{
			const FTransform& ActorTransform = PreDragActorTransforms.Add(Actor, Actor->GetTransform());
			NewActorPosition = ActorTransform.GetLocation() + DeltaTranslation;
		}

		FViewportCursorLocation Cursor(SceneView, this, 0, 0);

		FActorPositionTraceResult TraceResult;
		bool bSnapped = false;

		bool bIsOnScreen = false;
		{
			// We only snap things that are on screen
			FVector2D ScreenPos;
			FIntPoint ViewportSize = Viewport->GetSizeXY();
			if (SceneView->WorldToPixel(NewActorPosition, ScreenPos) && FMath::IsWithin<float>(ScreenPos.X, 0, ViewportSize.X) && FMath::IsWithin<float>(ScreenPos.Y, 0, ViewportSize.Y))
			{
				bIsOnScreen = true;
				Cursor = FViewportCursorLocation(SceneView, this, ScreenPos.X, ScreenPos.Y);
			}
		}

		if (bIsOnScreen)
		{
			// Determine how we're going to attempt to project the object onto the world
			if (CurrentAxis == EAxisList::XY || CurrentAxis == EAxisList::XZ || CurrentAxis == EAxisList::YZ)
			{
				// Snap along the perpendicular axis
				const FVector PlaneNormal = CurrentAxis == EAxisList::XY ? FVector(0, 0, 1) : CurrentAxis == EAxisList::XZ ? FVector(0, 1, 0) : FVector(1, 0, 0);
				FVector TraceDirection = InputCoordSystem.TransformVector(PlaneNormal);

				// Make sure the trace normal points along the view direction
				if (FVector::DotProduct(SceneView->GetViewDirection(), TraceDirection) < 0.f)
				{
					TraceDirection = -TraceDirection;
				}

				FVector RayStart	= NewActorPosition - (TraceDirection * HALF_WORLD_MAX/2);
				FVector RayEnd		= NewActorPosition + (TraceDirection * HALF_WORLD_MAX/2);

				TrimLineToFrustum(Frustum, RayStart, RayEnd);

				TraceResult = FActorPositioning::TraceWorldForPosition(*GetWorld(), *SceneView, RayStart, RayEnd, &Actors);
			}
			else
			{
				TraceResult = FActorPositioning::TraceWorldForPosition(Cursor, *SceneView, &Actors);
			}
		}
				
		if (TraceResult.State == FActorPositionTraceResult::HitSuccess)
		{
			// Move the actor to the position of the trace hit using the spawn offset rules
			// We only do this if we found a valid hit (we don't want to move the actor in front of the camera by default)

			const auto* Factory = GEditor->FindActorFactoryForActorClass(Actor->GetClass());
			
			const auto ActorLocation = Actor->GetActorLocation();
			const auto ActorRotation = Actor->GetActorRotation().Quaternion();

			const FTransform* PreDragActorTransform = PreDragActorTransforms.Find(Actor);
			check(PreDragActorTransform);

			// Compute the surface aligned transform. Note we do not use the snapped version here as our DragDelta is already snapped

			const FPositioningData PositioningData = FPositioningData(TraceResult.Location, TraceResult.SurfaceNormal)
				.UseStartTransform(*PreDragActorTransform)
				.UsePlacementExtent(Actor->GetPlacementExtent())
				.UseFactory(Factory);

			FTransform ActorTransform = FActorPositioning::GetSurfaceAlignedTransform(PositioningData);
			
			ActorTransform.SetScale3D(Actor->GetActorScale3D());
			if (auto* RootComponent = Actor->GetRootComponent())
			{
				RootComponent->SetWorldTransform(ActorTransform);
			}
		}
		else
		{
			// Didn't find a valid surface snapping candidate, just apply the deltas directly
			ApplyDeltaToActor(Actor, NewActorPosition - Actor->GetActorLocation(), Rot, FVector(0.f, 0.f, 0.f));
		}
	}
}

bool FLevelEditorViewportClient::InputWidgetDelta(FViewport* Viewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (GUnrealEd->ComponentVisManager.HandleInputDelta(this, Viewport, Drag, Rot, Scale))
	{
		return true;
	}

	bool bHandled = false;

	// Give the current editor mode a chance to use the input first.  If it does, don't apply it to anything else.
	if (FEditorViewportClient::InputWidgetDelta(Viewport, CurrentAxis, Drag, Rot, Scale))
	{
		bHandled = true;
	}
	else
	{
		//@TODO: MODETOOLS: Much of this needs to get pushed to Super, but not all of it can be...
		if( CurrentAxis != EAxisList::None )
		{
			// Skip actors transformation routine in case if any of the selected actors locked
			// but still pretend that we have handled the input
			if (!GEditor->HasLockedActors())
			{
				const bool LeftMouseButtonDown = Viewport->KeyState(EKeys::LeftMouseButton);
				const bool RightMouseButtonDown = Viewport->KeyState(EKeys::RightMouseButton);
				const bool MiddleMouseButtonDown = Viewport->KeyState(EKeys::MiddleMouseButton);

				// If duplicate dragging . . .
				if ( IsAltPressed() && (LeftMouseButtonDown || RightMouseButtonDown) )
				{
					// The widget has been offset, so check if we should duplicate actors.
					if ( bDuplicateActorsOnNextDrag )
					{
						// Only duplicate if we're translating or rotating.
						if ( !Drag.IsNearlyZero() || !Rot.IsZero() )
						{
							// Actors haven't been dragged since ALT+LMB went down.
							bDuplicateActorsOnNextDrag = false;

							GEditor->edactDuplicateSelected( GetWorld()->GetCurrentLevel(), false );
						}
					}
				}

				// We do not want actors updated if we are holding down the middle mouse button.
				if(!MiddleMouseButtonDown)
				{
					bool bSnapped = FSnappingUtils::SnapActorsToNearestActor( Drag, this );
					bSnapped = bSnapped || FSnappingUtils::SnapDraggedActorsToNearestVertex( Drag, this );

					// If we are only changing position, project the actors onto the world
					const bool bOnlyTranslation = !Drag.IsZero() && Rot.IsZero() && Scale.IsZero();

					const EAxisList::Type CurrentAxis = GetCurrentWidgetAxis();
					const bool bSingleAxisDrag = CurrentAxis == EAxisList::X || CurrentAxis == EAxisList::Y || CurrentAxis == EAxisList::Z;
					if (!bSnapped && !bSingleAxisDrag && GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled && bOnlyTranslation)
					{
						TArray<AActor*> SelectedActors;
						for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
						{
							if (AActor* Actor = Cast<AActor>(*It))
							{
								SelectedActors.Add(Actor);
							}
						}

						ProjectActorsIntoWorld(SelectedActors, Viewport, Drag, Rot);
					}
					else
					{
						ApplyDeltaToActors( Drag, Rot, Scale );
					}

					ApplyDeltaToRotateWidget( Rot );
				}
				else
				{
					FSnappingUtils::SnapDragLocationToNearestVertex( ModeTools->PivotLocation, Drag, this );
					bPivotMovedIndependantly = true;
				}

				ModeTools->PivotLocation += Drag;
				ModeTools->SnappedLocation += Drag;

				if( IsShiftPressed() )
				{
					FVector CameraDelta( Drag );
					MoveViewportCamera( CameraDelta, FRotator::ZeroRotator );
				}

				TArray<FEdMode*> ActiveModes; 
				ModeTools->GetActiveModes(ActiveModes);

				for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
				{
					ActiveModes[ModeIndex]->UpdateInternalData();
				}
			}

			bHandled = true;
		}

	}

	return bHandled;
}

TSharedPtr<FDragTool> FLevelEditorViewportClient::MakeDragTool( EDragTool::Type DragToolType )
{
	// Let the drag tool handle the transaction
	TrackingTransaction.Cancel();

	TSharedPtr<FDragTool> DragTool;
	switch( DragToolType )
	{
	case EDragTool::BoxSelect:
		DragTool = MakeShareable( new FDragTool_ActorBoxSelect(this) );
		break;
	case EDragTool::FrustumSelect:
		DragTool = MakeShareable( new FDragTool_ActorFrustumSelect(this) );
		break;	
	case EDragTool::Measure:
		DragTool = MakeShareable( new FDragTool_Measure(this) );
		break;
	};

	return DragTool;
}

static bool CommandAcceptsInput( FLevelEditorViewportClient& ViewportClient, FKey Key, const TSharedPtr<FUICommandInfo> Command )
{
	const FInputGesture& Gesture = *Command->GetActiveGesture();

	return (!Gesture.NeedsControl()	|| ViewportClient.IsCtrlPressed() ) 
		&& (!Gesture.NeedsAlt()		|| ViewportClient.IsAltPressed() ) 
		&& (!Gesture.NeedsShift()	|| ViewportClient.IsShiftPressed() ) 
		&& (!Gesture.NeedsCommand()		|| ViewportClient.IsCmdPressed() )
		&& Gesture.Key == Key;
}

static const FLevelViewportCommands& GetLevelViewportCommands()
{
	static FName LevelEditorName("LevelEditor");
	FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>( LevelEditorName );
	return LevelEditor.GetLevelViewportCommands();
}

void FLevelEditorViewportClient::SetCurrentViewport()
{
	// Set the current level editing viewport client to the dropped-in viewport client
	if (GCurrentLevelEditingViewportClient != this)
	{
		// Invalidate the old vp client to remove its special selection box
		if (GCurrentLevelEditingViewportClient)
		{
			GCurrentLevelEditingViewportClient->Invalidate();
		}
		GCurrentLevelEditingViewportClient = this;
	}
	Invalidate();
}

void FLevelEditorViewportClient::SetLastKeyViewport()
{
	// Store a reference to the last viewport that received a keypress.
	GLastKeyLevelEditingViewportClient = this;

	if (GCurrentLevelEditingViewportClient != this)
	{
		if (GCurrentLevelEditingViewportClient)
		{
			//redraw without yellow selection box
			GCurrentLevelEditingViewportClient->Invalidate();
		}
		//cause this viewport to redraw WITH yellow selection box
		Invalidate();
		GCurrentLevelEditingViewportClient = this;
	}
}

bool FLevelEditorViewportClient::InputKey(FViewport* Viewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	if (bDisableInput)
	{
		return true;
	}

	
	const int32	HitX = Viewport->GetMouseX();
	const int32	HitY = Viewport->GetMouseY();

	FInputEventState InputState( Viewport, Key, Event );

	SetLastKeyViewport();

	// Compute a view.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport,
		GetScene(),
		EngineShowFlags )
		.SetRealtimeUpdate( IsRealtime() ) );
	FSceneView* View = CalcSceneView( &ViewFamily );

	// Compute the click location.
	if ( InputState.IsAnyMouseButtonDown() )
	{
		const FViewportCursorLocation Cursor(View, this, HitX, HitY);
		const FActorPositionTraceResult TraceResult = FActorPositioning::TraceWorldForPositionWithDefault(Cursor, *View);
		GEditor->ClickLocation = TraceResult.Location;
		GEditor->ClickPlane = FPlane(TraceResult.Location, TraceResult.SurfaceNormal);

		// Snap the new location if snapping is enabled
		FSnappingUtils::SnapPointToGrid(GEditor->ClickLocation, FVector::ZeroVector);
	}

	if (GUnrealEd->ComponentVisManager.HandleInputKey(this, Viewport, Key, Event))
	{
		return true;
	}

	bool bHandled = FEditorViewportClient::InputKey(Viewport,ControllerId,Key,Event,AmountDepressed,bGamepad);

	// Handle input for the player height preview mode. 
	if (!InputState.IsMouseButtonEvent() && CommandAcceptsInput(*this, Key, GetLevelViewportCommands().EnablePreviewMesh))
	{
		// Holding down the backslash buttons turns on the mode. 
		if (Event == IE_Pressed)
		{
			GEditor->SetPreviewMeshMode(true);

			// If shift down, cycle between the preview meshes
			if (CommandAcceptsInput(*this, Key, GetLevelViewportCommands().CyclePreviewMesh))
			{
				GEditor->CyclePreviewMesh();
			}
		}
		// Releasing backslash turns off the mode. 
		else if (Event == IE_Released)
		{
			GEditor->SetPreviewMeshMode(false);
		}

		bHandled = true;
	}

	// Clear Duplicate Actors mode when ALT and all mouse buttons are released
	if ( !InputState.IsAltButtonPressed() && !InputState.IsAnyMouseButtonDown() )
	{
		bDuplicateActorsInProgress = false;
	}
	
	return bHandled;
}

void FLevelEditorViewportClient::TrackingStarted( const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge )
{
	// Begin transacting.  Give the current editor mode an opportunity to do the transacting.
	const bool bTrackingHandledExternally = ModeTools->StartTracking(this, Viewport);

	TrackingTransaction.End();

	// Re-initialize new tracking only if a new button was pressed, otherwise we continue the previous one.
	if ( InInputState.GetInputEvent() == IE_Pressed )
	{
		EInputEvent Event = InInputState.GetInputEvent();
		FKey Key = InInputState.GetKey();

		if ( InInputState.IsAltButtonPressed() && bDraggingByHandle )
		{
			if(Event == IE_Pressed && (Key == EKeys::LeftMouseButton || Key == EKeys::RightMouseButton) && !bDuplicateActorsInProgress)
			{
				// Set the flag so that the actors actors will be duplicated as soon as the widget is displaced.
				bDuplicateActorsOnNextDrag = true;
				bDuplicateActorsInProgress = true;
			}
		}
		else
		{
			bDuplicateActorsOnNextDrag = false;
		}
	}

	PreDragActorTransforms.Empty();
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It && !bIsTrackingBrushModification; ++It)
	{
		AActor* Actor = static_cast<AActor*>(*It);
		checkSlow( Actor->IsA(AActor::StaticClass()) );

		if( bIsDraggingWidget )
		{
			// Notify that this actor is beginning to move
			GEditor->BroadcastBeginObjectMovement( *Actor );
		}

		Widget->SetSnapEnabled(true);

		// See if any brushes are about to be transformed via their Widget
		TArray<AActor*> AttachedActors;
		Actor->GetAttachedActors( AttachedActors );
		const bool bExactClass = true;
		// First, check for selected brush actors, check the actors attached actors for brush actors as well.  If a parent actor moves, the bsp needs to be rebuilt
		ABrush* Brush = Cast< ABrush >( Actor );
		if (Brush && (!Brush->IsVolumeBrush() && !FActorEditorUtils::IsABuilderBrush(Actor)))
		{
			bIsTrackingBrushModification = true;
		}
		else // Next, check for selected groups actors that contain brushes
		{
			AGroupActor* GroupActor = Cast<AGroupActor>(Actor);
			if (GroupActor)
			{
				TArray<AActor*> GroupMembers;
				GroupActor->GetAllChildren(GroupMembers, true);
				for (int32 GroupMemberIdx = 0; GroupMemberIdx < GroupMembers.Num(); ++GroupMemberIdx)
				{
					Brush = Cast< ABrush >( GroupMembers[GroupMemberIdx] );
					if ( Brush && (!Brush->IsVolumeBrush() && !FActorEditorUtils::IsABuilderBrush(Actor)))
					{
						bIsTrackingBrushModification = true;
					}
				}
			}
		}
	}


	// Start a transformation transaction if required
	if( !bTrackingHandledExternally )
	{
		if( bIsDraggingWidget )
		{
			TrackingTransaction.TransCount++;

			FText TrackingDescription;

			switch( GetWidgetMode() )
			{
			case FWidget::WM_Translate:
				TrackingDescription = LOCTEXT("MoveActorsTransaction", "Move Actors");
				break;
			case FWidget::WM_Rotate:
				TrackingDescription = LOCTEXT("RotateActorsTransaction", "Rotate Actors");
				break;
			case FWidget::WM_Scale:
				TrackingDescription = LOCTEXT("ScaleActorsTransaction", "Scale Actors");
				break;
			case FWidget::WM_TranslateRotateZ:
				TrackingDescription = LOCTEXT("TranslateRotateZActorsTransaction", "Translate/RotateZ Actors");
				break;
			default:
				if( bNudge )
				{
					TrackingDescription = LOCTEXT("NudgeActorsTransaction", "Nudge Actors");
				}
			}

			if(!TrackingDescription.IsEmpty())
			{
				if(bNudge)
				{
					TrackingTransaction.Begin(TrackingDescription);
				}
				else
				{
					// If this hasn't begun due to a nudge, start it as a pending transaction so that it only really begins when the mouse is moved
					TrackingTransaction.BeginPending(TrackingDescription);
				}
			}

			if(TrackingTransaction.IsActive() || TrackingTransaction.IsPending())
			{
				// Suspend actor/component modification during each delta step to avoid recording unnecessary overhead into the transaction buffer
				GEditor->DisableDeltaModification(true);
			}
		}
	}

}

void FLevelEditorViewportClient::TrackingStopped()
{
	const bool AltDown = IsAltPressed();
	const bool ShiftDown = IsShiftPressed();
	const bool ControlDown = IsCtrlPressed();
	const bool LeftMouseButtonDown = Viewport->KeyState(EKeys::LeftMouseButton);
	const bool RightMouseButtonDown = Viewport->KeyState(EKeys::RightMouseButton);
	const bool MiddleMouseButtonDown = Viewport->KeyState(EKeys::MiddleMouseButton);

	// Only disable the duplicate on next drag flag if we actually dragged the mouse.
	bDuplicateActorsOnNextDrag = false;

	// here we check to see if anything of worth actually changed when ending our MouseMovement
	// If the TransCount > 0 (we changed something of value) so we need to call PostEditMove() on stuff
	// if we didn't change anything then don't call PostEditMove()
	bool bDidAnythingActuallyChange = false;

	// Stop transacting.  Give the current editor mode an opportunity to do the transacting.
	const bool bTransactingHandledByEditorMode = ModeTools->EndTracking(this, Viewport);
	if( !bTransactingHandledByEditorMode )
	{
		if( TrackingTransaction.TransCount > 0 )
		{
			bDidAnythingActuallyChange = true;
			TrackingTransaction.TransCount--;
		}
	}

	// Finish tracking a brush transform and update the Bsp
	if (bIsTrackingBrushModification)
	{
		bDidAnythingActuallyChange = HaveSelectedObjectsBeenChanged();

		bIsTrackingBrushModification = false;
		if ( bDidAnythingActuallyChange && bWidgetAxisControlledByDrag )
		{
			GEditor->RebuildAlteredBSP();
		}
	}

	// Notify the selected actors that they have been moved.
	// Don't do this if AddDelta was never called.
	if( bDidAnythingActuallyChange && MouseDeltaTracker->HasReceivedDelta() )
	{
		for ( FSelectionIterator It( GEditor->GetSelectedActorIterator() ) ; It ; ++It )
		{
			AActor* Actor = static_cast<AActor*>( *It );
			checkSlow( Actor->IsA(AActor::StaticClass()) );

			// Verify that the actor is in the same world as the viewport before moving it.
			if(GEditor->PlayWorld)
			{
				if(bIsSimulateInEditorViewport)
				{
					// If the Actor's outer (level) outer (world) is not the PlayWorld then it cannot be moved in this viewport.
					if( !(GEditor->PlayWorld == Actor->GetOuter()->GetOuter()) )
					{
						continue;
					}
				}
				else if( !(GEditor->EditorWorld == Actor->GetOuter()->GetOuter()) )
				{
					continue;
				}
			}

			Actor->PostEditMove( true );
			GEditor->BroadcastEndObjectMovement( *Actor );
		}

		if (!bPivotMovedIndependantly)
		{
			GUnrealEd->UpdatePivotLocationForSelection();
		}
	}

	// End the transaction here if one was started in StartTransaction()
	if( TrackingTransaction.IsActive() || TrackingTransaction.IsPending() )
	{
		if( !HaveSelectedObjectsBeenChanged())
		{
			TrackingTransaction.Cancel();
		}
		else
		{
			TrackingTransaction.End();
		}
		
		// Restore actor/component delta modification
		GEditor->DisableDeltaModification(false);
	}

	TArray<FEdMode*> ActiveModes; 
	ModeTools->GetActiveModes(ActiveModes);
	for( int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex )
	{
		// Also notify the current editing modes if they are interested.
		ActiveModes[ModeIndex]->ActorMoveNotify();
	}

	if( bDidAnythingActuallyChange )
	{
		FScopedLevelDirtied LevelDirtyCallback;
		LevelDirtyCallback.Request();

		RedrawAllViewportsIntoThisScene();
	}

	PreDragActorTransforms.Empty();
}

void FLevelEditorViewportClient::AbortTracking()
{
	if (TrackingTransaction.IsActive())
	{
		// Applying the global undo here will reset the drag operation
		GUndo->Apply();
		TrackingTransaction.Cancel();
		StopTracking();
	}
}

void FLevelEditorViewportClient::HandleViewportSettingChanged(FName PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULevelEditorViewportSettings, bUseSelectionOutline))
	{
		EngineShowFlags.SelectionOutline = GetDefault<ULevelEditorViewportSettings>()->bUseSelectionOutline;
	}
}

void FLevelEditorViewportClient::OnActorMoved(AActor* InActor)
{
	// Update the cameras from their locked actor (if any)
	UpdateLockedActorViewport(InActor, false);
}

void FLevelEditorViewportClient::NudgeSelectedObjects( const struct FInputEventState& InputState )
{
	FViewport* Viewport = InputState.GetViewport();
	EInputEvent Event = InputState.GetInputEvent();
	FKey Key = InputState.GetKey();

	const int32 MouseX = Viewport->GetMouseX();
	const int32 MouseY = Viewport->GetMouseY();

	if( Event == IE_Pressed || Event == IE_Repeat )
	{
		// If this is a pressed event, start tracking.
		if ( !bIsTracking && Event == IE_Pressed )
		{
			// without the check for !bIsTracking, the following code would cause a new transaction to be created
			// for each "nudge" that occurred while the key was held down.  Disabling this code prevents the transaction
			// from being constantly recreated while as long as the key is held, so that the entire move is considered an atomic action (and
			// doing undo reverts the entire movement, as opposed to just the last nudge that occurred while the key was held down)
			MouseDeltaTracker->StartTracking( this, MouseX, MouseY, InputState, true );
			bIsTracking = true;
		}

		FIntPoint StartMousePos;
		Viewport->GetMousePos( StartMousePos );
		FKey VirtualKey = EKeys::MouseX;
		EAxisList::Type VirtualAxis = GetHorizAxis();
		float VirtualDelta = GEditor->GetGridSize() * (Key == EKeys::Left?-1:1);
		if( Key == EKeys::Up || Key == EKeys::Down )
		{
			VirtualKey = EKeys::MouseY;
			VirtualAxis = GetVertAxis();
			VirtualDelta = GEditor->GetGridSize() * (Key == EKeys::Up?1:-1);
		}

		bWidgetAxisControlledByDrag = false;
		Widget->SetCurrentAxis( VirtualAxis );
		MouseDeltaTracker->AddDelta( this, VirtualKey , VirtualDelta, 1 );
		Widget->SetCurrentAxis( VirtualAxis );
		UpdateMouseDelta();
		Viewport->SetMouse( StartMousePos.X , StartMousePos.Y );
	}
	else if( bIsTracking && Event == IE_Released )
	{
		bWidgetAxisControlledByDrag = false;
		MouseDeltaTracker->EndTracking( this );
		bIsTracking = false;
		Widget->SetCurrentAxis( EAxisList::None );
	}

	RedrawAllViewportsIntoThisScene();
}


/**
 * Returns the horizontal axis for this viewport.
 */

EAxisList::Type FLevelEditorViewportClient::GetHorizAxis() const
{
	switch( GetViewportType() )
	{
	case LVT_OrthoXY:
		return EAxisList::X;
	case LVT_OrthoXZ:
		return EAxisList::X;
	case LVT_OrthoYZ:
		return EAxisList::Y;
	}

	return EAxisList::X;
}

/**
 * Returns the vertical axis for this viewport.
 */

EAxisList::Type FLevelEditorViewportClient::GetVertAxis() const
{
	switch( GetViewportType() )
	{
	case LVT_OrthoXY:
		return EAxisList::Y;
	case LVT_OrthoXZ:
		return EAxisList::Z;
	case LVT_OrthoYZ:
		return EAxisList::Z;
	}

	return EAxisList::Y;
}

//
//	FLevelEditorViewportClient::InputAxis
//

/**
 * Sets the current level editing viewport client when created and stores the previous one
 * When destroyed it sets the current viewport client back to the previous one.
 */
struct FScopedSetCurrentViewportClient
{
	FScopedSetCurrentViewportClient( FLevelEditorViewportClient* NewCurrentViewport )
	{
		PrevCurrentLevelEditingViewportClient = GCurrentLevelEditingViewportClient;
		GCurrentLevelEditingViewportClient = NewCurrentViewport;
	}
	~FScopedSetCurrentViewportClient()
	{
		GCurrentLevelEditingViewportClient = PrevCurrentLevelEditingViewportClient;
	}
private:
	FLevelEditorViewportClient* PrevCurrentLevelEditingViewportClient;
};

bool FLevelEditorViewportClient::InputAxis(FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	if (bDisableInput)
	{
		return true;
	}

	// @todo Slate: GCurrentLevelEditingViewportClient is switched multiple times per frame and since we draw the border in slate this effectively causes border to always draw on the last viewport

	FScopedSetCurrentViewportClient( this );

	return FEditorViewportClient::InputAxis(Viewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
}



static uint32 GetVolumeActorVisibilityId( const AActor& InActor )
{
	UClass* Class = InActor.GetClass();

	static TMap<UClass*, uint32 > ActorToIdMap;
	if( ActorToIdMap.Num() == 0 )
	{
		// Build a mapping of volume classes to ID's.  Do this only once
		TArray< UClass *> VolumeClasses;
		UUnrealEdEngine::GetSortedVolumeClasses(&VolumeClasses);
		for( int32 VolumeIdx = 0; VolumeIdx < VolumeClasses.Num(); ++VolumeIdx )
		{
			// An actors flag is just the index of the actor in the stored volume array shifted left to represent a unique bit.
			ActorToIdMap.Add( VolumeClasses[VolumeIdx], VolumeIdx );
		}
	}

	uint32* ActorID =  ActorToIdMap.Find( Class );

	// return 0 if the actor flag was not found, otherwise return the actual flag.  
	return ActorID ? *ActorID : 0;
}


/** 
 * Returns true if the passed in volume is visible in the viewport (due to volume actor visibility flags)
 *
 * @param Volume	The volume to check
 */
bool FLevelEditorViewportClient::IsVolumeVisibleInViewport( const AActor& VolumeActor ) const
{
	// We pass in the actor class for compatibility but we should make sure 
	// the function is only given volume actors
	//check( VolumeActor.IsA(AVolume::StaticClass()) );

	uint32 VolumeId = GetVolumeActorVisibilityId( VolumeActor );
	return VolumeActorVisibility[ VolumeId ];
}

void FLevelEditorViewportClient::RedrawAllViewportsIntoThisScene()
{
	// Invalidate all viewports, so the new gizmo is rendered in each one
	GEditor->RedrawLevelEditingViewports();
}

FVector FLevelEditorViewportClient::GetWidgetLocation() const
{
	FVector ComponentVisWidgetLocation;
	if (GUnrealEd->ComponentVisManager.GetWidgetLocation(this, ComponentVisWidgetLocation))
	{
		return ComponentVisWidgetLocation;
	}

	return FEditorViewportClient::GetWidgetLocation();
}

FMatrix FLevelEditorViewportClient::GetWidgetCoordSystem() const 
{
	FMatrix ComponentVisWidgetCoordSystem;
	if (GUnrealEd->ComponentVisManager.GetCustomInputCoordinateSystem(this, ComponentVisWidgetCoordSystem))
	{
		return ComponentVisWidgetCoordSystem;
	}

	return FEditorViewportClient::GetWidgetCoordSystem();
}

void FLevelEditorViewportClient::MoveLockedActorToCamera()
{
	// If turned on, move any selected actors to the cameras location/rotation
	TWeakObjectPtr<AActor> ActiveActorLock = GetActiveActorLock();
	if( ActiveActorLock.IsValid() )
	{
		if ( !ActiveActorLock->bLockLocation )
		{
			ActiveActorLock->SetActorLocation( GCurrentLevelEditingViewportClient->GetViewLocation(), false );
		}
		ActiveActorLock->SetActorRotation( GCurrentLevelEditingViewportClient->GetViewRotation() );

		ABrush* Brush = Cast< ABrush >( ActiveActorLock.Get() );
		if( Brush )
		{
			Brush->SetNeedRebuild( Brush->GetLevel() );
		}

		FScopedLevelDirtied LevelDirtyCallback;
		LevelDirtyCallback.Request();

		RedrawAllViewportsIntoThisScene();
	}
}

bool FLevelEditorViewportClient::HaveSelectedObjectsBeenChanged() const
{
	return ( TrackingTransaction.TransCount > 0 || TrackingTransaction.IsActive() ) && MouseDeltaTracker->HasReceivedDelta();
}

void FLevelEditorViewportClient::MoveCameraToLockedActor()
{
	// If turned on, move cameras location/rotation to the selected actors
	if( GetActiveActorLock().IsValid() )
	{
		SetViewLocation( GetActiveActorLock()->GetActorLocation() );
		SetViewRotation( GetActiveActorLock()->GetActorRotation() );
		Invalidate();
	}
}

bool FLevelEditorViewportClient::IsActorLocked(const TWeakObjectPtr<AActor> InActor) const
{
	return (InActor.IsValid() && GetActiveActorLock() == InActor);
}

bool FLevelEditorViewportClient::IsAnyActorLocked() const
{
	return GetActiveActorLock().IsValid();
}

void FLevelEditorViewportClient::UpdateLockedActorViewports(const AActor* InActor, const bool bCheckRealtime)
{
	// Loop through all the other viewports, checking to see if the camera needs updating based on the locked actor
	for( int32 ViewportIndex = 0; ViewportIndex < GEditor->LevelViewportClients.Num(); ++ViewportIndex )
	{
		FLevelEditorViewportClient* Client = GEditor->LevelViewportClients[ViewportIndex];
		if( Client && Client != this )
		{
			Client->UpdateLockedActorViewport(InActor, bCheckRealtime);
		}
	}
}

void FLevelEditorViewportClient::UpdateLockedActorViewport(const AActor* InActor, const bool bCheckRealtime)
{
	// If this viewport has the actor locked and we need to update the camera, then do so
	if( IsActorLocked(InActor) && ( !bCheckRealtime || IsRealtime() ) )
	{
		MoveCameraToLockedActor();
	}
}


void FLevelEditorViewportClient::ApplyDeltaToActors(const FVector& InDrag,
													const FRotator& InRot,
													const FVector& InScale)
{
	if( (InDrag.IsZero() && InRot.IsZero() && InScale.IsZero()) )
	{
		return;
	}

	FVector ModifiedScale = InScale;
	// If we are scaling, we need to change the scaling factor a bit to properly align to grid.

	if( GEditor->UsePercentageBasedScaling() )
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		const bool bScalingActors = !InScale.IsNearlyZero();

		if( bScalingActors )
		{
			/* @todo: May reenable this form of calculating scaling factors later on.
			// Calculate a bounding box for the actors.
			FBox ActorsBoundingBox( 0 );

			for ( FSelectionIterator It( GEditor->GetSelectedActorIterator() ) ; It ; ++It )
			{
				AActor* Actor = static_cast<AActor*>( *It );
				checkSlow( Actor->IsA(AActor::StaticClass()) );

				const FBox ActorsBox = Actor->GetComponentsBoundingBox( true );
				ActorsBoundingBox += ActorsBox;
			}

			const FVector BoxExtent = ActorsBoundingBox.GetExtent();

			for (int32 Idx=0; Idx < 3; Idx++)
			{
				ModifiedScale[Idx] = InScale[Idx] / BoxExtent[Idx];
			}
			*/

			ModifiedScale = InScale * ((GEditor->GetScaleGridSize() / 100.0f) / GEditor->GetGridSize());
		}
	}

	// Transact the actors.
	GEditor->NoteActorMovement();

	TArray<AGroupActor*> ActorGroups;

	// Apply the deltas to any selected actors.
	for ( FSelectionIterator It( GEditor->GetSelectedActorIterator() ) ; It ; ++It )
	{
		AActor* Actor = static_cast<AActor*>( *It );
		checkSlow( Actor->IsA(AActor::StaticClass()) );

		// Verify that the actor is in the same world as the viewport before moving it.
		if(GEditor->PlayWorld)
		{
			if(bIsSimulateInEditorViewport)
			{
				// If the Actor's outer (level) outer (world) is not the PlayWorld then it cannot be moved in this viewport.
				if( !(GEditor->PlayWorld == Actor->GetOuter()->GetOuter()) )
				{
					continue;
				}
			}
			else if( !(GEditor->EditorWorld == Actor->GetOuter()->GetOuter()) )
			{
				continue;
			}
		}

		if ( !Actor->bLockLocation )
		{
			// find topmost selected group
			AGroupActor* ParentGroup = AGroupActor::GetRootForActor(Actor, true, true);
			if(ParentGroup && GEditor->bGroupingActive )
			{
				ActorGroups.AddUnique(ParentGroup);
			}
			else
			{
				// Finally, verify that no actor in the parent hierarchy is also selected
				bool bHasParentInSelection = false;
				AActor* ParentActor = Actor->GetAttachParentActor();
				while(ParentActor!=NULL && !bHasParentInSelection)
				{
					if(ParentActor->IsSelected())
					{
						bHasParentInSelection = true;
					}
					ParentActor = ParentActor->GetAttachParentActor();
				}
				if(!bHasParentInSelection)
				{
					ApplyDeltaToActor( Actor, InDrag, InRot, ModifiedScale );
				}
			}
		}
	}
	AGroupActor::RemoveSubGroupsFromArray(ActorGroups);
	for(int32 ActorGroupsIndex=0; ActorGroupsIndex<ActorGroups.Num(); ++ActorGroupsIndex)
	{
		ActorGroups[ActorGroupsIndex]->GroupApplyDelta(this, InDrag, InRot, ModifiedScale);
	}
}


/** Helper function for ModifyScale - Convert the active Dragging Axis to per-axis flags */
static void CheckActiveAxes( EAxisList::Type DraggingAxis, bool bActiveAxes[3] )
{
	bActiveAxes[0] = bActiveAxes[1] = bActiveAxes[2] = false;
	switch(DraggingAxis)
	{
	default:
	case EAxisList::None:
		break;
	case EAxisList::X:
		bActiveAxes[0] = true;
		break;
	case EAxisList::Y:
		bActiveAxes[1] = true;
		break;
	case EAxisList::Z:
		bActiveAxes[2] = true;
		break;
	case EAxisList::XYZ:
	case EAxisList::All:
	case EAxisList::Screen:
		bActiveAxes[0] = bActiveAxes[1] = bActiveAxes[2] = true;
		break;
	case EAxisList::XY:
		bActiveAxes[0] = bActiveAxes[1] = true;
		break;
	case EAxisList::XZ:
		bActiveAxes[0] = bActiveAxes[2] = true;
		break;
	case EAxisList::YZ:
		bActiveAxes[1] = bActiveAxes[2] = true;
		break;
	}
}

/** Helper function for ModifyScale - Check scale criteria to see if this is allowed, returns modified absolute scale*/
static float CheckScaleValue( float ScaleDeltaToCheck, float CurrentScaleFactor, float CurrentExtent, bool bCheckSmallExtent, bool bSnap )
{
	float AbsoluteScaleValue = ScaleDeltaToCheck + CurrentScaleFactor;
	if( bSnap )
	{
		AbsoluteScaleValue = FMath::GridSnap( AbsoluteScaleValue, GEditor->GetScaleGridSize() );
	}
	// In some situations CurrentExtent can be 0 (eg: when scaling a plane in Z), this causes a divide by 0 that we need to avoid.
	if(CurrentExtent < KINDA_SMALL_NUMBER) {
		return AbsoluteScaleValue;
	}
	float UnscaledExtent = CurrentExtent / CurrentScaleFactor;
	float ScaledExtent = UnscaledExtent * AbsoluteScaleValue;

	if( ( FMath::Square( ScaledExtent ) ) > BIG_NUMBER )	// cant get too big...
	{
		return CurrentScaleFactor;
	}
	else if( bCheckSmallExtent && 
			(FMath::Abs( ScaledExtent ) < MIN_ACTOR_BOUNDS_EXTENT * 0.5f ||		// ...or too small (apply sign in this case)...
			( CurrentScaleFactor < 0.0f ) != ( AbsoluteScaleValue < 0.0f )) )	// ...also cant cross the zero boundary
	{
		return ( ( MIN_ACTOR_BOUNDS_EXTENT * 0.5 ) / UnscaledExtent ) * ( CurrentScaleFactor < 0.0f ? -1.0f : 1.0f );
	}

	return AbsoluteScaleValue;
}

/** 
 * Helper function for ValidateScale().
 * If the setting is enabled, this function will appropriately re-scale the scale delta so that 
 * proportions are preserved when snapping.
 * @param	CurrentScale	The object's current scale
 * @param	bActiveAxes		The axes that are active when scaling interactively.
 * @param	InOutScaleDelta	The scale delta we are potentially transforming.
 * @return true if the axes should be snapped individually, according to the snap setting (i.e. this function had no effect)
 */
static bool OptionallyPreserveNonUniformScale(const FVector& InCurrentScale, const bool bActiveAxes[3], FVector& InOutScaleDelta)
{
	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();

	if(ViewportSettings->SnapScaleEnabled && ViewportSettings->PreserveNonUniformScale)
	{
		// when using 'auto-precision', we take the max component & snap its scale, then proportionally scale the other components
		float MaxComponentSum = -1.0f;
		int32 MaxAxisIndex = -1;
		for( int Axis = 0; Axis < 3; ++Axis )
		{
			if( bActiveAxes[Axis] ) 
			{
				const float AbsScale = FMath::Abs(InOutScaleDelta[Axis] + InCurrentScale[Axis]);
				if(AbsScale > MaxComponentSum)
				{
					MaxAxisIndex = Axis;
					MaxComponentSum = AbsScale;
				}
			}
		}

		check(MaxAxisIndex != -1);

		float AbsoluteScaleValue = FMath::GridSnap( InCurrentScale[MaxAxisIndex] + InOutScaleDelta[MaxAxisIndex], GEditor->GetScaleGridSize() );
		float ScaleRatioMax = InCurrentScale[MaxAxisIndex] == 0.0f ? 1.0f : AbsoluteScaleValue / InCurrentScale[MaxAxisIndex];
		for( int Axis = 0; Axis < 3; ++Axis )
		{
			if( bActiveAxes[Axis] ) 
			{
				if(Axis == MaxAxisIndex)
				{
					InOutScaleDelta[Axis] = AbsoluteScaleValue - InCurrentScale[Axis];
				}
				else
				{
					InOutScaleDelta[Axis] = (InCurrentScale[Axis] * ScaleRatioMax) - InCurrentScale[Axis];
				}
			}
		}

		return false;
	}

	return ViewportSettings->SnapScaleEnabled;
}

/** Helper function for ModifyScale - Check scale criteria to see if this is allowed */
void FLevelEditorViewportClient::ValidateScale( const FVector& InCurrentScale, const FVector& InBoxExtent, FVector& InOutScaleDelta, bool bInCheckSmallExtent ) const
{
	// get the axes that are active in this operation
	bool bActiveAxes[3];
	CheckActiveAxes( Widget != NULL ? Widget->GetCurrentAxis() : EAxisList::None, bActiveAxes );

	bool bSnapAxes = OptionallyPreserveNonUniformScale(InCurrentScale, bActiveAxes, InOutScaleDelta);
	
	// check each axis
	for( int Axis = 0; Axis < 3; ++Axis )
	{
		if( bActiveAxes[Axis] ) 
		{
			float ModifiedScaleAbsolute = CheckScaleValue( InOutScaleDelta[Axis], InCurrentScale[Axis], InBoxExtent[Axis], bInCheckSmallExtent, bSnapAxes );
			InOutScaleDelta[Axis] = ModifiedScaleAbsolute - InCurrentScale[Axis];
		}
		else
		{
			InOutScaleDelta[Axis] = 0.0f;
		}
	}
}

void FLevelEditorViewportClient::ModifyScale( AActor* InActor, FVector& ScaleDelta, bool bCheckSmallExtent ) const
{
	if( InActor->GetRootComponent() )
	{
		const FVector CurrentScale = InActor->GetRootComponent()->RelativeScale3D;

		const FBox LocalBox = InActor->GetComponentsBoundingBox( true );
		const FVector ScaledExtents = LocalBox.GetExtent() * CurrentScale;
		ValidateScale( CurrentScale, ScaledExtents, ScaleDelta, bCheckSmallExtent );

		if( ScaleDelta.IsNearlyZero() )
		{
			ScaleDelta = FVector::ZeroVector;
		}
	}
}

void FLevelEditorViewportClient::ModifyScale( USceneComponent* InComponent, FVector& ScaleDelta ) const
{
	ValidateScale( InComponent->RelativeScale3D, InComponent->Bounds.GetBox().GetExtent(), ScaleDelta );

	if( ScaleDelta.IsNearlyZero() )
	{
		ScaleDelta = FVector::ZeroVector;
	}
}

//
//	FLevelEditorViewportClient::ApplyDeltaToActor
//

void FLevelEditorViewportClient::ApplyDeltaToActor( AActor* InActor, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale )
{
	// If we are scaling, we may need to change the scaling factor a bit to properly align to the grid.
	FVector ModifiedDeltaScale = InDeltaScale;

	// we dont scale actors when we only have a very small scale change
	if( !InDeltaScale.IsNearlyZero() )
	{
		if(!GEditor->UsePercentageBasedScaling())
		{
			ModifyScale( InActor, ModifiedDeltaScale, Cast< ABrush >( InActor ) != NULL );
		}
	}
	else
	{
		ModifiedDeltaScale = FVector::ZeroVector;
	}

	GEditor->ApplyDeltaToActor(
		InActor,
		true,
		&InDeltaDrag,
		&InDeltaRot,
		&ModifiedDeltaScale,
		IsAltPressed(),
		IsShiftPressed(),
		IsCtrlPressed() );

	// Update the cameras from their locked actor (if any) only if the viewport is realtime enabled
	UpdateLockedActorViewports(InActor, true);
}

EMouseCursor::Type FLevelEditorViewportClient::GetCursor(FViewport* Viewport,int32 X,int32 Y)
{
	EMouseCursor::Type CursorType = FEditorViewportClient::GetCursor(Viewport,X,Y);

	HHitProxy* HitProxy = Viewport->GetHitProxy(X,Y);

	// Don't select widget axes by mouse over while they're being controlled by a mouse drag.
	if( Viewport->IsCursorVisible() && !bWidgetAxisControlledByDrag && !HitProxy )
	{
		if( HoveredObjects.Num() > 0 )
	{
			ClearHoverFromObjects();
			Invalidate( false, false );
	}
	}

	return CursorType;

}

FViewportCursorLocation FLevelEditorViewportClient::GetCursorWorldLocationFromMousePos()
{
	// Create the scene view context
	FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues(
		Viewport, 
		GetScene(),
		EngineShowFlags )
		.SetRealtimeUpdate( IsRealtime() ));

	// Calculate the scene view
	FSceneView* View = CalcSceneView( &ViewFamily );

	// Construct an FViewportCursorLocation which calculates world space postion from the scene view and mouse pos.
	return FViewportCursorLocation( View, 
		this, 
		Viewport->GetMouseX(), 
		Viewport->GetMouseY()
		);
}



/**
 * Called when the mouse is moved while a window input capture is in effect
 *
 * @param	InViewport	Viewport that captured the mouse input
 * @param	InMouseX	New mouse cursor X coordinate
 * @param	InMouseY	New mouse cursor Y coordinate
 */
void FLevelEditorViewportClient::CapturedMouseMove( FViewport* InViewport, int32 InMouseX, int32 InMouseY )
{
	// Commit to any pending transactions now
	TrackingTransaction.PromotePendingToActive();

	FEditorViewportClient::CapturedMouseMove(InViewport, InMouseX, InMouseY);
}


/**
 * Checks if the mouse is hovered over a hit proxy and decides what to do.
 */
void FLevelEditorViewportClient::CheckHoveredHitProxy( HHitProxy* HoveredHitProxy )
{
	FEditorViewportClient::CheckHoveredHitProxy(HoveredHitProxy);

	// We'll keep track of changes to hovered objects as the cursor moves
	const bool bUseHoverFeedback = GEditor != NULL && GetDefault<ULevelEditorViewportSettings>()->bEnableViewportHoverFeedback;
	TSet< FViewportHoverTarget > NewHoveredObjects;

	// If the cursor is visible over level viewports, then we'll check for new objects to be hovered over
	if( bUseHoverFeedback && HoveredHitProxy )
	{
		// Set mouse hover cue for objects under the cursor
		if (HoveredHitProxy->IsA(HActor::StaticGetType()) || HoveredHitProxy->IsA(HBSPBrushVert::StaticGetType()))
		{
			// Hovered over an actor
			AActor* ActorUnderCursor = NULL;
			if (HoveredHitProxy->IsA(HActor::StaticGetType()))
			{
				HActor* ActorHitProxy = static_cast<HActor*>(HoveredHitProxy);
				ActorUnderCursor = ActorHitProxy->Actor;
			}
			else if (HoveredHitProxy->IsA(HBSPBrushVert::StaticGetType()))
			{
				HBSPBrushVert* ActorHitProxy = static_cast<HBSPBrushVert*>(HoveredHitProxy);
				ActorUnderCursor = ActorHitProxy->Brush.Get();
			}

			if( ActorUnderCursor != NULL  )
			{
				// Check to see if the actor under the cursor is part of a group.  If so, we will how a hover cue the whole group
				AGroupActor* GroupActor = AGroupActor::GetRootForActor( ActorUnderCursor, true, false );

				if( GroupActor && GEditor->bGroupingActive)
				{
					// Get all the actors in the group and add them to the list of objects to show a hover cue for.
					TArray<AActor*> ActorsInGroup;
					GroupActor->GetGroupActors( ActorsInGroup, true );
					for( int32 ActorIndex = 0; ActorIndex < ActorsInGroup.Num(); ++ActorIndex )
					{
						NewHoveredObjects.Add( FViewportHoverTarget( ActorsInGroup[ActorIndex] ) );
					}
				}
				else
				{
					NewHoveredObjects.Add( FViewportHoverTarget( ActorUnderCursor ) );
				}
			}
		}
		else if( HoveredHitProxy->IsA( HModel::StaticGetType() ) )
		{
			// Hovered over a model (BSP surface)
			HModel* ModelHitProxy = static_cast< HModel* >( HoveredHitProxy );
			UModel* ModelUnderCursor = ModelHitProxy->GetModel();
			if( ModelUnderCursor != NULL )
			{
				FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
					Viewport, 
					GetScene(),
					EngineShowFlags)
					.SetRealtimeUpdate( IsRealtime() ));
				FSceneView* SceneView = CalcSceneView( &ViewFamily );

				uint32 SurfaceIndex = INDEX_NONE;
				if( ModelHitProxy->ResolveSurface( SceneView, CachedMouseX, CachedMouseY, SurfaceIndex ) )
				{
					FBspSurf& Surf = ModelUnderCursor->Surfs[ SurfaceIndex ];
					Surf.PolyFlags |= PF_Hovered;

					NewHoveredObjects.Add( FViewportHoverTarget( ModelUnderCursor, SurfaceIndex ) );
				}
			}
		}
	}


	// Check to see if there are any hovered objects that need to be updated
	{
		bool bAnyHoverChanges = false;
		if( NewHoveredObjects.Num() > 0 )
		{
			for( TSet<FViewportHoverTarget>::TIterator It( HoveredObjects ); It; ++It )
			{
				FViewportHoverTarget& OldHoverTarget = *It;
				if( !NewHoveredObjects.Contains( OldHoverTarget ) )
				{
					// Remove hover effect from object that no longer needs it
					RemoveHoverEffect( OldHoverTarget );
					HoveredObjects.Remove( OldHoverTarget );

					bAnyHoverChanges = true;
				}
			}
		}

		for( TSet<FViewportHoverTarget>::TIterator It( NewHoveredObjects ); It; ++It )
		{
			FViewportHoverTarget& NewHoverTarget = *It;
			if( !HoveredObjects.Contains( NewHoverTarget ) )
			{
				// Add hover effect to this object
				AddHoverEffect( NewHoverTarget );
				HoveredObjects.Add( NewHoverTarget );

				bAnyHoverChanges = true;
			}
		}


		// Redraw the viewport if we need to
		if( bAnyHoverChanges )
		{
			// NOTE: We're only redrawing the viewport that the mouse is over.  We *could* redraw all viewports
			//		 so the hover effect could be seen in all potential views, but it will be slower.
			RedrawRequested( Viewport );
		}
	}
}

bool FLevelEditorViewportClient::GetActiveSafeFrame(float& OutAspectRatio) const
{
	if (!IsOrtho())
	{
		const UCameraComponent* CameraComponent = GetCameraComponentForView();
		if (CameraComponent && CameraComponent->bConstrainAspectRatio)
		{
			OutAspectRatio = CameraComponent->AspectRatio;
			return true;
		}
	}

	return false;
}

/** 
 * Renders a view frustum specified by the provided frustum parameters
 *
 * @param	PDI					PrimitiveDrawInterface to use to draw the view frustum
 * @param	FrustumColor		Color to draw the view frustum in
 * @param	FrustumAngle		Angle of the frustum
 * @param	FrustumAspectRatio	Aspect ratio of the frustum
 * @param	FrustumStartDist	Start distance of the frustum
 * @param	FrustumEndDist		End distance of the frustum
 * @param	InViewMatrix		View matrix to use to draw the frustum
 */
static void RenderViewFrustum( FPrimitiveDrawInterface* PDI,
								const FLinearColor& FrustumColor,
								float FrustumAngle,
								float FrustumAspectRatio,
								float FrustumStartDist,
								float FrustumEndDist,
								const FMatrix& InViewMatrix)
{
	FVector Direction(0,0,1);
	FVector LeftVector(1,0,0);
	FVector UpVector(0,1,0);

	FVector Verts[8];

	// FOVAngle controls the horizontal angle.
	float HozHalfAngle = (FrustumAngle) * ((float)PI/360.f);
	float HozLength = FrustumStartDist * FMath::Tan(HozHalfAngle);
	float VertLength = HozLength/FrustumAspectRatio;

	// near plane verts
	Verts[0] = (Direction * FrustumStartDist) + (UpVector * VertLength) + (LeftVector * HozLength);
	Verts[1] = (Direction * FrustumStartDist) + (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[2] = (Direction * FrustumStartDist) - (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[3] = (Direction * FrustumStartDist) - (UpVector * VertLength) + (LeftVector * HozLength);

	HozLength = FrustumEndDist * FMath::Tan(HozHalfAngle);
	VertLength = HozLength/FrustumAspectRatio;

	// far plane verts
	Verts[4] = (Direction * FrustumEndDist) + (UpVector * VertLength) + (LeftVector * HozLength);
	Verts[5] = (Direction * FrustumEndDist) + (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[6] = (Direction * FrustumEndDist) - (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[7] = (Direction * FrustumEndDist) - (UpVector * VertLength) + (LeftVector * HozLength);

	for( int32 x = 0 ; x < 8 ; ++x )
	{
		Verts[x] = InViewMatrix.InverseFast().TransformPosition( Verts[x] );
	}

	const uint8 PrimitiveDPG = SDPG_Foreground;
	PDI->DrawLine( Verts[0], Verts[1], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[1], Verts[2], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[2], Verts[3], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[3], Verts[0], FrustumColor, PrimitiveDPG );

	PDI->DrawLine( Verts[4], Verts[5], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[5], Verts[6], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[6], Verts[7], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[7], Verts[4], FrustumColor, PrimitiveDPG );

	PDI->DrawLine( Verts[0], Verts[4], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[1], Verts[5], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[2], Verts[6], FrustumColor, PrimitiveDPG );
	PDI->DrawLine( Verts[3], Verts[7], FrustumColor, PrimitiveDPG );
}

void FLevelEditorViewportClient::Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI)
{
	FMemMark Mark(FMemStack::Get());

	FEditorViewportClient::Draw(View,PDI);

	DrawBrushDetails(View, PDI);
	AGroupActor::DrawBracketsForGroups(PDI, Viewport);

	if (EngineShowFlags.StreamingBounds)
	{
		DrawTextureStreamingBounds(View, PDI);
	}

	// Determine if a view frustum should be rendered in the viewport.
	// The frustum should definitely be rendered if the viewport has a view parent.
	bool bRenderViewFrustum = ViewState.GetReference()->HasViewParent();

	// If the viewport doesn't have a view parent, a frustum still should be drawn anyway if the viewport is ortho and level streaming
	// volume previs is enabled in some viewport
	if ( !bRenderViewFrustum && IsOrtho() )
	{
		for ( int32 ViewportClientIndex = 0; ViewportClientIndex < GEditor->LevelViewportClients.Num(); ++ViewportClientIndex )
		{
			const FLevelEditorViewportClient* CurViewportClient = GEditor->LevelViewportClients[ ViewportClientIndex ];
			if ( CurViewportClient && IsPerspective() && GetDefault<ULevelEditorViewportSettings>()->bLevelStreamingVolumePrevis )
			{
				bRenderViewFrustum = true;
				break;
			}
		}
	}

	// Draw the view frustum of the view parent or level streaming volume previs viewport, if necessary
	if ( bRenderViewFrustum )
	{
		RenderViewFrustum( PDI, FLinearColor(1.0,0.0,1.0,1.0),
			GPerspFrustumAngle,
			GPerspFrustumAspectRatio,
			GPerspFrustumStartDist,
			GPerspFrustumEndDist,
			GPerspViewMatrix);
	}


	if (IsPerspective())
	{
		DrawStaticLightingDebugInfo( View, PDI );
	}

	if ( GEditor->bEnableSocketSnapping )
	{
		const bool bGameViewMode = View->Family->EngineShowFlags.Game && !GEditor->bDrawSocketsInGMode;

		for( FActorIterator It(GetWorld()); It; ++It )
		{
			AActor* Actor = *It;

			if (bGameViewMode || Actor->IsHiddenEd())
			{
				// Don't display sockets on hidden actors...
				continue;
			}

			TInlineComponentArray<USceneComponent*> Components;
			Actor->GetComponents(Components);

			for (int32 ComponentIndex = 0 ; ComponentIndex < Components.Num(); ++ComponentIndex)
			{
				USceneComponent* SceneComponent = Components[ComponentIndex];
				if (SceneComponent->HasAnySockets())
				{
					TArray<FComponentSocketDescription> Sockets;
					SceneComponent->QuerySupportedSockets(Sockets);

					for (int32 SocketIndex = 0; SocketIndex < Sockets.Num() ; ++SocketIndex)
					{
						FComponentSocketDescription& Socket = Sockets[SocketIndex];

						if (Socket.Type == EComponentSocketType::Socket)
						{
							const FTransform SocketTransform = SceneComponent->GetSocketTransform(Socket.Name);

							const float DiamondSize = 2.0f;
							const FColor DiamondColor(255,128,128);

							PDI->SetHitProxy( new HLevelSocketProxy( *It, SceneComponent, Socket.Name ) );
							DrawWireDiamond( PDI, SocketTransform.ToMatrixWithScale(), DiamondSize, DiamondColor, SDPG_Foreground );
							PDI->SetHitProxy( NULL );
						}
					}
				}
			}
		}
	}

	if( this == GCurrentLevelEditingViewportClient )
	{
		FSnappingUtils::DrawSnappingHelpers( View, PDI );
	}

	if(GUnrealEd != NULL && !IsInGameView())
	{
		GUnrealEd->DrawComponentVisualizers(View, PDI);
	}

	if (GEditor->bDrawParticleHelpers == true)
	{
		if (View->Family->EngineShowFlags.Game)
		{
			extern ENGINE_API void DrawParticleSystemHelpers(const FSceneView* View,FPrimitiveDrawInterface* PDI);
			DrawParticleSystemHelpers(View, PDI);
		}
	}


	Mark.Pop();
}

void FLevelEditorViewportClient::DrawBrushDetails(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	if (GEditor->bShowBrushMarkerPolys)
	{
		// Draw translucent polygons on brushes and volumes

		for (TActorIterator<ABrush> It(GetWorld()); It; ++It)
		{
			ABrush* Brush = *It;

			// Brush->Brush is checked to safe from brushes that were created without having their brush members attached.
			if (Brush->Brush && (FActorEditorUtils::IsABuilderBrush(Brush) || Brush->IsVolumeBrush()) && ModeTools->GetSelectedActors()->IsSelected(Brush))
			{
				// Build a mesh by basically drawing the triangles of each 
				FDynamicMeshBuilder MeshBuilder;
				int32 VertexOffset = 0;

				for (int32 PolyIdx = 0; PolyIdx < Brush->Brush->Polys->Element.Num(); ++PolyIdx)
				{
					const FPoly* Poly = &Brush->Brush->Polys->Element[PolyIdx];

					if (Poly->Vertices.Num() > 2)
					{
						const FVector Vertex0 = Poly->Vertices[0];
						FVector Vertex1 = Poly->Vertices[1];

						MeshBuilder.AddVertex(Vertex0, FVector2D::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FColor::White);
						MeshBuilder.AddVertex(Vertex1, FVector2D::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FColor::White);

						for (int32 VertexIdx = 2; VertexIdx < Poly->Vertices.Num(); ++VertexIdx)
						{
							const FVector Vertex2 = Poly->Vertices[VertexIdx];
							MeshBuilder.AddVertex(Vertex2, FVector2D::ZeroVector, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FColor::White);
							MeshBuilder.AddTriangle(VertexOffset, VertexOffset + VertexIdx, VertexOffset + VertexIdx - 1);
							Vertex1 = Vertex2;
						}

						// Increment the vertex offset so the next polygon uses the correct vertex indices.
						VertexOffset += Poly->Vertices.Num();
					}
				}

				// Allocate the material proxy and register it so it can be deleted properly once the rendering is done with it.
				FDynamicColoredMaterialRenderProxy* MaterialProxy = new FDynamicColoredMaterialRenderProxy(GEngine->EditorBrushMaterial->GetRenderProxy(false), Brush->GetWireColor());
				PDI->RegisterDynamicResource(MaterialProxy);

				// Flush the mesh triangles.
				MeshBuilder.Draw(PDI, Brush->ActorToWorld().ToMatrixWithScale(), MaterialProxy, SDPG_World, 0.f);
			}
		}
	}
	
	if (ModeTools->ShouldDrawBrushVertices() && !IsInGameView())
	{
		UTexture2D* VertexTexture = GEngine->DefaultBSPVertexTexture;
		const float TextureSizeX = VertexTexture->GetSizeX() * 0.170f;
		const float TextureSizeY = VertexTexture->GetSizeY() * 0.170f;

		for (FSelectionIterator It(*ModeTools->GetSelectedActors()); It; ++It)
		{
			AActor* SelectedActor = static_cast<AActor*>(*It);
			checkSlow(SelectedActor->IsA(AActor::StaticClass()));

			ABrush* Brush = Cast< ABrush >(SelectedActor);
			if (Brush && Brush->Brush && !FActorEditorUtils::IsABuilderBrush(Brush))
			{
				for (int32 p = 0; p < Brush->Brush->Polys->Element.Num(); ++p)
				{
					FTransform BrushTransform = Brush->ActorToWorld();

					FPoly* poly = &Brush->Brush->Polys->Element[p];
					for (int32 VertexIndex = 0; VertexIndex < poly->Vertices.Num(); ++VertexIndex)
					{
						const FVector& PolyVertex = poly->Vertices[VertexIndex];
						const FVector WorldLocation = BrushTransform.TransformPosition(PolyVertex);

						const float Scale = View->WorldToScreen(WorldLocation).W * (4.0f / View->ViewRect.Width() / View->ViewMatrices.ProjMatrix.M[0][0]);

						const FColor Color(Brush->GetWireColor());
						PDI->SetHitProxy(new HBSPBrushVert(Brush, &poly->Vertices[VertexIndex]));

						PDI->DrawSprite(WorldLocation, TextureSizeX * Scale, TextureSizeY * Scale, VertexTexture->Resource, Color, SDPG_World, 0.0f, 0.0f, 0.0f, 0.0f, SE_BLEND_Masked);

						PDI->SetHitProxy(NULL);
					}
				}
			}
		}
	}
}

/**
 * Updates the audio listener for this viewport 
 *
 * @param View	The scene view to use when calculate the listener position
 */
void FLevelEditorViewportClient::UpdateAudioListener( const FSceneView& View )
{
	FAudioDevice* AudioDevice = GEditor->GetAudioDevice();

	// AudioDevice may not exist. For example if we are in -nosound mode
	if( AudioDevice )
	{
		if( GetWorld() )
		{
			FReverbSettings ReverbSettings;
			FInteriorSettings InteriorSettings;
			const FVector& ViewLocation = GetViewLocation();

			class AAudioVolume* AudioVolume = GetWorld()->GetAudioSettings( ViewLocation, &ReverbSettings, &InteriorSettings );

			FMatrix CameraToWorld = View.ViewMatrices.ViewMatrix.InverseFast();
			FVector ProjUp = CameraToWorld.TransformVector(FVector(0, 1000, 0));
			FVector ProjRight = CameraToWorld.TransformVector(FVector(1000, 0, 0));

			FTransform ListenerTransform(FRotationMatrix::MakeFromZY(ProjUp, ProjRight));
			ListenerTransform.SetTranslation(ViewLocation);
			ListenerTransform.NormalizeRotation();

			AudioDevice->SetListener( 0, ListenerTransform, 0.f, AudioVolume, InteriorSettings );
			AudioDevice->SetReverbSettings( AudioVolume, ReverbSettings );
		}
	}
}

void FLevelEditorViewportClient::SetupViewForRendering( FSceneViewFamily& ViewFamily, FSceneView& View )
{
	FEditorViewportClient::SetupViewForRendering( ViewFamily, View );

	ViewFamily.bDrawBaseInfo = bDrawBaseInfo;

	// Don't use fading or color scaling while we're in light complexity mode, since it may change the colors!
	if(!ViewFamily.EngineShowFlags.LightComplexity)
	{
		if(bEnableFading)
		{
			View.OverlayColor = FadeColor;
			View.OverlayColor.A = FMath::Clamp(FadeAmount, 0.f, 1.f);
		}

		if(bEnableColorScaling)
		{
				View.ColorScale = FLinearColor(ColorScale.X,ColorScale.Y,ColorScale.Z);
		}
	}

	if (ModeTools->GetActiveMode(FBuiltinEditorModes::EM_InterpEdit) == 0 || !AllowMatineePreview())
	{
		// in the editor, disable camera motion blur and other rendering features that rely on the former frame
		// unless the view port is Matinee controlled
		ViewFamily.EngineShowFlags.CameraInterpolation = 0;
		// keep the image sharp - ScreenPercentage is an optimization and should not affect the editor
		ViewFamily.EngineShowFlags.ScreenPercentage = 0;
	}

	TSharedPtr<FDragDropOperation> DragOperation = FSlateApplication::Get().GetDragDroppingContent();
	if (!(DragOperation.IsValid() && DragOperation->IsOfType<FBrushBuilderDragDropOp>()))
	{
		// Hide the builder brush when not in geometry mode
		ViewFamily.EngineShowFlags.BuilderBrush = 0;
	}

	// Update the listener.
	FAudioDevice* const AudioDevice = GEditor ? GEditor->GetAudioDevice() : NULL;
	if( AudioDevice && bHasAudioFocus )
	{
		UpdateAudioListener( View );
	}
}

void FLevelEditorViewportClient::DrawCanvas( FViewport& InViewport, FSceneView& View, FCanvas& Canvas )
{
	// HUD for components visualizers
	if (GUnrealEd != NULL)
	{
		GUnrealEd->DrawComponentVisualizersHUD(&InViewport, &View, &Canvas);
	}

	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	// Testbed
	FCanvasItemTestbed TestBed;
	TestBed.Draw( Viewport, &Canvas );

	DrawStaticLightingDebugInfo(&View, &Canvas);
}

/**
 *	Draw the texture streaming bounds.
 */
void FLevelEditorViewportClient::DrawTextureStreamingBounds(const FSceneView* View,FPrimitiveDrawInterface* PDI)
{
	// Iterate each level
	for (TObjectIterator<ULevel> It; It; ++It)
	{
		ULevel* Level = *It;
		// Grab the streaming bounds entries for the level
		UTexture2D* TargetTexture = NULL;
		TArray<FStreamableTextureInstance>* STIA = Level->GetStreamableTextureInstances(TargetTexture);
		if (STIA)
		{
			for (int32 Index = 0; Index < STIA->Num(); Index++)
			{
				FStreamableTextureInstance& STI = (*STIA)[Index];
#if defined(_STREAMING_BOUNDS_DRAW_BOX_)
				FVector InMin = STI.BoundingSphere.Center;
				FVector InMax = STI.BoundingSphere.Center;
				float Max = STI.BoundingSphere.W;
				InMin -= FVector(Max);
				InMax += FVector(Max);
				FBox Box = FBox(InMin, InMax);
				DrawWireBox(PDI, Box, FColorList::Yellow, SDPG_World);
#else	//#if defined(_STREAMING_BOUNDS_DRAW_BOX_)
				// Draw bounding spheres
				FVector Origin = STI.BoundingSphere.Center;
				float Radius = STI.BoundingSphere.W;
				DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 1, 0), FColorList::Yellow, Radius, 32, SDPG_World);
				DrawCircle(PDI, Origin, FVector(1, 0, 0), FVector(0, 0, 1), FColorList::Yellow, Radius, 32, SDPG_World);
				DrawCircle(PDI, Origin, FVector(0, 1, 0), FVector(0, 0, 1), FColorList::Yellow, Radius, 32, SDPG_World);
#endif	//#if defined(_STREAMING_BOUNDS_DRAW_BOX_)
			}
		}
	}
}


/** Serialization. */
void FLevelEditorViewportClient::AddReferencedObjects( FReferenceCollector& Collector )
{
	FEditorViewportClient::AddReferencedObjects( Collector );

	for( TSet<FViewportHoverTarget>::TIterator It( FLevelEditorViewportClient::HoveredObjects ); It; ++It )
	{
		FViewportHoverTarget& CurHoverTarget = *It;
		Collector.AddReferencedObject( CurHoverTarget.HoveredActor );
		Collector.AddReferencedObject( CurHoverTarget.HoveredModel );
	}

	{
		FSceneViewStateInterface* Ref = ViewState.GetReference();

		if(Ref)
		{
			Ref->AddReferencedObjects(Collector);
		}
	}
}

/**
 * Copies layout and camera settings from the specified viewport
 *
 * @param InViewport The viewport to copy settings from
 */
void FLevelEditorViewportClient::CopyLayoutFromViewport( const FLevelEditorViewportClient& InViewport )
{
	SetViewLocation( InViewport.GetViewLocation() );
	SetViewRotation( InViewport.GetViewRotation() );
	ViewFOV = InViewport.ViewFOV;
	ViewportType = InViewport.ViewportType;
	SetOrthoZoom( InViewport.GetOrthoZoom() );
	ActorLockedToCamera = InViewport.ActorLockedToCamera;
	bAllowMatineePreview = InViewport.bAllowMatineePreview;
}


UWorld* FLevelEditorViewportClient::ConditionalSetWorld()
{
	// Should set GWorld to the play world if we are simulating in the editor and not already in the play world (reentrant calls to this would cause the world to be the same)
	if( bIsSimulateInEditorViewport && GEditor->PlayWorld != GWorld )
	{
		check( GEditor->PlayWorld != NULL );
		return SetPlayInEditorWorld( GEditor->PlayWorld );
	}

	// Returned world doesn't matter for this case
	return NULL;
}

void FLevelEditorViewportClient::ConditionalRestoreWorld( UWorld* InWorld )
{
	if( bIsSimulateInEditorViewport && InWorld )
	{
		// We should not already be in the world about to switch to an we should not be switching to the play world
		check( GWorld != InWorld && InWorld != GEditor->PlayWorld );
		RestoreEditorWorld( InWorld );
	}
}


/** Updates any orthographic viewport movement to use the same location as this viewport */
void FLevelEditorViewportClient::UpdateLinkedOrthoViewports( bool bInvalidate )
{
	// Only update if linked ortho movement is on, this viewport is orthographic, and is the current viewport being used.
	if (GetDefault<ULevelEditorViewportSettings>()->bUseLinkedOrthographicViewports && IsOrtho() && GCurrentLevelEditingViewportClient == this)
	{
		int32 MaxFrames = -1;
		int32 NextViewportIndexToDraw = INDEX_NONE;

		// Search through all viewports for orthographic ones
		for( int32 ViewportIndex = 0; ViewportIndex < GEditor->LevelViewportClients.Num(); ++ViewportIndex )
		{
			FLevelEditorViewportClient* Client = GEditor->LevelViewportClients[ViewportIndex];
			// Only update other orthographic viewports viewing the same scene
			if( (Client != this) && Client->IsOrtho() && (Client->GetScene() == this->GetScene()) )
			{
				int32 Frames = Client->FramesSinceLastDraw;
				Client->bNeedsLinkedRedraw = false;
				Client->SetOrthoZoom( GetOrthoZoom() );
				Client->SetViewLocation( GetViewLocation() );
				if( Client->IsVisible() )
				{
					// Find the viewport which has the most number of frames since it was last rendered.  We will render that next.
					if( Frames > MaxFrames )
					{
						MaxFrames = Frames;
						NextViewportIndexToDraw = ViewportIndex;
					}
					if( bInvalidate )
					{
						Client->Invalidate();
					}
				}
			}
		}

		if( bInvalidate )
		{
			Invalidate();
		}

		if( NextViewportIndexToDraw != INDEX_NONE )
		{
			// Force this viewport to redraw.
			GEditor->LevelViewportClients[NextViewportIndexToDraw]->bNeedsLinkedRedraw = true;
		}
	}
}


//
//	FLevelEditorViewportClient::GetScene
//

FLinearColor FLevelEditorViewportClient::GetBackgroundColor() const
{
	return IsPerspective() ? GEditor->C_WireBackground : GEditor->C_OrthoBackground;
}

int32 FLevelEditorViewportClient::GetCameraSpeedSetting() const
{
	return GetDefault<ULevelEditorViewportSettings>()->CameraSpeed;
}

void FLevelEditorViewportClient::SetCameraSpeedSetting(int32 SpeedSetting)
{
	GetMutableDefault<ULevelEditorViewportSettings>()->CameraSpeed = SpeedSetting;
}


bool FLevelEditorViewportClient::OverrideHighResScreenshotCaptureRegion(FIntRect& OutCaptureRegion)
{
	FSlateRect Rect;
	if (CalculateEditorConstrainedViewRect(Rect, Viewport))
	{
		FSlateRect InnerRect = Rect.InsetBy(FMargin(0.5f * SafePadding * Rect.GetSize().Size()));
		OutCaptureRegion = FIntRect((int32)InnerRect.Left, (int32)InnerRect.Top, (int32)(InnerRect.Left + InnerRect.GetSize().X), (int32)(InnerRect.Top + InnerRect.GetSize().Y));
		return true;
	}
	return false;
}

/**
 * Static: Adds a hover effect to the specified object
 *
 * @param	InHoverTarget	The hoverable object to add the effect to
 */
void FLevelEditorViewportClient::AddHoverEffect( FViewportHoverTarget& InHoverTarget )
{
	AActor* ActorUnderCursor = InHoverTarget.HoveredActor;
	UModel* ModelUnderCursor = InHoverTarget.HoveredModel;

	if( ActorUnderCursor != NULL )
	{
		TInlineComponentArray<UPrimitiveComponent*> Components;
		ActorUnderCursor->GetComponents(Components);

		for(int32 ComponentIndex = 0;ComponentIndex < Components.Num();ComponentIndex++)
		{
			UPrimitiveComponent* PrimitiveComponent = Components[ComponentIndex];
			if (PrimitiveComponent->IsRegistered())
			{
				PrimitiveComponent->PushHoveredToProxy( true );
			}
		}
	}
	else if( ModelUnderCursor != NULL )
	{
		check( InHoverTarget.ModelSurfaceIndex != INDEX_NONE );
		check( InHoverTarget.ModelSurfaceIndex < (uint32)ModelUnderCursor->Surfs.Num() );
		FBspSurf& Surf = ModelUnderCursor->Surfs[ InHoverTarget.ModelSurfaceIndex ];
		Surf.PolyFlags |= PF_Hovered;
	}
}


/**
 * Static: Removes a hover effect to the specified object
 *
 * @param	InHoverTarget	The hoverable object to remove the effect from
 */
void FLevelEditorViewportClient::RemoveHoverEffect( FViewportHoverTarget& InHoverTarget )
{
	AActor* CurHoveredActor = InHoverTarget.HoveredActor;
	if( CurHoveredActor != NULL )
	{
		TInlineComponentArray<UPrimitiveComponent*> Components;
		CurHoveredActor->GetComponents(Components);

		for(int32 ComponentIndex = 0;ComponentIndex < Components.Num();ComponentIndex++)
		{
			UPrimitiveComponent* PrimitiveComponent = Components[ComponentIndex];
			if (PrimitiveComponent->IsRegistered())
			{
				check(PrimitiveComponent->IsRegistered());
				PrimitiveComponent->PushHoveredToProxy( false );
			}
		}
	}

	UModel* CurHoveredModel = InHoverTarget.HoveredModel;
	if( CurHoveredModel != NULL )
	{
		if( InHoverTarget.ModelSurfaceIndex != INDEX_NONE &&
			(uint32)CurHoveredModel->Surfs.Num() >= InHoverTarget.ModelSurfaceIndex )
		{
			FBspSurf& Surf = CurHoveredModel->Surfs[ InHoverTarget.ModelSurfaceIndex ];
			Surf.PolyFlags &= ~PF_Hovered;
		}
	}
}


/**
 * Static: Clears viewport hover effects from any objects that currently have that
 */
void FLevelEditorViewportClient::ClearHoverFromObjects()
{
	// Clear hover feedback for any actor's that were previously drawing a hover cue
	if( HoveredObjects.Num() > 0 )
	{
		for( TSet<FViewportHoverTarget>::TIterator It( HoveredObjects ); It; ++It )
		{
			FViewportHoverTarget& CurHoverTarget = *It;
			RemoveHoverEffect( CurHoverTarget );
		}

		HoveredObjects.Empty();
	}
}

void FLevelEditorViewportClient::OnEditorCleanse()
{
	ClearHoverFromObjects();
}

bool FLevelEditorViewportClient::GetSpriteCategoryVisibility( const FName& InSpriteCategory ) const
{
	const int32 CategoryIndex = GEngine->GetSpriteCategoryIndex( InSpriteCategory );
	check( CategoryIndex != INDEX_NONE && CategoryIndex < SpriteCategoryVisibility.Num() );

	return SpriteCategoryVisibility[ CategoryIndex ];
}

bool FLevelEditorViewportClient::GetSpriteCategoryVisibility( int32 Index ) const
{
	check( Index >= 0 && Index < SpriteCategoryVisibility.Num() );
	return SpriteCategoryVisibility[ Index ];
}

void FLevelEditorViewportClient::SetSpriteCategoryVisibility( const FName& InSpriteCategory, bool bVisible )
{
	const int32 CategoryIndex = GEngine->GetSpriteCategoryIndex( InSpriteCategory );
	check( CategoryIndex != INDEX_NONE && CategoryIndex < SpriteCategoryVisibility.Num() );

	SpriteCategoryVisibility[ CategoryIndex ] = bVisible;
}

void FLevelEditorViewportClient::SetSpriteCategoryVisibility( int32 Index, bool bVisible )
{
	check( Index >= 0 && Index < SpriteCategoryVisibility.Num() );
	SpriteCategoryVisibility[ Index ] = bVisible;
}

void FLevelEditorViewportClient::SetAllSpriteCategoryVisibility( bool bVisible )
{
	SpriteCategoryVisibility.Init( bVisible, SpriteCategoryVisibility.Num() );
}

UWorld* FLevelEditorViewportClient::GetWorld() const
{
	if (bIsSimulateInEditorViewport)
	{
		// TODO: Find a proper way to get this
		return GWorld;
	}
	else if (World)
	{
		return World;
	}
	return FEditorViewportClient::GetWorld();
}

void FLevelEditorViewportClient::SetReferenceToWorldContext(FWorldContext& WorldContext)
{
	WorldContext.AddRef(World);
}

void FLevelEditorViewportClient::RemoveReferenceToWorldContext(FWorldContext& WorldContext)
{
	WorldContext.RemoveRef(World);
}

void FLevelEditorViewportClient::SetIsSimulateInEditorViewport( bool bInIsSimulateInEditorViewport )
{ 
	bIsSimulateInEditorViewport = bInIsSimulateInEditorViewport; 

	if (bInIsSimulateInEditorViewport)
	{
		TSharedRef<FPhysicsManipulationEdModeFactory> Factory = MakeShareable( new FPhysicsManipulationEdModeFactory );
		FEditorModeRegistry::Get().RegisterMode( FBuiltinEditorModes::EM_Physics, Factory );
	}
	else
	{
		FEditorModeRegistry::Get().UnregisterMode( FBuiltinEditorModes::EM_Physics );
	}
}

#undef LOCTEXT_NAMESPACE

// Doxygen cannot parse these correctly since the declarations are made in Editor, not UnrealEd
#if !UE_BUILD_DOCS
IMPLEMENT_HIT_PROXY(HGeomPolyProxy,HHitProxy);
IMPLEMENT_HIT_PROXY(HGeomEdgeProxy,HHitProxy);
IMPLEMENT_HIT_PROXY(HGeomVertexProxy,HHitProxy);
#endif
