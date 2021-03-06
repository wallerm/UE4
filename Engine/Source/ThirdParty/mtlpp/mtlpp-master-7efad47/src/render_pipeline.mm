/*
 * Copyright 2016-2017 Nikolay Aleksiev. All rights reserved.
 * License: https://github.com/naleksiev/mtlpp/blob/master/LICENSE
 */
// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
// Modifications for Unreal Engine

#include "render_pipeline.hpp"
#include "vertex_descriptor.hpp"
#include <Metal/MTLRenderPipeline.h>

namespace mtlpp
{
    RenderPipelineColorAttachmentDescriptor::RenderPipelineColorAttachmentDescriptor() :
        ns::Object<MTLRenderPipelineColorAttachmentDescriptor*>([[MTLRenderPipelineColorAttachmentDescriptor alloc] init], false)
    {
    }

    PixelFormat RenderPipelineColorAttachmentDescriptor::GetPixelFormat() const
    {
        Validate();
        return PixelFormat([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr pixelFormat]);
    }

    bool RenderPipelineColorAttachmentDescriptor::IsBlendingEnabled() const
    {
        Validate();
        return [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr isBlendingEnabled];
    }

    BlendFactor RenderPipelineColorAttachmentDescriptor::GetSourceRgbBlendFactor() const
    {
        Validate();
        return BlendFactor([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr sourceRGBBlendFactor]);
    }

    BlendFactor RenderPipelineColorAttachmentDescriptor::GetDestinationRgbBlendFactor() const
    {
        Validate();
        return BlendFactor([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr destinationRGBBlendFactor]);
    }

    BlendOperation RenderPipelineColorAttachmentDescriptor::GetRgbBlendOperation() const
    {
        Validate();
        return BlendOperation([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr rgbBlendOperation]);
    }

    BlendFactor RenderPipelineColorAttachmentDescriptor::GetSourceAlphaBlendFactor() const
    {
        Validate();
        return BlendFactor([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr sourceAlphaBlendFactor]);
    }

    BlendFactor RenderPipelineColorAttachmentDescriptor::GetDestinationAlphaBlendFactor() const
    {
        Validate();
        return BlendFactor([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr destinationAlphaBlendFactor]);
    }

    BlendOperation RenderPipelineColorAttachmentDescriptor::GetAlphaBlendOperation() const
    {
        Validate();
        return BlendOperation([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr alphaBlendOperation]);
    }

    ColorWriteMask RenderPipelineColorAttachmentDescriptor::GetWriteMask() const
    {
        Validate();
        return ColorWriteMask([(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr writeMask]);
    }

    void RenderPipelineColorAttachmentDescriptor::SetPixelFormat(PixelFormat pixelFormat)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setPixelFormat:MTLPixelFormat(pixelFormat)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetBlendingEnabled(bool blendingEnabled)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setBlendingEnabled:blendingEnabled];
    }

    void RenderPipelineColorAttachmentDescriptor::SetSourceRgbBlendFactor(BlendFactor sourceRgbBlendFactor)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setSourceRGBBlendFactor:MTLBlendFactor(sourceRgbBlendFactor)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetDestinationRgbBlendFactor(BlendFactor destinationRgbBlendFactor)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setDestinationRGBBlendFactor:MTLBlendFactor(destinationRgbBlendFactor)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetRgbBlendOperation(BlendOperation rgbBlendOperation)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setRgbBlendOperation:MTLBlendOperation(rgbBlendOperation)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetSourceAlphaBlendFactor(BlendFactor sourceAlphaBlendFactor)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setSourceAlphaBlendFactor:MTLBlendFactor(sourceAlphaBlendFactor)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetDestinationAlphaBlendFactor(BlendFactor destinationAlphaBlendFactor)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setDestinationAlphaBlendFactor:MTLBlendFactor(destinationAlphaBlendFactor)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetAlphaBlendOperation(BlendOperation alphaBlendOperation)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setAlphaBlendOperation:MTLBlendOperation(alphaBlendOperation)];
    }

    void RenderPipelineColorAttachmentDescriptor::SetWriteMask(ColorWriteMask writeMask)
    {
        Validate();
        [(MTLRenderPipelineColorAttachmentDescriptor*)m_ptr setWriteMask:MTLColorWriteMask(writeMask)];
    }

    RenderPipelineReflection::RenderPipelineReflection() :
        ns::Object<MTLRenderPipelineReflection*>([[MTLRenderPipelineReflection alloc] init], false)
    {
    }

    const ns::Array<Argument> RenderPipelineReflection::GetVertexArguments() const
    {
        Validate();
        return [(MTLRenderPipelineReflection*)m_ptr vertexArguments];
    }

    const ns::Array<Argument> RenderPipelineReflection::GetFragmentArguments() const
    {
        Validate();
        return [(MTLRenderPipelineReflection*)m_ptr fragmentArguments];
    }
	
	const ns::Array<Argument> RenderPipelineReflection::GetTileArguments() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [(MTLRenderPipelineReflection*)m_ptr tileArguments];
