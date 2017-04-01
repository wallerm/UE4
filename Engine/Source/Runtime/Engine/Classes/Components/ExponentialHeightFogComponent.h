// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "ExponentialHeightFogComponent.generated.h"

/**
 *	Used to create fogging effects such as clouds but with a density that is related to the height of the fog.
 */
UCLASS(ClassGroup=Rendering, collapsecategories, hidecategories=(Object, Mobility), editinlinenew, meta=(BlueprintSpawnableComponent))
class ENGINE_API UExponentialHeightFogComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	/** Global density factor. */
	UPROPERTY(BlueprintReadOnly, interp, Category=ExponentialHeightFogComponent, meta=(UIMin = "0", UIMax = ".05"))
	float FogDensity;

	UPROPERTY(BlueprintReadOnly, interp, Category=ExponentialHeightFogComponent)
	FLinearColor FogInscatteringColor;

	/** 
	 * Cubemap that can be specified for fog color, which is useful to make distant, heavily fogged scene elements match the sky.
	 * When the cubemap is specified, FogInscatteringColor is ignored and Directional inscattering is disabled. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=InscatteringTexture)
	class UTextureCube* InscatteringColorCubemap;

	/** Angle to rotate the InscatteringColorCubemap around the Z axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=InscatteringTexture, meta=(UIMin = "0", UIMax = "360"))
	float InscatteringColorCubemapAngle;

	/** Tint color used when InscatteringColorCubemap is specified, for quick edits without having to reimport InscatteringColorCubemap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=InscatteringTexture)
	FLinearColor InscatteringTextureTint;

	/** Distance at which InscatteringColorCubemap should be used directly for the Inscattering Color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=InscatteringTexture, meta=(UIMin = "1000", UIMax = "1000000"))
	float FullyDirectionalInscatteringColorDistance;

	/** Distance at which only the average color of InscatteringColorCubemap should be used as Inscattering Color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=InscatteringTexture, meta=(UIMin = "1000", UIMax = "1000000"))
	float NonDirectionalInscatteringColorDistance;

	/** 
	 * Controls the size of the directional inscattering cone, which is used to approximate inscattering from a directional light.  
	 * Note: there must be a directional light with bUsedAsAtmosphereSunLight enabled for DirectionalInscattering to be used.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category=DirectionalInscattering, meta=(UIMin = "2", UIMax = "64"))
	float DirectionalInscatteringExponent;

	/** 
	 * Controls the start distance from the viewer of the directional inscattering, which is used to approximate inscattering from a directional light. 
	 * Note: there must be a directional light with bUsedAsAtmosphereSunLight enabled for DirectionalInscattering to be used.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category=DirectionalInscattering)
	float DirectionalInscatteringStartDistance;

	/** 
	 * Controls the color of the directional inscattering, which is used to approximate inscattering from a directional light. 
	 * Note: there must be a directional light with bUsedAsAtmosphereSunLight enabled for DirectionalInscattering to be used.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category=DirectionalInscattering)
	FLinearColor DirectionalInscatteringColor;

	/** 
	 * Height density factor, controls how the density increases as height decreases.  
	 * Smaller values make the visible transition larger.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category=ExponentialHeightFogComponent, meta=(UIMin = "0.001", UIMax = "2"))
	float FogHeightFalloff;

	/** 
	 * Maximum opacity of the fog.  
	 * A value of 1 means the fog can become fully opaque at a distance and replace scene color completely,
	 * A value of 0 means the fog color will not be factored in at all.
	 */
	UPROPERTY(BlueprintReadOnly, interp, Category=ExponentialHeightFogComponent, meta=(UIMin = "0", UIMax = "1"))
	float FogMaxOpacity;

	/** Distance from the camera that the fog will start, in world units. */
	UPROPERTY(BlueprintReadOnly, interp, Category=ExponentialHeightFogComponent, meta=(UIMin = "0", UIMax = "5000"))
	float StartDistance;

	/** Scene elements past this distance will not have fog applied.  This is useful for excluding skyboxes which already have fog baked in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=ExponentialHeightFogComponent, meta=(UIMin = "100000", UIMax = "20000000"))
	float FogCutoffDistance;

	/** Whether to enable volumetric fog. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Volumetric Fog"))
	bool bEnableVolumetricFog;

	/** Positive values cause more scattering in the direction of the light, negative values cause more scattering in the opposite direction of the light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Scattering Distribution", UIMin = "-.9", UIMax = ".9"))
	float VolumetricFogScatteringDistribution;

	/** Scales the global scattering amount used by volumetric fog.  Values larger than 1 cause fog particles everywhere reflect more light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Scattering Scale", UIMin = ".1", UIMax = "10"))
	float VolumetricFogScatteringScale;

	/** Scales the global absorption amount used by volumetric fog.  Values larger than 1 cause fog particles everywhere absorb more light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Absorption Scale", UIMin = ".1", UIMax = "10"))
	float VolumetricFogAbsorptionScale;

	/** 
	 * Distance over which volumetric fog should cover.  Larger values extend the effect into the distance but reduce the details that can be captured.
	 * This should typically be tweaked along with the directional light 'Dynamic Shadow Distance' so that volumetric fog will receive shadows everywhere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog, meta=(DisplayName = "Distance", UIMin = "1000", UIMax = "10000"))
	float VolumetricFogDistance;

	/** 
	 * Whether to use FogInscatteringColor for the Sky Light volumetric scattering color and DirectionalInscatteringColor for the Directional Light scattering color. 
	 * Enabling this allows Volumetric fog to match Height fog in the distance, but produces non-physical volumetric lighting that may not match surface lighting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = VolumetricFog)
	bool bOverrideLightColorsWithFogInscatteringColors;

public:
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFogDensity(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFogInscatteringColor(FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetInscatteringColorCubemap(UTextureCube* Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetInscatteringColorCubemapAngle(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFullyDirectionalInscatteringColorDistance(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetNonDirectionalInscatteringColorDistance(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetInscatteringTextureTint(FLinearColor Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetDirectionalInscatteringExponent(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetDirectionalInscatteringStartDistance(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetDirectionalInscatteringColor(FLinearColor Value);
	
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFogHeightFalloff(float Value);
	
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFogMaxOpacity(float Value);
	
	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetStartDistance(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|Components|ExponentialHeightFog")
	void SetFogCutoffDistance(float Value);

	UFUNCTION(BlueprintCallable, Category="Rendering|VolumetricFog")
	void SetVolumetricFog(bool bNewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|VolumetricFog")
	void SetVolumetricFogScatteringDistribution(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|VolumetricFog")
	void SetVolumetricFogAbsorptionScale(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|VolumetricFog")
	void SetVolumetricFogScatteringScale(float NewValue);

	UFUNCTION(BlueprintCallable, Category="Rendering|VolumetricFog")
	void SetVolumetricFogDistance(float NewValue);

protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface.

	void AddFogIfNeeded();

public:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual bool CanEditChange(const UProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void PostInterpChange(UProperty* PropertyThatChanged) override;
	//~ End UObject Interface
};