#else
		return ns::Array<Argument>();
#endif
	}

    RenderPipelineDescriptor::RenderPipelineDescriptor() :
        ns::Object<MTLRenderPipelineDescriptor*>([[MTLRenderPipelineDescriptor alloc] init], false)
    {
    }

    ns::String RenderPipelineDescriptor::GetLabel() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr label];
    }

    Function RenderPipelineDescriptor::GetVertexFunction() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr vertexFunction];
    }

    Function RenderPipelineDescriptor::GetFragmentFunction() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr fragmentFunction];
    }

    VertexDescriptor RenderPipelineDescriptor::GetVertexDescriptor() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr vertexDescriptor];
    }

    uint32_t RenderPipelineDescriptor::GetSampleCount() const
    {
        Validate();
        return uint32_t([(MTLRenderPipelineDescriptor*)m_ptr sampleCount]);
    }

    bool RenderPipelineDescriptor::IsAlphaToCoverageEnabled() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr isAlphaToCoverageEnabled];
    }

    bool RenderPipelineDescriptor::IsAlphaToOneEnabled() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr isAlphaToOneEnabled];
    }

    bool RenderPipelineDescriptor::IsRasterizationEnabled() const
    {
        Validate();
        return [(MTLRenderPipelineDescriptor*)m_ptr isRasterizationEnabled];
    }

    ns::Array<RenderPipelineColorAttachmentDescriptor> RenderPipelineDescriptor::GetColorAttachments() const
    {
        Validate();
        return (NSArray*)[(MTLRenderPipelineDescriptor*)m_ptr colorAttachments];
    }

    PixelFormat RenderPipelineDescriptor::GetDepthAttachmentPixelFormat() const
    {
        Validate();
        return PixelFormat([(MTLRenderPipelineDescriptor*)m_ptr depthAttachmentPixelFormat]);
    }

    PixelFormat RenderPipelineDescriptor::GetStencilAttachmentPixelFormat() const
    {
        Validate();
        return PixelFormat([(MTLRenderPipelineDescriptor*)m_ptr stencilAttachmentPixelFormat]);
    }

    PrimitiveTopologyClass RenderPipelineDescriptor::GetInputPrimitiveTopology() const
    {
        Validate();
#if MTLPP_PLATFORM_MAC
        return PrimitiveTopologyClass([(MTLRenderPipelineDescriptor*)m_ptr inputPrimitiveTopology]);
#else
        return PrimitiveTopologyClass(0);
#endif
    }

    TessellationPartitionMode RenderPipelineDescriptor::GetTessellationPartitionMode() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return TessellationPartitionMode([(MTLRenderPipelineDescriptor*)m_ptr tessellationPartitionMode]);
#else
        return TessellationPartitionMode(0);
#endif
    }

    uint32_t RenderPipelineDescriptor::GetMaxTessellationFactor() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return uint32_t([(MTLRenderPipelineDescriptor*)m_ptr maxTessellationFactor]);
#else
        return 0;
#endif
    }

    bool RenderPipelineDescriptor::IsTessellationFactorScaleEnabled() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return [(MTLRenderPipelineDescriptor*)m_ptr isTessellationFactorScaleEnabled];
#else
        return false;
#endif
    }

    TessellationFactorFormat RenderPipelineDescriptor::GetTessellationFactorFormat() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return TessellationFactorFormat([(MTLRenderPipelineDescriptor*)m_ptr tessellationFactorFormat]);
#else
        return TessellationFactorFormat(0);
#endif
    }

    TessellationControlPointIndexType RenderPipelineDescriptor::GetTessellationControlPointIndexType() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return TessellationControlPointIndexType([(MTLRenderPipelineDescriptor*)m_ptr tessellationControlPointIndexType]);
#else
        return TessellationControlPointIndexType(0);
#endif
    }

    TessellationFactorStepFunction RenderPipelineDescriptor::GetTessellationFactorStepFunction() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return TessellationFactorStepFunction([(MTLRenderPipelineDescriptor*)m_ptr tessellationFactorStepFunction]);
#else
        return TessellationFactorStepFunction(0);
#endif
    }

    Winding RenderPipelineDescriptor::GetTessellationOutputWindingOrder() const
    {
        Validate();
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        return Winding([(MTLRenderPipelineDescriptor*)m_ptr tessellationOutputWindingOrder]);
#else
        return Winding(0);
#endif
    }
	
	ns::Array<PipelineBufferDescriptor> RenderPipelineDescriptor::GetVertexBuffers() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE(10_13, 11_0)
		return (NSArray*)[(MTLRenderPipelineDescriptor*)m_ptr vertexBuffers];
#else
		return ns::Array<PipelineBufferDescriptor>();
#endif
	}
	
	ns::Array<PipelineBufferDescriptor> RenderPipelineDescriptor::GetFragmentBuffers() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE(10_13, 11_0)
		return (NSArray*)[(MTLRenderPipelineDescriptor*)m_ptr fragmentBuffers];
#else
		return ns::Array<PipelineBufferDescriptor>();
#endif
	}

    void RenderPipelineDescriptor::SetLabel(const ns::String& label)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setLabel:(NSString*)label.GetPtr()];
    }

    void RenderPipelineDescriptor::SetVertexFunction(const Function& vertexFunction)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setVertexFunction:(id<MTLFunction>)vertexFunction.GetPtr()];
    }

    void RenderPipelineDescriptor::SetFragmentFunction(const Function& fragmentFunction)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setFragmentFunction:(id<MTLFunction>)fragmentFunction.GetPtr()];
    }

    void RenderPipelineDescriptor::SetVertexDescriptor(const VertexDescriptor& vertexDescriptor)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setVertexDescriptor:(MTLVertexDescriptor*)vertexDescriptor.GetPtr()];
    }

    void RenderPipelineDescriptor::SetSampleCount(uint32_t sampleCount)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setSampleCount:sampleCount];
    }

    void RenderPipelineDescriptor::SetAlphaToCoverageEnabled(bool alphaToCoverageEnabled)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setAlphaToCoverageEnabled:alphaToCoverageEnabled];
    }

    void RenderPipelineDescriptor::SetAlphaToOneEnabled(bool alphaToOneEnabled)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setAlphaToOneEnabled:alphaToOneEnabled];
    }

    void RenderPipelineDescriptor::SetRasterizationEnabled(bool rasterizationEnabled)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setRasterizationEnabled:rasterizationEnabled];
    }

    void RenderPipelineDescriptor::SetDepthAttachmentPixelFormat(PixelFormat depthAttachmentPixelFormat)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setDepthAttachmentPixelFormat:MTLPixelFormat(depthAttachmentPixelFormat)];
    }

    void RenderPipelineDescriptor::SetStencilAttachmentPixelFormat(PixelFormat depthAttachmentPixelFormat)
    {
        Validate();
        [(MTLRenderPipelineDescriptor*)m_ptr setStencilAttachmentPixelFormat:MTLPixelFormat(depthAttachmentPixelFormat)];
    }

    void RenderPipelineDescriptor::SetInputPrimitiveTopology(PrimitiveTopologyClass inputPrimitiveTopology)
    {
        Validate();
#if MTLPP_PLATFORM_MAC
        [(MTLRenderPipelineDescriptor*)m_ptr setInputPrimitiveTopology:MTLPrimitiveTopologyClass(inputPrimitiveTopology)];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationPartitionMode(TessellationPartitionMode tessellationPartitionMode)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationPartitionMode:MTLTessellationPartitionMode(tessellationPartitionMode)];
#endif
    }

    void RenderPipelineDescriptor::SetMaxTessellationFactor(uint32_t maxTessellationFactor)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setMaxTessellationFactor:maxTessellationFactor];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationFactorScaleEnabled(bool tessellationFactorScaleEnabled)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationFactorScaleEnabled:tessellationFactorScaleEnabled];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationFactorFormat(TessellationFactorFormat tessellationFactorFormat)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationFactorFormat:MTLTessellationFactorFormat(tessellationFactorFormat)];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationControlPointIndexType(TessellationControlPointIndexType tessellationControlPointIndexType)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationControlPointIndexType:MTLTessellationControlPointIndexType(tessellationControlPointIndexType)];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationFactorStepFunction(TessellationFactorStepFunction tessellationFactorStepFunction)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationFactorStepFunction:MTLTessellationFactorStepFunction(tessellationFactorStepFunction)];
#endif
    }

    void RenderPipelineDescriptor::SetTessellationOutputWindingOrder(Winding tessellationOutputWindingOrder)
    {
#if MTLPP_IS_AVAILABLE(10_12, 10_0)
        [(MTLRenderPipelineDescriptor*)m_ptr setTessellationOutputWindingOrder:MTLWinding(tessellationOutputWindingOrder)];
#endif
    }
	
	void RenderPipelineDescriptor::SetVertexBuffers(ns::Array<PipelineBufferDescriptor> const& array)
	{
#if MTLPP_IS_AVAILABLE(10_13, 11_0)
		[(MTLRenderPipelineDescriptor*)m_ptr setVertexBuffers:(MTLPipelineBufferDescriptorArray*)array.GetPtr()];
#endif
	}
	
	void RenderPipelineDescriptor::SetFragmentBuffers(ns::Array<PipelineBufferDescriptor> const& array)
	{
#if MTLPP_IS_AVAILABLE(10_13, 11_0)
		[(MTLRenderPipelineDescriptor*)m_ptr setFragmentBuffers:(MTLPipelineBufferDescriptorArray*)array.GetPtr()];
#endif
	}

    void RenderPipelineDescriptor::Reset()
    {
        [(MTLRenderPipelineDescriptor*)m_ptr reset];
    }

    ns::String RenderPipelineState::GetLabel() const
    {
        Validate();
        return [(id<MTLRenderPipelineState>)m_ptr label];
    }

    Device RenderPipelineState::GetDevice() const
    {
        Validate();
        return [(id<MTLRenderPipelineState>)m_ptr device];
    }
	
	uint32_t RenderPipelineState::GetMaxTotalThreadsPerThreadgroup() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr maxTotalThreadsPerThreadgroup];
#else
		return 0;
#endif
	}
	
	bool RenderPipelineState::GetThreadgroupSizeMatchesTileSize() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr threadgroupSizeMatchesTileSize];
#else
		return false;
#endif
	}
	
	uint32_t RenderPipelineState::GetImageblockSampleLength() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr imageblockSampleLength];
#else
		return 0;
#endif
	}
	
	uint32_t RenderPipelineState::GetImageblockMemoryLengthForDimensions(Size const& imageblockDimensions) const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr imageblockMemoryLengthForDimensions:MTLSizeMake(imageblockDimensions.Width, imageblockDimensions.Height, imageblockDimensions.Depth)];
#else
	return 0;
#endif
	}
	
	PixelFormat TileRenderPipelineColorAttachmentDescriptor::GetPixelFormat() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return (PixelFormat)[m_ptr pixelFormat];
#else
		return (PixelFormat)0;
#endif
	}
	
	void TileRenderPipelineColorAttachmentDescriptor::SetPixelFormat(PixelFormat pixelFormat)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setPixelFormat:(MTLPixelFormat)pixelFormat];
#endif
	}
	
	ns::String TileRenderPipelineDescriptor::GetLabel() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr label];
#else
		return ns::String();
#endif
	}
	
	Function TileRenderPipelineDescriptor::GetTileFunction() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr tileFunction];
#else
		return Function();
#endif
	}
	
	uint32_t TileRenderPipelineDescriptor::GetRasterSampleCount() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr rasterSampleCount];
#else
		return 0;
#endif
	}
	
	ns::Array<TileRenderPipelineColorAttachmentDescriptor> TileRenderPipelineDescriptor::GetColorAttachments() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return (NSArray*)[m_ptr colorAttachments];
#else
		return ns::Array<TileRenderPipelineColorAttachmentDescriptor>();
#endif
	}
	
	bool TileRenderPipelineDescriptor::GetThreadgroupSizeMatchesTileSize() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return [m_ptr threadgroupSizeMatchesTileSize];
#else
		return false;
#endif
	}
	
	ns::Array<PipelineBufferDescriptor> TileRenderPipelineDescriptor::GetTileBuffers() const
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		return (NSArray*)[m_ptr tileBuffers];
#else
		return ns::Array<PipelineBufferDescriptor>();
#endif
	}
	
	void TileRenderPipelineDescriptor::SetLabel(const ns::String& label)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setLabel:label.GetPtr()];
#endif
	}
	
	void TileRenderPipelineDescriptor::SetTileFunction(const Function& tileFunction)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setTileFunction:tileFunction.GetPtr()];
#endif
	}
	
	void TileRenderPipelineDescriptor::SetRasterSampleCount(uint32_t sampleCount)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setRasterSampleCount:sampleCount];
#endif
	}
	
	void TileRenderPipelineDescriptor::SetThreadgroupSizeMatchesTileSize(bool threadgroupSizeMatchesTileSize)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setThreadgroupSizeMatchesTileSize:threadgroupSizeMatchesTileSize];
#endif
	}
	
	void TileRenderPipelineDescriptor::SetTileBuffers(ns::Array<PipelineBufferDescriptor> const& array)
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr setTileBuffers:(MTLPipelineBufferDescriptorArray*)array.GetPtr()];
#endif
	}
	
	void TileRenderPipelineDescriptor::Reset()
	{
		Validate();
#if MTLPP_IS_AVAILABLE_IOS(11_0)
		[m_ptr reset];
#endif
	}
}
