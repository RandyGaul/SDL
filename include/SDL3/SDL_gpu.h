﻿/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/**
 * \file SDL_gpu.h
 *
 * Include file for SDL GPU API functions
*/

#ifndef SDL_GPU_H
#define SDL_GPU_H

#include <SDL3/SDL_stdinc.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Type Declarations */

typedef struct SDL_GpuDevice SDL_GpuDevice;
typedef struct SDL_GpuBuffer SDL_GpuBuffer;
typedef struct SDL_GpuBufferCycle SDL_GpuBufferCycle;
typedef struct SDL_GpuTransferBuffer SDL_GpuTransferBuffer;
typedef struct SDL_GpuTransferBufferCycle SDL_GpuTransferBufferCycle;
typedef struct SDL_GpuUniformBuffer SDL_GpuUniformBuffer;
typedef struct SDL_GpuTexture SDL_GpuTexture;
typedef struct SDL_GpuTextureCycle SDL_GpuTextureCycle;
typedef struct SDL_GpuSampler SDL_GpuSampler;
typedef struct SDL_GpuShader SDL_GpuShader;
typedef struct SDL_GpuComputePipeline SDL_GpuComputePipeline;
typedef struct SDL_GpuGraphicsPipeline SDL_GpuGraphicsPipeline;
typedef struct SDL_GpuResourceSet SDL_GpuResourceSet;
typedef struct SDL_GpuCommandBuffer SDL_GpuCommandBuffer;
typedef struct SDL_GpuRenderPass SDL_GpuRenderPass;
typedef struct SDL_GpuComputePass SDL_GpuComputePass;
typedef struct SDL_GpuCopyPass SDL_GpuCopyPass;
typedef struct SDL_GpuFence SDL_GpuFence;
typedef struct SDL_GpuOcclusionQuery SDL_GpuOcclusionQuery;

typedef enum SDL_GpuPresentMode
{
	SDL_GPU_PRESENTMODE_IMMEDIATE,
	SDL_GPU_PRESENTMODE_MAILBOX,
	SDL_GPU_PRESENTMODE_FIFO,
	SDL_GPU_PRESENTMODE_FIFO_RELAXED
} SDL_GpuPresentMode;

typedef enum SDL_GpuPrimitiveType
{
	SDL_GPU_PRIMITIVETYPE_POINTLIST,
	SDL_GPU_PRIMITIVETYPE_LINELIST,
	SDL_GPU_PRIMITIVETYPE_LINESTRIP,
	SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
	SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP
} SDL_GpuPrimitiveType;

typedef enum SDL_GpuLoadOp
{
	SDL_GPU_LOADOP_LOAD,
	SDL_GPU_LOADOP_CLEAR,
	SDL_GPU_LOADOP_DONT_CARE
} SDL_GpuLoadOp;

typedef enum SDL_GpuStoreOp
{
	SDL_GPU_STOREOP_STORE,
	SDL_GPU_STOREOP_DONT_CARE
} SDL_GpuStoreOp;

typedef enum SDL_GpuIndexElementSize
{
	SDL_GPU_INDEXELEMENTSIZE_16BIT,
	SDL_GPU_INDEXELEMENTSIZE_32BIT
} SDL_GpuIndexElementSize;

typedef enum SDL_GpuTextureFormat
{
	/* Unsigned Normalized Float Color Formats */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8,
	SDL_GPU_TEXTUREFORMAT_B8G8R8A8,
	SDL_GPU_TEXTUREFORMAT_R5G6B5,
	SDL_GPU_TEXTUREFORMAT_A1R5G5B5,
	SDL_GPU_TEXTUREFORMAT_B4G4R4A4,
	SDL_GPU_TEXTUREFORMAT_A2R10G10B10,
	SDL_GPU_TEXTUREFORMAT_R16G16,
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16,
	SDL_GPU_TEXTUREFORMAT_R8,
	SDL_GPU_TEXTUREFORMAT_A8,
	/* Compressed Unsigned Normalized Float Color Formats */
	SDL_GPU_TEXTUREFORMAT_BC1,
	SDL_GPU_TEXTUREFORMAT_BC2,
	SDL_GPU_TEXTUREFORMAT_BC3,
	SDL_GPU_TEXTUREFORMAT_BC7,
	/* Signed Normalized Float Color Formats  */
	SDL_GPU_TEXTUREFORMAT_R8G8_SNORM,
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM,
	/* Signed Float Color Formats */
	SDL_GPU_TEXTUREFORMAT_R16_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_R16G16_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_R32_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_R32G32_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_R32G32B32A32_SFLOAT,
	/* Unsigned Integer Color Formats */
	SDL_GPU_TEXTUREFORMAT_R8_UINT,
	SDL_GPU_TEXTUREFORMAT_R8G8_UINT,
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT,
	SDL_GPU_TEXTUREFORMAT_R16_UINT,
	SDL_GPU_TEXTUREFORMAT_R16G16_UINT,
	SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
	/* SRGB Color Formats */
	SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SRGB,
	SDL_GPU_TEXTUREFORMAT_B8G8R8A8_SRGB,
	/* Compressed SRGB Color Formats */
	SDL_GPU_TEXTUREFORMAT_BC3_SRGB,
	SDL_GPU_TEXTUREFORMAT_BC7_SRGB,
	/* Depth Formats */
	SDL_GPU_TEXTUREFORMAT_D16_UNORM,
	SDL_GPU_TEXTUREFORMAT_D24_UNORM,
	SDL_GPU_TEXTUREFORMAT_D32_SFLOAT,
	SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
	SDL_GPU_TEXTUREFORMAT_D32_SFLOAT_S8_UINT
} SDL_GpuTextureFormat;

typedef enum SDL_GpuTextureUsageFlagBits
{
	SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT                = 0x00000001,
	SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT           = 0x00000002,
	SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT   = 0x00000004,
	SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT  = 0x00000008,
	SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_WRITE_BIT = 0x00000010,
	SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT   = 0x00000020,
	SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT  = 0x00000040
} SDL_GpuTextureUsageFlagBits;

typedef Uint32 SDL_GpuTextureUsageFlags;

typedef enum SDL_GpuColorSpace
{
    SDL_GPU_COLORSPACE_NONLINEAR_SRGB,
    SDL_GPU_COLORSPACE_LINEAR_SRGB,
    SDL_GPU_COLORSPACE_HDR10_ST2048
} SDL_GpuColorSpace;

typedef enum SDL_GpuTextureType
{
    SDL_GPU_TEXTURETYPE_2D,
    SDL_GPU_TEXTURETYPE_3D,
    SDL_GPU_TEXTURETYPE_CUBE,
} SDL_GpuTextureType;

typedef enum SDL_GpuSampleCount
{
	SDL_GPU_SAMPLECOUNT_1,
	SDL_GPU_SAMPLECOUNT_2,
	SDL_GPU_SAMPLECOUNT_4,
	SDL_GPU_SAMPLECOUNT_8
} SDL_GpuSampleCount;

typedef enum SDL_GpuCubeMapFace
{
	SDL_GPU_CUBEMAPFACE_POSITIVEX,
	SDL_GPU_CUBEMAPFACE_NEGATIVEX,
	SDL_GPU_CUBEMAPFACE_POSITIVEY,
	SDL_GPU_CUBEMAPFACE_NEGATIVEY,
	SDL_GPU_CUBEMAPFACE_POSITIVEZ,
	SDL_GPU_CUBEMAPFACE_NEGATIVEZ
} SDL_GpuCubeMapFace;

typedef enum SDL_GpuBufferUsageFlagBits
{
	SDL_GPU_BUFFERUSAGE_VERTEX_BIT 	 = 0x00000001,
	SDL_GPU_BUFFERUSAGE_INDEX_BIT  	 = 0x00000002,
	SDL_GPU_BUFFERUSAGE_INDIRECT_BIT = 0x00000004,
	SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT  = 0x00000008,
	SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_WRITE_BIT = 0x00000010,
	SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ_BIT   = 0x00000020,
	SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT  = 0x00000040
} SDL_GpuBufferUsageFlagBits;

typedef Uint32 SDL_GpuBufferUsageFlags;

typedef enum SDL_GpuTransferBufferMapFlagBits
{
    SDL_GPU_TRANSFER_MAP_READ  = 0x00000001,
    SDL_GPU_TRANSFER_MAP_WRITE = 0x00000002
} SDL_GpuTransferBufferMapFlagBits;

typedef Uint32 SDL_GpuTransferBufferMapFlags;

typedef enum SDL_GpuShaderResourceType
{
	SDL_GPU_RESOURCETYPE_TEXTURE_SAMPLER,
	SDL_GPU_RESOURCETYPE_STORAGE_BUFFER_READONLY,
	SDL_GPU_RESOURCETYPE_STORAGE_BUFFER_READWRITE,
	SDL_GPU_RESOURCETYPE_STORAGE_TEXTURE_READONLY,
	SDL_GPU_RESOURCETYPE_STORAGE_TEXTURE_READWRITE,
	SDL_GPU_RESOURCETYPE_UNIFORM_BUFFER
} SDL_GpuShaderResourceType;

typedef enum SDL_GpuShaderStageFlagBits
{
	SDL_GPU_SHADERSTAGE_VERTEX    = 0x00000001,
	SDL_GPU_SHADERSTAGE_FRAGMENT  = 0x00000002,
	SDL_GPU_SHADERSTAGE_COMPUTE   = 0x00000004
} SDL_GpuShaderStageFlagBits;

typedef Uint32 SDL_GpuShaderStageFlags;

typedef enum SDL_GpuShaderFormat
{
	SDL_GPU_SHADERFORMAT_INVALID,
	SDL_GPU_SHADERFORMAT_SPIRV,	/* Vulkan */
	SDL_GPU_SHADERFORMAT_DXBC,	/* D3D11, D3D12 */
	SDL_GPU_SHADERFORMAT_DXIL,	/* D3D12 */
	SDL_GPU_SHADERFORMAT_MSL,	/* Metal */
	SDL_GPU_SHADERFORMAT_METALLIB,	/* Metal */
	SDL_GPU_SHADERFORMAT_SECRET	/* NDA'd platforms */
} SDL_GpuShaderFormat;

typedef enum SDL_GpuVertexElementFormat
{
	SDL_GPU_VERTEXELEMENTFORMAT_UINT,
	SDL_GPU_VERTEXELEMENTFORMAT_FLOAT,
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR2,
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR3,
	SDL_GPU_VERTEXELEMENTFORMAT_VECTOR4,
	SDL_GPU_VERTEXELEMENTFORMAT_COLOR,
	SDL_GPU_VERTEXELEMENTFORMAT_BYTE4,
	SDL_GPU_VERTEXELEMENTFORMAT_SHORT2,
	SDL_GPU_VERTEXELEMENTFORMAT_SHORT4,
	SDL_GPU_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2,
	SDL_GPU_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4,
	SDL_GPU_VERTEXELEMENTFORMAT_HALFVECTOR2,
	SDL_GPU_VERTEXELEMENTFORMAT_HALFVECTOR4
} SDL_GpuVertexElementFormat;

typedef enum SDL_GpuVertexInputRate
{
	SDL_GPU_VERTEXINPUTRATE_VERTEX = 0,
	SDL_GPU_VERTEXINPUTRATE_INSTANCE = 1
} SDL_GpuVertexInputRate;

typedef enum SDL_GpuFillMode
{
	SDL_GPU_FILLMODE_FILL,
	SDL_GPU_FILLMODE_LINE
} SDL_GpuFillMode;

typedef enum SDL_GpuCullMode
{
	SDL_GPU_CULLMODE_NONE,
	SDL_GPU_CULLMODE_FRONT,
	SDL_GPU_CULLMODE_BACK
} SDL_GpuCullMode;

typedef enum SDL_GpuFrontFace
{
	SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
	SDL_GPU_FRONTFACE_CLOCKWISE
} SDL_GpuFrontFace;

typedef enum SDL_GpuCompareOp
{
	SDL_GPU_COMPAREOP_NEVER,
	SDL_GPU_COMPAREOP_LESS,
	SDL_GPU_COMPAREOP_EQUAL,
	SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
	SDL_GPU_COMPAREOP_GREATER,
	SDL_GPU_COMPAREOP_NOT_EQUAL,
	SDL_GPU_COMPAREOP_GREATER_OR_EQUAL,
	SDL_GPU_COMPAREOP_ALWAYS
} SDL_GpuCompareOp;

typedef enum SDL_GpuStencilOp
{
	SDL_GPU_STENCILOP_KEEP,
	SDL_GPU_STENCILOP_ZERO,
	SDL_GPU_STENCILOP_REPLACE,
	SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP,
	SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP,
	SDL_GPU_STENCILOP_INVERT,
	SDL_GPU_STENCILOP_INCREMENT_AND_WRAP,
	SDL_GPU_STENCILOP_DECREMENT_AND_WRAP
} SDL_GpuStencilOp;

typedef enum SDL_GpuBlendOp
{
	SDL_GPU_BLENDOP_ADD,
	SDL_GPU_BLENDOP_SUBTRACT,
	SDL_GPU_BLENDOP_REVERSE_SUBTRACT,
	SDL_GPU_BLENDOP_MIN,
	SDL_GPU_BLENDOP_MAX
} SDL_GpuBlendOp;

typedef enum SDL_GpuBlendFactor
{
	SDL_GPU_BLENDFACTOR_ZERO,
	SDL_GPU_BLENDFACTOR_ONE,
	SDL_GPU_BLENDFACTOR_SRC_COLOR,
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
	SDL_GPU_BLENDFACTOR_DST_COLOR,
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR,
	SDL_GPU_BLENDFACTOR_SRC_ALPHA,
	SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
	SDL_GPU_BLENDFACTOR_DST_ALPHA,
	SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
	SDL_GPU_BLENDFACTOR_CONSTANT_COLOR,
	SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR,
	SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE
} SDL_GpuBlendFactor;

typedef enum SDL_GpuColorComponentFlagBits
{
	SDL_GPU_COLORCOMPONENT_R_BIT = 0x00000001,
	SDL_GPU_COLORCOMPONENT_G_BIT = 0x00000002,
	SDL_GPU_COLORCOMPONENT_B_BIT = 0x00000004,
	SDL_GPU_COLORCOMPONENT_A_BIT = 0x00000008
} SDL_GpuColorComponentFlagBits;

typedef Uint32 SDL_GpuColorComponentFlags;

typedef enum SDL_GpuFilter
{
	SDL_GPU_FILTER_NEAREST,
	SDL_GPU_FILTER_LINEAR
} SDL_GpuFilter;

typedef enum SDL_GpuSamplerMipmapMode
{
	SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
	SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
} SDL_GpuSamplerMipmapMode;

typedef enum SDL_GpuSamplerAddressMode
{
	SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
	SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT,
	SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
	SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_BORDER
} SDL_GpuSamplerAddressMode;

/* FIXME: we should probably make a library-level decision about color types */
typedef enum SDL_GpuBorderColor
{
	SDL_GPU_BORDERCOLOR_FLOAT_TRANSPARENT_BLACK,
	SDL_GPU_BORDERCOLOR_INT_TRANSPARENT_BLACK,
	SDL_GPU_BORDERCOLOR_FLOAT_OPAQUE_BLACK,
	SDL_GPU_BORDERCOLOR_INT_OPAQUE_BLACK,
	SDL_GPU_BORDERCOLOR_FLOAT_OPAQUE_WHITE,
	SDL_GPU_BORDERCOLOR_INT_OPAQUE_WHITE
} SDL_GpuBorderColor;

typedef enum SDL_GpuTransferUsage
{
	SDL_GPU_TRANSFERUSAGE_BUFFER,
	SDL_GPU_TRANSFERUSAGE_TEXTURE
} SDL_GpuTransferUsage;

typedef enum SDL_ResourceSetType
{
    SDL_GPU_RESOURCESETTYPE_BUFFER,
    SDL_GPU_RESOURCESETTYPE_SAMPLED_TEXTURE,
    SDL_GPU_RESOURCESETTYPE_STORAGE_TEXTURE
} SDL_ResourceSetType;

typedef enum SDL_GpuBackendBits
{
	SDL_GPU_BACKEND_INVALID = 0,
	SDL_GPU_BACKEND_VULKAN  = 0x0000000000000001,
	SDL_GPU_BACKEND_D3D11   = 0x0000000000000002,
	SDL_GPU_BACKEND_METAL   = 0x0000000000000004,
	SDL_GPU_BACKEND_ALL = (SDL_GPU_BACKEND_VULKAN | SDL_GPU_BACKEND_D3D11 | SDL_GPU_BACKEND_METAL)
} SDL_GpuBackendBits;

typedef Uint64 SDL_GpuBackend;

/* Structures */

typedef struct SDL_GpuDepthStencilValue
{
	float depth;
	Uint32 stencil;
} SDL_GpuDepthStencilValue;

typedef struct SDL_GpuRect
{
	Sint32 x;
	Sint32 y;
	Sint32 w;
	Sint32 h;
} SDL_GpuRect;

typedef struct SDL_GpuColor
{
	float r;
	float g;
	float b;
	float a;
} SDL_GpuColor;

typedef struct SDL_GpuViewport
{
	float x;
	float y;
	float w;
	float h;
	float minDepth;
	float maxDepth;
} SDL_GpuViewport;

typedef struct SDL_GpuTextureSlice
{
	SDL_GpuTexture *texture;
	Uint32 mipLevel;
	Uint32 layer;
} SDL_GpuTextureSlice;

typedef struct SDL_GpuTextureRegion
{
	SDL_GpuTextureSlice textureSlice;
	Uint32 x;
	Uint32 y;
	Uint32 z;
	Uint32 w;
	Uint32 h;
	Uint32 d;
} SDL_GpuTextureRegion;

typedef struct SDL_GpuBufferImageCopy
{
	Uint32 bufferOffset;
	Uint32 bufferStride; /* number of pixels from one row to the next */
	Uint32 bufferImageHeight; /* number of rows from one layer/depth-slice to the next */
} SDL_GpuBufferImageCopy;

typedef struct SDL_GpuBufferCopy
{
	Uint32 srcOffset;
	Uint32 dstOffset;
	Uint32 size;
} SDL_GpuBufferCopy;

typedef struct SDL_GpuIndirectDrawCommand
{
	Uint32 vertexCount;
	Uint32 instanceCount;
	Uint32 firstVertex;
	Uint32 firstInstance;
} SDL_GpuIndirectDrawCommand;

/* State structures */

typedef struct SDL_GpuSamplerStateCreateInfo
{
	SDL_GpuFilter minFilter;
	SDL_GpuFilter magFilter;
	SDL_GpuSamplerMipmapMode mipmapMode;
	SDL_GpuSamplerAddressMode addressModeU;
	SDL_GpuSamplerAddressMode addressModeV;
	SDL_GpuSamplerAddressMode addressModeW;
	float mipLodBias;
	Uint8 anisotropyEnable;
	float maxAnisotropy;
	Uint8 compareEnable;
	SDL_GpuCompareOp compareOp;
	float minLod;
	float maxLod;
	SDL_GpuBorderColor borderColor;
} SDL_GpuSamplerStateCreateInfo;

typedef struct SDL_GpuVertexBinding
{
	Uint32 binding;
	Uint32 stride;
	SDL_GpuVertexInputRate inputRate;
    Uint32 stepRate;
} SDL_GpuVertexBinding;

typedef struct SDL_GpuVertexAttribute
{
	Uint32 location;
	Uint32 binding;
	SDL_GpuVertexElementFormat format;
	Uint32 offset;
} SDL_GpuVertexAttribute;

typedef struct SDL_GpuVertexInputState
{
	const SDL_GpuVertexBinding *vertexBindings;
	Uint32 vertexBindingCount;
	const SDL_GpuVertexAttribute *vertexAttributes;
	Uint32 vertexAttributeCount;
} SDL_GpuVertexInputState;

typedef struct SDL_GpuStencilOpState
{
	SDL_GpuStencilOp failOp;
	SDL_GpuStencilOp passOp;
	SDL_GpuStencilOp depthFailOp;
	SDL_GpuCompareOp compareOp;
} SDL_GpuStencilOpState;

typedef struct SDL_GpuColorAttachmentBlendState
{
	Uint8 blendEnable;
	SDL_GpuBlendFactor srcColorBlendFactor;
	SDL_GpuBlendFactor dstColorBlendFactor;
	SDL_GpuBlendOp colorBlendOp;
	SDL_GpuBlendFactor srcAlphaBlendFactor;
	SDL_GpuBlendFactor dstAlphaBlendFactor;
	SDL_GpuBlendOp alphaBlendOp;
	SDL_GpuColorComponentFlags colorWriteMask;
} SDL_GpuColorAttachmentBlendState;

typedef struct SDL_GpuShaderCreateInfo
{
	size_t codeSize;
	const Uint8 *code;
	const char* entryPointName;
	SDL_GpuShaderStageFlagBits stage;
	SDL_GpuShaderFormat format;
} SDL_GpuShaderCreateInfo;

typedef struct SDL_GpuTextureCreateInfo
{
	Uint32 width;
	Uint32 height;
	Uint32 depth;
	Uint8 isCube;
	Uint32 layerCount;
	Uint32 levelCount;
	SDL_GpuSampleCount sampleCount;
	SDL_GpuTextureFormat format;
	SDL_GpuTextureUsageFlags usageFlags;
} SDL_GpuTextureCreateInfo;

/* Pipeline state structures */

typedef struct SDL_GpuRasterizerState
{
	SDL_GpuFillMode fillMode;
	SDL_GpuCullMode cullMode;
	SDL_GpuFrontFace frontFace;
	Uint8 depthBiasEnable;
	float depthBiasConstantFactor;
	float depthBiasClamp;
	float depthBiasSlopeFactor;
} SDL_GpuRasterizerState;

typedef struct SDL_GpuMultisampleState
{
	SDL_GpuSampleCount multisampleCount;
	Uint32 sampleMask;
} SDL_GpuMultisampleState;

typedef struct SDL_GpuDepthStencilState
{
	Uint8 depthTestEnable;
	Uint8 depthWriteEnable;
	SDL_GpuCompareOp compareOp;
	Uint8 depthBoundsTestEnable;
	Uint8 stencilTestEnable;
	SDL_GpuStencilOpState backStencilState;
	SDL_GpuStencilOpState frontStencilState;
	Uint32 compareMask;
	Uint32 writeMask;
	Uint32 reference;
	float minDepthBounds;
	float maxDepthBounds;
} SDL_GpuDepthStencilState;

typedef struct SDL_GpuColorAttachmentDescription
{
	SDL_GpuTextureFormat format;
	SDL_GpuColorAttachmentBlendState blendState;
} SDL_GpuColorAttachmentDescription;

typedef struct SDL_GpuGraphicsPipelineAttachmentInfo
{
	SDL_GpuColorAttachmentDescription *colorAttachmentDescriptions;
	Uint32 colorAttachmentCount;
	Uint8 hasDepthStencilAttachment;
	SDL_GpuTextureFormat depthStencilFormat;
} SDL_GpuGraphicsPipelineAttachmentInfo;

/* A single description of a resource that will be bound by a shader. */
typedef struct SDL_GpuShaderResourceDescription
{
	SDL_GpuShaderResourceType resourceType;
	SDL_GpuShaderStageFlagBits shaderStage;
    SDL_bool isResourceSet;
} SDL_GpuShaderResourceDescription;

/* Describes the resources that are used by a pipeline. */
typedef struct SDL_GpuShaderResourceLayoutInfo
{
	SDL_GpuShaderResourceDescription *resourceDescriptions;
	Uint32 resourceDescriptionCount;
} SDL_GpuShaderResourceLayoutInfo;

typedef struct SDL_GpuGraphicsPipelineCreateInfo
{
	SDL_GpuShader *vertexShader;
	SDL_GpuShader *fragmentShader;
	SDL_GpuVertexInputState vertexInputState;
	SDL_GpuPrimitiveType primitiveType;
	SDL_GpuRasterizerState rasterizerState;
	SDL_GpuMultisampleState multisampleState;
	SDL_GpuDepthStencilState depthStencilState;
	SDL_GpuGraphicsPipelineAttachmentInfo attachmentInfo;
	SDL_GpuShaderResourceLayoutInfo pipelineResourceLayoutInfo;
	float blendConstants[4];
} SDL_GpuGraphicsPipelineCreateInfo;

typedef struct SDL_GpuComputePipelineCreateInfo
{
	SDL_GpuShader *computeShader;
	SDL_GpuShaderResourceLayoutInfo pipelineResourceLayoutInfo;
} SDL_GpuComputePipelineCreateInfo;

typedef struct SDL_GpuColorAttachmentInfo
{
    /* The texture slice that will be used as a color attachment by a render pass. */
	SDL_GpuTextureSlice textureSlice;

    /* Can be ignored by RenderPass if CLEAR is not used */
	SDL_GpuColor clearColor;

    /* Determines what is done with the texture slice at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the data currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single color.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
	SDL_GpuLoadOp loadOp;

    /* Determines what is done with the texture slice at the end of the render pass.
    *
    *   STORE:
    *     Stores the results of the render pass in the texture slice.
    *
    *   DONT_CARE:
    *     The driver will do whatever it wants with the texture slice memory.
    *     This is often a good option for depth/stencil textures.
    */
	SDL_GpuStoreOp storeOp;
} SDL_GpuColorAttachmentInfo;

typedef struct SDL_GpuDepthStencilAttachmentInfo
{
    /* The texture slice that will be used as the depth stencil attachment by a render pass. */
	SDL_GpuTextureSlice textureSlice;

    /* Can be ignored by the render pass if CLEAR is not used */
	SDL_GpuDepthStencilValue depthStencilClearValue;

    /* Determines what is done with the depth values at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the depth values currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single depth.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
	SDL_GpuLoadOp loadOp;

    /* Determines what is done with the depth values at the end of the render pass.
     *
     *   STORE:
     *     Stores the depth results in the texture slice.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is often a good option for depth/stencil textures.
     */
	SDL_GpuStoreOp storeOp;

    /* Determines what is done with the stencil values at the beginning of the render pass.
     *
     *   LOAD:
     *     Loads the stencil values currently in the texture slice.
     *
     *   CLEAR:
     *     Clears the texture slice to a single stencil value.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the memory.
     *     This is a good option if you know that every single pixel will be touched in the render pass.
     */
	SDL_GpuLoadOp stencilLoadOp;

    /* Determines what is done with the stencil values at the end of the render pass.
     *
     *   STORE:
     *     Stores the stencil results in the texture slice.
     *
     *   DONT_CARE:
     *     The driver will do whatever it wants with the texture slice memory.
     *     This is often a good option for depth/stencil textures.
     */
	SDL_GpuStoreOp stencilStoreOp;
} SDL_GpuDepthStencilAttachmentInfo;

/* Binding structs */

typedef struct SDL_GpuBufferBinding
{
	SDL_GpuBuffer *gpuBuffer;
	Uint32 offset;
} SDL_GpuBufferBinding;

typedef struct SDL_GpuTextureSamplerBinding
{
	SDL_GpuTexture *texture;
	SDL_GpuSampler *sampler;
} SDL_GpuTextureSamplerBinding;

typedef struct SDL_GpuStorageBufferBinding
{
	SDL_GpuBuffer *gpuBuffer;
} SDL_GpuStorageBufferBinding;

typedef struct SDL_GpuStorageTextureBinding
{
	SDL_GpuTextureSlice textureSlice;
} SDL_GpuStorageTextureBinding;

typedef struct SDL_GpuUniformBufferBinding
{
	SDL_GpuUniformBuffer *uniformBuffer;
	Uint32 uniformDataSizeInBytes;
} SDL_GpuUniformBufferBinding;

typedef struct SDL_GpuResourceSetBinding
{
    SDL_GpuResourceSet *resourceSet;
} SDL_GpuResourceSetBinding;

typedef struct SDL_GpuShaderResourceBinding
{
	SDL_GpuShaderResourceType resourceType;

	union {
		SDL_GpuTextureSamplerBinding textureSampler;
		SDL_GpuBuffer *storageBufferReadOnly; /* FIXME: should this contain an offset/range? */
		SDL_GpuStorageBufferBinding storageBufferReadWrite;
		SDL_GpuTextureSlice storageTextureReadOnly;
		SDL_GpuStorageTextureBinding storageTextureReadWrite;
		SDL_GpuUniformBufferBinding uniformBuffer;
        SDL_GpuResourceSetBinding resourceSet;
	} resource;
} SDL_GpuShaderResourceBinding;

/* Functions */

/* Device */

/**
 * Creates a GPU context.
 *
 * Backends will first be checked for availability in order of bitflags passed using preferredBackends. If none of the backends are available, the remaining backends are checked as fallback renderers.
 *
 * Think of "preferred" backends as those that have pre-built shaders readily available - for example, you would set the SDL_GPU_BACKEND_VULKAN bit if your game includes SPIR-V shaders. If you generate shaders at runtime (i.e. via SDL_shader) and the library does _not_ provide you with a preferredBackends value, you should pass SDL_GPU_BACKEND_ALL so that updated versions of SDL can be aware of which backends the application was aware of at compile time. SDL_GPU_BACKEND_INVALID is an accepted value but is not recommended.
 *
 * \param preferredBackends a bitflag containing the renderers most recognized by the application
 * \param debugMode enable debug mode properties and validations
 * \returns a GPU context on success or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSelectBackend
 * \sa SDL_GpuDestroyDevice
 */
extern DECLSPEC SDL_GpuDevice *SDLCALL SDL_GpuCreateDevice(
	SDL_GpuBackend preferredBackends,
	SDL_bool debugMode
);

/**
 * Destroys a GPU context previously returned by SDL_GpuCreateDevice.
 *
 * \param device a GPU Context to destroy
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuCreateDevice
 */
extern DECLSPEC void SDLCALL SDL_GpuDestroyDevice(SDL_GpuDevice *device);

/**
 * Returns the backend used to create this GPU context.
 *
 * \param device a GPU context to query
 * \returns an SDL_GpuBackend value, or SDL_GPU_BACKEND_INVALID on error
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSelectBackend
 */
extern DECLSPEC SDL_GpuBackend SDLCALL SDL_GpuGetBackend(SDL_GpuDevice *device);

/* State Creation */

/**
 * Creates a pipeline object to be used in a compute workflow.
 *
 * \param device a GPU Context
 * \param computePipelineCreateInfo a struct describing the state of the requested compute pipeline
 * \returns a compute pipeline object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuCreateShader
 * \sa SDL_GpuBindComputePipeline
 * \sa SDL_GpuQueueDestroyComputePipeline
 */
extern DECLSPEC SDL_GpuComputePipeline *SDLCALL SDL_GpuCreateComputePipeline(
	SDL_GpuDevice *device,
	SDL_GpuComputePipelineCreateInfo *computePipelineCreateInfo
);

/**
 * Creates a pipeline object to be used in a graphics workflow.
 *
 * \param device a GPU Context
 * \param pipelineCreateInfo a struct describing the state of the desired graphics pipeline
 * \returns a graphics pipeline object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuCreateShader
 * \sa SDL_GpuBindGraphicsPipeline
 * \sa SDL_GpuQueueDestroyGraphicsPipeline
 */
extern DECLSPEC SDL_GpuGraphicsPipeline *SDLCALL SDL_GpuCreateGraphicsPipeline(
	SDL_GpuDevice *device,
	SDL_GpuGraphicsPipelineCreateInfo *pipelineCreateInfo
);

/**
 * Creates a sampler object to be used when binding textures in a graphics workflow.
 *
 * \param device a GPU Context
 * \param samplerStateCreateInfo a struct describing the state of the desired sampler
 * \returns a sampler object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuBindVertexSamplers
 * \sa SDL_GpuBindFragmentSamplers
 * \sa SDL_QueueDestroySampler
 */
extern DECLSPEC SDL_GpuSampler *SDLCALL SDL_GpuCreateSampler(
	SDL_GpuDevice *device,
	SDL_GpuSamplerStateCreateInfo *samplerStateCreateInfo
);

/**
 * Creates a shader to be used when creating a graphics or compute pipeline.
 *
 * \param device a GPU Context
 * \param shaderCreateInfo a struct describing the state of the desired shader
 * \returns a shader object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuCreateGraphicsPipeline
 * \sa SDL_GpuCreateComputePipeline
 * \sa SDL_GpuQueueDestroyShader
 */
extern DECLSPEC SDL_GpuShader *SDLCALL SDL_GpuCreateShader(
	SDL_GpuDevice *device,
	SDL_GpuShaderCreateInfo *shaderCreateInfo
);

/**
 * Creates a texture object to be used in graphics or compute workflows.
 * The contents of this texture are undefined until data is written to the texture.
 *
 * \param device a GPU Context
 * \param textureCreateInfo a struct describing the state of the texture to create
 * \returns a texture object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuUploadToTexture
 * \sa SDL_GpuDownloadFromTexture
 * \sa SDL_GpuBindVertexSamplers
 * \sa SDL_GpuBindFragmentSamplers
 * \sa SDL_GpuBlit
 * \sa SDL_GpuBindComputeTextures
 * \sa SDL_GpuQueueDestroyTexture
 */
extern DECLSPEC SDL_GpuTexture *SDLCALL SDL_GpuCreateTexture(
	SDL_GpuDevice *device,
	SDL_GpuTextureCreateInfo *textureCreateInfo
);

/**
 * Creates a texture cycle object which consists of an arbitrary number of textures.
 * The cycle can rotate to point to a new texture if the current texture buffer is bound.
 *
 * \param device a GPU Context
 * \param textureCreateInfo a struct describing the state of the texture to create
 * \returns a texture cycle object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuQueueDestroyTextureCycle
 */
extern DECLSPEC SDL_GpuTextureCycle *SDLCALL SDL_GpuCreateTextureCycle(
    SDL_GpuDevice *device,
    SDL_GpuTextureCreateInfo *textureCreateInfo
);

/**
 * Creates a buffer object to be used in graphics or compute workflows.
 * The contents of this buffer are undefined until data is written to the buffer.
 *
 * \param device a GPU Context
 * \param usageFlags bitflag mask hinting at how the buffer will be used
 * \param sizeInBytes the size of the buffer
 * \returns a buffer object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuUploadToBuffer
 * \sa SDL_GpuBindVertexBuffers
 * \sa SDL_GpuBindIndexBuffer
 * \sa SDL_GpuBindComputeStorageBuffers
 * \sa SDL_GpuQueueDestroyGpuBuffer
 */
extern DECLSPEC SDL_GpuBuffer *SDLCALL SDL_GpuCreateGpuBuffer(
	SDL_GpuDevice *device,
	SDL_GpuBufferUsageFlags usageFlags,
	Uint32 sizeInBytes
);

/**
 * Creates a buffer cycle object, which consists of an arbitrary number of GpuBuffers.
 * The cycle can rotate to point to a new buffer if the current active buffer is bound.
 *
 * \param device a GPU Context
 * \param usageFlags bitflag mask hinting at how the buffer will be used
 * \param sizeInBytes the size of the buffer
 * \returns a buffer cycle object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuQueueDestroyBufferCycle
 */
extern DECLSPEC SDL_GpuBufferCycle *SDLCALL SDL_GpuCreateBufferCycle(
    SDL_GpuDevice *device,
    SDL_GpuBufferUsageFlags usageFlags,
    Uint32 sizeInBytes
);

/**
 * Creates a transfer buffer to be used when uploading to or downloading from graphics resources.
 *
 * \param device a GPU Context
 * \param usage specifies whether the transfer buffer will transfer buffers or textures
 * \param mapFlags specify read-write options for the transfer buffer
 * \param sizeInBytes the size of the transfer buffer
 * \returns a transfer buffer on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuUploadToBuffer
 * \sa SDL_GpuDownloadFromBuffer
 * \sa SDL_GpuUploadToTexture
 * \sa SDL_GpuDownloadFromTexture
 * \sa SDL_GpuQueueDestroyTransferBuffer
 */
extern DECLSPEC SDL_GpuTransferBuffer *SDLCALL SDL_GpuCreateTransferBuffer(
	SDL_GpuDevice *device,
	SDL_GpuTransferUsage usage,
    SDL_GpuTransferBufferMapFlags mapFlags,
	Uint32 sizeInBytes
);

/**
 * Creates a buffer cycle object, which consists of an arbitrary number of transfer buffers.
 * The cycle can rotate to point to a new buffer if the current active transfer buffer is bound.
 *
 * \param device a GPU Context
 * \param usage specifies whether the transfer buffer will transfer buffers or textures
 * \param mapFlags specify read-write options for the transfer buffer
 * \param sizeInBytes the size of the transfer buffer
 * \returns a transfer buffer cycle on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuUploadToBuffer
 * \sa SDL_GpuDownloadFromBuffer
 * \sa SDL_GpuUploadToTexture
 * \sa SDL_GpuDownloadFromTexture
 * \sa SDL_GpuQueueDestroyTransferBuffer
 */
extern DECLSPEC SDL_GpuTransferBufferCycle *SDLCALL SDL_GpuCreateTransferBufferCycle(
    SDL_GpuDevice *device,
	SDL_GpuTransferUsage usage,
    SDL_GpuTransferBufferMapFlags mapFlags,
	Uint32 sizeInBytes
);

/**
 * Creates a uniform buffer object to be used in shader workflows.
 *
 * \param device a GPU context
 * \param sizeInBytes the size of the uniform buffer
 * \returns a uniform buffer object on success, or NULL on failure
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuBindGraphicsResourceSet
 * \sa SDL_GpuBindComputeResourceSet
 * \sa SDL_GpuPushGraphicsUniformData
 * \sa SDL_GpuPushComputeUniformData
 * \sa SDL_GpuQueueDestroyUniformBuffer
 */
extern DECLSPEC SDL_GpuUniformBuffer *SDLCALL SDL_GpuCreateUniformBuffer(
	SDL_GpuDevice *device,
	Uint32 sizeInBytes
);

/**
 * Creates a resource set to be used in bindless workflows.
 *
 * \param device a GPU context
 * \param resourceSetType the type of resource that will be in the set
 * \returns a resource set object
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuResourceSet *SDLCALL SDL_GpuCreateResourceSet(
    SDL_GpuDevice *device,
    SDL_ResourceSetType resourceSetType
);

/**
 * Creates an occlusion query object.
 *
 * This function is included for compatibility with old render systems.
 * If you are not supporting an old application, do NOT use this.
 *
 * \param device a GPU context
 * \returns an occlusion query object
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuQueueDestroyQuery
 */
extern DECLSPEC SDL_GpuOcclusionQuery *SDLCALL SDL_GpuCreateOcclusionQuery(
    SDL_GpuDevice *device
);

/* Debug Naming */

/**
 * Sets an arbitrary string constant to label a buffer. Useful for debugging.
 *
 * \param device a GPU Context
 * \param buffer a buffer to attach the name to
 * \param text a UTF-8 string constant to mark as the name of the buffer
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetGpuBufferName(
	SDL_GpuDevice *device,
	SDL_GpuBuffer *buffer,
	const char *text
);

/**
 * Sets an arbitrary string constant to label a texture. Useful for debugging.
 *
 * \param device a GPU Context
 * \param texture a texture to attach the name to
 * \param text a UTF-8 string constant to mark as the name of the texture
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetTextureName(
	SDL_GpuDevice *device,
	SDL_GpuTexture *texture,
	const char *text
);

/**
 * Sets an arbitrary string constant to label a section of a command buffer. Useful for debugging.
 *
 * \param commandBuffer a command buffer
 * \param text a UTF-8 string constant to mark as the label
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetStringMarker(
    SDL_GpuCommandBuffer *commandBuffer,
    const char *text
);

/* Disposal */

/**
 * Specifies that the given texture should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param texture a texture to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyTexture(
	SDL_GpuDevice *device,
	SDL_GpuTexture *texture
);

/**
 * Specifies that the given texture cycle should be destroyed.
 *
 * \param device a GPU context
 * \param textureCycle a texture cycle to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDestroyTextureCycle(
    SDL_GpuDevice *device,
    SDL_GpuTextureCycle *textureCycle
);

/**
 * Specifies that the given sampler should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param sampler a sampler to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroySampler(
	SDL_GpuDevice *device,
	SDL_GpuSampler *sampler
);

/**
 * Specifies that the given buffer should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param gpuBuffer a buffer to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyGpuBuffer(
	SDL_GpuDevice *device,
	SDL_GpuBuffer *gpuBuffer
);

/**
 * Specifies that the given buffer cycle should be destroyed.
 *
 * \param device a Gpu context
 * \param bufferCycle a buffer cycle to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDestroyBufferCycle(
    SDL_GpuDevice *device,
    SDL_GpuBufferCycle *bufferCycle
);

/**
 * Specifies that the given transfer buffer should be destroyed once it is no longer bound.
 *
 * \param device a GPU context
 * \param transferBuffer a transfer buffer to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyTransferBuffer(
	SDL_GpuDevice *device,
	SDL_GpuTransferBuffer *transferBuffer
);

/**
 * Specifies that the given transfer buffer cycle should be destroyed.
 *
 * \param device a GPU context
 * \param transferBufferCycle a transfer buffer cycle to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDestroyTransferBufferCycle(
    SDL_GpuDevice *device,
    SDL_GpuTransferBufferCycle *transferBufferCycle
);

/**
 * Specifies that the given uniform buffer should be destroyed once it is no longer bound.
 *
 * \param device a GPU context
 * \param uniformBuffer a uniform buffer to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyUniformBuffer(
	SDL_GpuDevice *device,
	SDL_GpuUniformBuffer *uniformBuffer
);

/**
 * Specifies that the given shader should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param shader a shader to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyShader(
	SDL_GpuDevice *device,
	SDL_GpuShader *shader
);

/**
 * Specifies that the given compute pipeline should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param computePipeline a compute pipeline to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyComputePipeline(
	SDL_GpuDevice *device,
	SDL_GpuComputePipeline *computePipeline
);

/**
 * Specifies that the given graphics pipeline should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param graphicsPipeline a graphics pipeline to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyGraphicsPipeline(
	SDL_GpuDevice *device,
	SDL_GpuGraphicsPipeline *graphicsPipeline
);

/**
 * Specifies that the given resource set should be destroyed.
 *
 * \param device a GPU context
 * \param resourceSet a resource set to destroy
 */
extern DECLSPEC void SDLCALL SDL_GpuDestroyResourceSet(
    SDL_GpuDevice *device,
    SDL_GpuResourceSet *resourceSet
);

/**
 * Specifies that the given occlusion query should be destroyed once it is no longer referenced.
 *
 * \param device a GPU context
 * \param query an occlusion query object to be destroyed
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuQueueDestroyOcclusionQuery(
    SDL_GpuDevice *device,
    SDL_GpuOcclusionQuery *query
);

/*
 * A NOTE ON CYCLING
 *
 * When using a command buffer, operations do not occur immediately -
 * they occur some time after the command buffer is submitted.
 *
 * When a resource is used in a pending or active command buffer, it is considered to be "bound".
 * When a resource is no longer used in any pending or active command buffers, it is considered to be "unbound".
 *
 * If data resources are bound, it is unspecified when that data will be unbound
 * unless you acquire a fence when submitting the command buffer and wait on it.
 * However, this doesn't mean you need to track resource usage manually.
 *
 * Buffers and textures can be created on their own, but they can also be created via a buffer or texture cycle.
 * These effectively function as ring buffers. Each cycle has an active resource that it points to.
 * When CycleBuffer or CycleTexture are called, if the current active resource is bound, the cycle rotates to a new active resource.
 * If none is available, a new resource is created within the cycle.
 * This means you don't have to worry about complex state tracking and synchronization as long as cycling is correctly employed.
 *
 * For example: say you call SetTransferData and then UploadToBuffer. The next time you call SetTransferData,
 * if you cycle first, you don't have to worry about overwriting any data that is not yet uploaded.
 *
 * Another example: If you are using a texture in a render pass every frame, this can cause a data dependency between frames.
 * If you cycle the texture before calling BeginRenderPass, you can prevent this data dependency.
 *
 * Cycling will never undefine already bound data.
 * When cycling, all data in the resource should be considered to be undefined for subsequent commands until that data is written again.
 * You must take care not to read undefined data.
 *
 * You must also take care not to overwrite a section of data that has been referenced in a command without cycling first.
 * It is OK to overwrite unreferenced data in a bound resource without cycling,
 * but overwriting a section of data that has already been referenced will produce unexpected results.
 */

/**
 * Cycles a buffer cycle, returning an unbound buffer in the cycle.
 * This buffer may be the current active buffer of the cycle.
 * May create a new buffer if all current buffers in the cycle are bound.
 *
 * \param bufferCycle A buffer cycle to cycle
 * \returns an unbound buffer in the cycle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuBuffer *SDLCALL SDL_CycleBuffer(
    SDL_GpuBufferCycle *bufferCycle
);

/**
 * Cycles a transfer buffer cycle, returning an unbound transfer buffer in the cycle.
 * This transfer buffer may be the current active transfer buffer of the cycle.
 * May create a new transfer buffer if all current transfer buffers in the cycle are bound.
 *
 * \param transferBufferCycle A transfer buffer cycle to cycle
 * \returns an unbound transfer buffer in the cycle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuTransferBuffer *SDLCALL SDL_CycleTransferBuffer(
    SDL_GpuTransferBufferCycle *transferBufferCycle
);

/**
 * Cycles a texture cycle, returning an unbound texture in the cycle.
 * This texture may be the current active texture of the cycle.
 * May create a new texture if all current textures in the cycle are bound.
 *
 * \param textureCycle a texture cycle to cycle
 * \returns an unbound texture in the cycle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuTexture *SDLCALL SDL_CycleTexture(
    SDL_GpuTextureCycle *textureCycle
);

/* Resource Sets */

/**
 * Adds a buffer to a resource set, returning the index of the buffer in the set.
 * The resource set must have been created with type SDL_GPU_RESOURCESETTYPE_BUFFER.
 *
 * \param resourceSet a resource set object
 * \param buffer a buffer
 * \return the index of the buffer in the resource set
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC Uint32 SDL_AddBufferToResourceSet(
    SDL_GpuResourceSet *resourceSet,
    SDL_GpuBuffer *buffer
);

/**
 * Adds a texture to resource set, returning the index of the texture in the set.
 * The resource set must have been created with type SDL_GPU_RESOURCESETTYPE_SAMPLED_TEXTURE.
 *
 * \param resourceSet a resource set object
 * \param texture a texture
 * \return the index of the texture in the resource set
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC Uint32 SDL_AddSampledTextureToResourceSet(
    SDL_GpuResourceSet *resourceSet,
    SDL_GpuTexture *texture,
    SDL_GpuSampler *sampler
);

/**
 * Adds a texture to resource set, returning the index of the texture in the set.
 * The resource set must have been created with type SDL_GPU_RESOURCESETTYPE_STORAGE_TEXTURE.
 *
 * \param resourceSet a resource set object
 * \param texture a texture
 * \return the index of the texture in the resource set
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC Uint32 SDL_AddStorageTextureToResourceSet(
    SDL_GpuResourceSet *resourceSet,
    SDL_GpuTexture *texture
);

/**
 * Removes the resource at the given index from the set.
 * It is recommended to call this before destroying the resource.
 * If you reference an index that contains a destroyed resource, bad things will happen!
 *
 * \param resourceSet
 * \param resourceSetIndex
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDL_RemoveFromResourceSet(
    SDL_GpuResourceSet *resourceSet,
    Uint32 resourceSetIndex
);

/* Graphics State */

/**
 * Begins a render pass on a command buffer.
 * A render pass consists of a set of textures, clear values, and load/store operations
 * which will be rendered to during the render pass.
 * All operations related to graphics pipelines must take place inside of a render pass.
 * A default viewport and scissor state are automatically set when this is called.
 * You cannot begin another render pass, or begin a compute pass or copy pass
 * until you have ended the render pass.
 *
 * \param commandBuffer a command buffer
 * \param colorAttachmentInfos an array of SDL_GpuColorAttachmentInfo structs
 * \param colorAttachmentCount the number of color attachments in the colorAttachmentInfos array
 * \param depthStencilAttachmentInfo the depth-stencil target and clear value, may be NULL
 * \returns a render pass handle
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuEndRenderPass
 */
extern DECLSPEC SDL_GpuRenderPass *SDLCALL SDL_GpuBeginRenderPass(
	SDL_GpuCommandBuffer *commandBuffer,
	SDL_GpuColorAttachmentInfo *colorAttachmentInfos,
	Uint32 colorAttachmentCount,
	SDL_GpuDepthStencilAttachmentInfo *depthStencilAttachmentInfo
);

/**
 * Binds a graphics pipeline on a render pass to be used in rendering.
 * A graphics pipeline must be bound before making any draw calls.
 *
 * \param renderPass a render pass handle
 * \param graphicsPipeline the graphics pipeline to bind
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindGraphicsPipeline(
    SDL_GpuRenderPass *renderPass,
	SDL_GpuGraphicsPipeline *graphicsPipeline
);

/**
 * Sets the current viewport state on a command buffer.
 *
 * \param renderPass a render pass handle
 * \param viewport the viewport to set
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetViewport(
    SDL_GpuRenderPass *renderPass,
	SDL_GpuViewport *viewport
);

/**
 * Sets the current scissor state on a command buffer.
 *
 * \param renderPass a render pass handle
 * \param scissor the scissor area to set
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetScissor(
    SDL_GpuRenderPass *renderPass,
	SDL_GpuRect *scissor
);

/**
 * Binds vertex buffers on a command buffer for use with subsequent draw calls.
 *
 * \param renderPass a render pass handle
 * \param firstBinding the starting bind point for the vertex buffers
 * \param bindingCount the number of bindings in the pBindings array
 * \param pBindings an array of SDL_GpuBufferBinding structs containing vertex buffers and offset values
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindVertexBuffers(
    SDL_GpuRenderPass *renderPass,
	Uint32 firstBinding,
	Uint32 bindingCount,
	SDL_GpuBufferBinding *pBindings
);

/**
 * Binds an index buffer on a command buffer for use with subsequent draw calls.
 *
 * \param renderPass a render pass handle
 * \param pBinding a pointer to a struct containing an index buffer and offset
 * \param indexElementSize whether the index values in the buffer are 16- or 32-bit
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindIndexBuffer(
    SDL_GpuRenderPass *renderPass,
	SDL_GpuBufferBinding *pBinding,
	SDL_GpuIndexElementSize indexElementSize
);

/**
 * Binds resources on the currently bound graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param resourceBindings an array of resource bindings
 * \param resourceBindingCount the number of resource bindings provided
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindGraphicsResourceSet(
	SDL_GpuRenderPass *renderPass,
	SDL_GpuShaderResourceBinding *resourceBindings,
	Uint32 resourceBindingCount
);

/**
 * Pushes data to a bound uniform buffer.
 * Subsequent draw calls will use this uniform data.
 * You must not call this function before calling
 * SDL_GpuBindGraphicsResourceSet using this uniform buffer.
 * You must not bind the same uniform buffer to two command buffers simultaneously.
 *
 * \param renderPass a render pass handle
 * \param uniformBuffer a uniform buffer object
 * \param data client data to write into the uniform buffer
 * \param dataLengthInBytes the length of the data to write
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuPushGraphicsUniformData(
	SDL_GpuRenderPass *renderPass,
	SDL_GpuUniformBuffer *uniformBuffer,
	void *data,
	Uint32 dataLengthInBytes
);

/* Drawing */

/**
 * Draws data using bound graphics state with instancing enabled.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param baseVertex the starting offset to read from the vertex buffer
 * \param startIndex the starting offset to read from the index buffer
 * \param primitiveCount the number of primitives to draw
 * \param instanceCount the number of instances that will be drawn
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDrawInstancedPrimitives(
	SDL_GpuRenderPass *renderPass,
	Uint32 baseVertex,
	Uint32 startIndex,
	Uint32 primitiveCount,
	Uint32 instanceCount
);

/**
 * Draws data using bound graphics state.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param vertexStart The starting offset to read from the vertex buffer
 * \param primitiveCount The number of primitives to draw
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDrawPrimitives(
	SDL_GpuRenderPass *renderPass,
	Uint32 vertexStart,
	Uint32 primitiveCount
);

/**
 * Draws data using bound graphics state and with draw parameters set from a buffer.
 * The buffer layout should match the layout of SDL_GpuIndirectDrawCommand.
 * You must not call this function before binding a graphics pipeline.
 *
 * \param renderPass a render pass handle
 * \param gpuBuffer a buffer containing draw parameters
 * \param offsetInBytes the offset to start reading from the draw buffer
 * \param drawCount the number of draw parameter sets that should be read from the draw buffer
 * \param stride the byte stride between sets of draw parameters
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDrawPrimitivesIndirect(
	SDL_GpuRenderPass *renderPass,
	SDL_GpuBuffer *gpuBuffer,
	Uint32 offsetInBytes,
	Uint32 drawCount,
	Uint32 stride
);

/**
 * Ends the given render pass.
 * All bound graphics state on the render pass command buffer is unset.
 * The render pass handle is now invalid.
 *
 * \param renderPass a render pass handle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuEndRenderPass(
	SDL_GpuRenderPass *renderPass
);

/* Compute Pass */

/**
 * Begins a compute pass on a command buffer.
 * All operations related to compute pipelines must take place inside of a compute pass.
 * You must not begin another compute pass, or a render pass or copy pass
 * before ending the compute pass.
 *
 * \param commandBuffer a command buffer
 * \returns a compute pass handle
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuEndComputePass
 */
extern DECLSPEC SDL_GpuComputePass *SDLCALL SDL_GpuBeginComputePass(
	SDL_GpuCommandBuffer *commandBuffer
);

/**
 * Binds a compute pipeline on a command buffer for use in compute dispatch.
 *
 * \param computePass a compute pass handle
 * \param computePipeline a compute pipeline to bind
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindComputePipeline(
	SDL_GpuComputePass *computePass,
	SDL_GpuComputePipeline *computePipeline
);

/**
 * Binds resources on the currently bound compute pipeline.
 *
 * \param computePass a compute pass handle
 * \param resourceBindings an array of resource bindings
 * \param resourceBindingCount the number of resource bindings provided
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBindComputeResourceSet(
	SDL_GpuComputePass *computePass,
	SDL_GpuShaderResourceBinding *resourceBindings,
	Uint32 resourceBindingCount
);

/**
 * Pushes data to a bound uniform buffer.
 * Subsequent draw calls will use this uniform data.
 * You must not call this function before calling
 * SDL_GpuBindComputeResourceSet using this uniform buffer.
 * You must not bind the same uniform buffer to two command buffers simultaneously.
 *
 * \param computePass a compute pass handle
 * \param uniformBuffer a uniform buffer object
 * \param data client data to write into the uniform buffer
 * \param dataLengthInBytes the length of the data to write
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuPushComputeUniformData(
	SDL_GpuComputePass *computePass,
	SDL_GpuUniformBuffer *uniformBuffer,
	void *data,
	Uint32 dataLengthInBytes
);

/**
 * Dispatches compute work.
 * You must not call this function before binding a compute pipeline.
 *
 * \param computePass a compute pass handle
 * \param groupCountX number of local workgroups to dispatch in the X dimension
 * \param groupCountY number of local workgroups to dispatch in the Y dimension
 * \param groupCountZ number of local workgroups to dispatch in the Z dimension
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDispatchCompute(
	SDL_GpuComputePass *computePass,
	Uint32 groupCountX,
	Uint32 groupCountY,
	Uint32 groupCountZ
);

/**
 * Ends the current compute pass.
 * All bound compute state on the command buffer is unset.
 * The compute pass handle is now invalid.
 *
 * \param computePass a compute pass handle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuEndComputePass(
	SDL_GpuComputePass *computePass
);

/* TransferBuffer Data */

/**
 * Maps a transfer buffer into application address space.
 * You must unmap the transfer buffer before encoding upload commands.
 *
 * \param device a GPU context
 * \param transferBuffer a transfer buffer
 * \param ppData where to store the address of the mapped transfer buffer memory
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuMapTransferBuffer(
    SDL_GpuDevice *device,
    SDL_GpuTransferBuffer *transferBuffer,
    void **ppData
);

/**
 * Unmaps a previously mapped transfer buffer.
 *
 * \param device a GPU context
 * \param transferBuffer a previously mapped transfer buffer
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuUnmapTransferBuffer(
    SDL_GpuDevice *device,
    SDL_GpuTransferBuffer *transferBuffer
);

/**
 * Immediately copies data from a pointer to a transfer buffer.
 *
 * \param device a GPU context
 * \param data a pointer to data to copy into the transfer buffer
 * \param transferBuffer a transfer buffer
 * \param copyParams a struct containing parameters specifying copy offsets and size
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetTransferData(
	SDL_GpuDevice *device,
	void* data,
	SDL_GpuTransferBuffer *transferBuffer,
	SDL_GpuBufferCopy *copyParams
);

/**
 * Immediately copies data from a transfer buffer to a pointer.
 *
 * \param device a GPU context
 * \param transferBuffer a transfer buffer
 * \param data a data pointer
 * \param copyParams a struct containing parameters specifying copy offsets and size
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuGetTransferData(
	SDL_GpuDevice *device,
	SDL_GpuTransferBuffer *transferBuffer,
	void* data,
	SDL_GpuBufferCopy *copyParams
);

/* Copy Pass */

/**
 * Begins a copy pass on a command buffer.
 * All operations related to copying to or from buffers or textures take place inside a copy pass.
 * You must not begin another copy pass, or a render pass or compute pass
 * before ending the copy pass.
 *
 * \param commandBuffer a command buffer
 * \returns a copy pass handle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuCopyPass *SDLCALL SDL_GpuBeginCopyPass(
	SDL_GpuCommandBuffer *commandBuffer
);

/**
 * Uploads data from a transfer buffer to a texture.
 * The upload occurs on the GPU timeline.
 * You may assume that the upload has finished in subsequent commands.
 *
 * You must align the data in the transfer buffer to a multiple of
 * the texel size of the texture format.
 *
 * \param copyPass a copy pass handle
 * \param transferBuffer a transfer buffer
 * \param textureRegion a struct containing parameters specifying the texture region to upload data to
 * \param copyParams a struct containing parameters specifying buffer offset, stride, and height
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuUploadToTexture(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuTransferBuffer *transferBuffer,
	SDL_GpuTextureRegion *textureRegion,
	SDL_GpuBufferImageCopy *copyParams
);

/* Uploads data from a TransferBuffer to a GpuBuffer. */

/**
 * Uploads data from a transfer buffer to a buffer.
 * The upload occurs on the GPU timeline.
 * You may assume that the upload has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param transferBuffer a transfer buffer
 * \param gpuBuffer a buffer
 * \param copyParams a struct containing offsets and length
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuUploadToBuffer(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuTransferBuffer *transferBuffer,
	SDL_GpuBuffer *gpuBuffer,
	SDL_GpuBufferCopy *copyParams
);

/**
 * Performs a texture-to-texture copy.
 * This copy occurs on the GPU timeline.
 * You may assume the copy has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param source a source texture region
 * \param destination must be the same dimensions as the source region
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuCopyTextureToTexture(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuTextureRegion *source,
	SDL_GpuTextureRegion *destination
);

/* Copies data from a buffer to a buffer. */

/**
 * Performs a buffer-to-buffer copy.
 * This copy occurs on the GPU timeline.
 * You may assume the copy has finished in subsequent commands.
 *
 * \param copyPass a copy pass handle
 * \param source the buffer to copy from
 * \param destination the buffer to copy to
 * \param copyParams a struct containing offset and length data
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuCopyBufferToBuffer(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuBuffer *source,
	SDL_GpuBuffer *destination,
	SDL_GpuBufferCopy *copyParams
);

/**
 * Generates mipmaps for the given texture.
 *
 * \param copyPass a copy pass handle
 * \param texture a texture with more than 1 mip level
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuGenerateMipmaps(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuTexture *texture
);

/**
 * Copies data from a texture to a transfer buffer on the GPU timeline.
 * This data is not guaranteed to be copied until the command buffer fence is signaled.
 *
 * \param copyPass a copy pass handle
 * \param textureRegion the texture region to download
 * \param transferBuffer the transfer buffer to download into
 * \param copyParams a struct containing parameters specifying buffer offset, stride, and height
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDownloadFromTexture(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuTextureRegion *textureRegion,
	SDL_GpuTransferBuffer *transferBuffer,
	SDL_GpuBufferImageCopy *copyParams
);

/**
 * Copies data from a buffer to a transfer buffer on the GPU timeline.
 * This data is not guaranteed to be copied until the command buffer fence is signaled.
 *
 * \param copyPass a copy pass handle
 * \param gpuBuffer the buffer to download
 * \param transferBuffer the transfer buffer to download into
 * \param copyParams a struct containing offsets and length
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuDownloadFromBuffer(
	SDL_GpuCopyPass *copyPass,
	SDL_GpuBuffer *gpuBuffer,
	SDL_GpuTransferBuffer *transferBuffer,
	SDL_GpuBufferCopy *copyParams
);

/**
 * Ends the current copy pass.
 *
 * \param copyPass a copy pass handle
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuEndCopyPass(
	SDL_GpuCopyPass *copyPass
);

/**
 * Blits from a source texture region to a destination texture region.
 * This function must not be called inside of any render, compute, or copy pass.
 *
 * \param commandBuffer a command buffer
 * \param source the texture region to copy from
 * \param destination the texture region to copy to
 * \param filterMode the filter mode that will be used when blitting
 *
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuBlit(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTextureRegion *source,
    SDL_GpuTextureRegion *destination,
    SDL_GpuFilter filterMode
);

/* Submission/Presentation */

/**
 * Claims a window, creating a swapchain structure for it.
 * This must be called before SDL_GpuAcquireSwapchainTexture is called using the window.
 *
 * Note that for performance reasons an operating system may swizzle the format internally
 * (for example, RGBA may be stored as BGRA) so be sure to render or blit to swapchain textures
 * to avoid format swizzling issues.
 *
 * \param device a GPU context
 * \param windowHandle an SDL_Window
 * \param presentMode the desired present mode for the swapchain
 * \param swapchainFormat the desired swapchain format, MUST be an RGBA-ordered format
 * \param colorSpace the desired color space for the swapchain
 *
 * \returns SDL_TRUE on success, otherwise SDL_FALSE.
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuAcquireSwapchainTexture
 * \sa SDL_GpuUnclaimWindow
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuClaimWindow(
	SDL_GpuDevice *device,
	SDL_Window *windowHandle,
	SDL_GpuPresentMode presentMode,
    SDL_GpuTextureFormat swapchainFormat,
    SDL_GpuColorSpace colorSpace
);

/**
 * Unclaims a window, destroying its swapchain structure.
 *
 * \param device a GPU context
 * \param windowHandle an SDL_Window that has been claimed
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuClaimWindow
 */
extern DECLSPEC void SDLCALL SDL_GpuUnclaimWindow(
	SDL_GpuDevice *device,
	SDL_Window *windowHandle
);

/**
 * Changes the swapchain parameters for the given claimed window.
 *
 * \param device a GPU context
 * \param windowHandle an SDL_Window that has been claimed
 * \param presentMode the desired present mode for the swapchain
 * \param swapchainFormat the desired swapchain format, MUST be an RGBA-ordered format
 * \param colorSpace the desired color space for the swapchain
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuSetSwapchainParameters(
	SDL_GpuDevice *device,
	SDL_Window *windowHandle,
	SDL_GpuPresentMode presentMode,
    SDL_GpuTextureFormat swapchainFormat,
    SDL_GpuColorSpace colorSpace
);

/**
 * Obtains the texture format of the swapchain for the given window.
 *
 * \param device a GPU context
 * \param windowHandle an SDL_Window that has been claimed
 * \returns the texture format of the swapchain
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuTextureFormat SDLCALL SDL_GpuGetSwapchainFormat(
	SDL_GpuDevice *device,
	SDL_Window *windowHandle
);

/**
 * Obtains whether or not a presentation mode is supported by the GPU backend.
 *
 * \param device a GPU context
 * \param presentMode the presentation mode to check
 * \returns SDL_TRUE if supported, SDL_FALSE if unsupported (or on error)
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuSupportsPresentMode(
	SDL_GpuDevice *device,
	SDL_GpuPresentMode presentMode
);

/**
 * Obtains whether or not the GPU backend supports a bindless workflow.
 *
 * \param device a GPU context
 * \returns SDL_TRUE if supported, SDL_FALSE if unsupported
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuSupportsBindless(
    SDL_GpuDevice *device
);

/**
 * Acquire a command buffer.
 * This command buffer is managed by the implementation and should not be freed by the user.
 * A command buffer may only be used on the thread it was acquired on.
 *
 * \param device a GPU context
 * \returns a command buffer
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSubmit
 * \sa SDL_GpuSubmitAndAcquireFence
 */
extern DECLSPEC SDL_GpuCommandBuffer *SDLCALL SDL_GpuAcquireCommandBuffer(
	SDL_GpuDevice *device
);

/**
 * Acquire a texture to use in presentation.
 * When a swapchain texture is acquired on a command buffer,
 * it will automatically be submitted for presentation when the command buffer is submitted.
 * May return NULL under certain conditions. This is not necessarily an error.
 * This texture is managed by the implementation and must not be freed by the user.
 * You MUST NOT call this function from any thread other than the one that created the window.
 *
 * \param commandBuffer a command buffer
 * \param windowHandle a window that has been claimed
 * \param pWidth a pointer filled in with the swapchain width
 * \param pHeight a pointer filled in with the swapchain height
 * \returns a swapchain texture
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuClaimWindow
 * \sa SDL_GpuSubmit
 * \sa SDL_GpuSubmitAndAcquireFence
 */
extern DECLSPEC SDL_GpuTexture *SDLCALL SDL_GpuAcquireSwapchainTexture(
	SDL_GpuCommandBuffer *commandBuffer,
	SDL_Window *windowHandle,
	Uint32 *pWidth,
	Uint32 *pHeight
);

/**
 * Submits a command buffer so its commands can be processed on the GPU.
 * It is invalid to use the command buffer after this is called.
 *
 * \param commandBuffer a command buffer
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuAcquireCommandBuffer
 * \sa SDL_GpuAcquireSwapchainTexture
 * \sa SDL_GpuSubmitAndAcquireFence
 */
extern DECLSPEC void SDLCALL SDL_GpuSubmit(
	SDL_GpuCommandBuffer *commandBuffer
);

/**
 * Submits a command buffer so its commands can be processed on the GPU,
 * and acquires a fence associated with the command buffer.
 * You must release this fence when it is no longer needed or it will cause a leak.
 * It is invalid to use the command buffer after this is called.
 *
 * \param commandBuffer a command buffer
 * \returns a fence associated with the command buffer
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_AcquireCommandBuffer
 * \sa SDL_GpuAcquireSwapchainTexture
 * \sa SDL_GpuSubmit
 * \sa SDL_GpuReleaseFence
 */
extern DECLSPEC SDL_GpuFence *SDLCALL SDL_GpuSubmitAndAcquireFence(
	SDL_GpuCommandBuffer *commandBuffer
);

/**
 * Blocks the thread until the GPU is completely idle.
 *
 * \param device a GPU context
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuWaitForFences
 */
extern DECLSPEC void SDLCALL SDL_GpuWait(
	SDL_GpuDevice *device
);

/**
 * Blocks the thread until the given fences are signaled.
 *
 * \param device a GPU context
 * \param waitAll if 0, wait for any fence to be signaled, if 1, wait for all fences to be signaled
 * \param fenceCount the number of fences in the pFences array
 * \param pFences an array of fences to wait on
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSubmitAndAcquireFence
 * \sa SDL_GpuWait
 */
extern DECLSPEC void SDLCALL SDL_GpuWaitForFences(
	SDL_GpuDevice *device,
	Uint8 waitAll,
	Uint32 fenceCount,
	SDL_GpuFence **pFences
);

/**
 * Checks the status of a fence.
 *
 * \param device a GPU context
 * \param fence a fence
 * \returns SDL_TRUE if the fence is signaled, SDL_FALSE if it is not
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSubmitAndAcquireFence
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuQueryFence(
	SDL_GpuDevice *device,
	SDL_GpuFence *fence
);

/**
 * Releases a fence obtained from SDL_GpuSubmitAndAcquireFence.
 *
 * \param device a GPU context
 * \param fence a fence
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSubmitAndAcquireFence
 */
extern DECLSPEC void SDLCALL SDL_GpuReleaseFence(
	SDL_GpuDevice *device,
	SDL_GpuFence *fence
);

/* Format Info */

/**
 * Obtains the texel block size for a texture format.
 *
 * \param textureFormat the texture format you want to know the texel size of
 * \returns the texel block size of the texture format
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuSetTransferData
 * \sa SDL_GpuUploadToTexture
 */
extern DECLSPEC Uint32 SDLCALL SDL_GpuTextureFormatTexelBlockSize(
    SDL_GpuTextureFormat textureFormat
);

/**
 * Determines whether a texture format is supported for a given type and usage.
 *
 * \param device a GPU context
 * \param format the texture format to check
 * \param type the type of texture (2D, 3D, Cube)
 * \param usage a bitmask of all usage scenarios to check
 * \returns whether the texture format is supported for this type and usage
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuIsTextureFormatSupported(
    SDL_GpuDevice *device,
    SDL_GpuTextureFormat format,
    SDL_GpuTextureType type,
    SDL_GpuTextureUsageFlags usage
);

/**
 * Determines the "best" sample count for a texture format, i.e.
 * the highest supported sample count that is <= the desired sample count.
 *
 * \param device a GPU context
 * \param format the texture format to check
 * \param desiredSampleCount the sample count you want
 * \returns a hardware-specific version of min(preferred, possible)
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_GpuSampleCount SDLCALL SDL_GpuGetBestSampleCount(
    SDL_GpuDevice *device,
    SDL_GpuTextureFormat format,
    SDL_GpuSampleCount desiredSampleCount
);

/* Queries */

/**
 * Begins an occlusion query.
 *
 * This function is included for compatibility with old render systems.
 * If you are not supporting an old application, do NOT use this.
 *
 * \param commandBuffer a command buffer
 * \param query an occlusion query object
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC void SDLCALL SDL_GpuOcclusionQueryBegin(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuOcclusionQuery *query
);

/**
 * Ends an occlusion query.
 * You must have called SDL_GpuOcclusionQueryBegin on the query beforehand.
 *
 * This function is included for compatibility with old render systems.
 * If you are not supporting an old application, do NOT use this.
 *
 * \param commandBuffer a commandBuffer
 * \param query an occlusion query object
 *
 * \since This function is available since SDL 3.x.x
 *
 * \sa SDL_GpuBeginQuery
 */
extern DECLSPEC void SDLCALL SDL_GpuOcclusionQueryEnd(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuOcclusionQuery *query
);

/**
 * Checks if an occlusion query is complete and fills in the pixel count of the query.
 *
 * This function is included for compatibility with old render systems.
 * If you are not supporting an old application, do NOT use this.
 *
 * \param device a GPU context
 * \param query an occlusion query object
 * \param pixelCount a pointer to be filled with the pixel count
 * \returns SDL_TRUE if the occlusion query is complete, SDL_FALSE otherwise
 *
 * \since This function is available since SDL 3.x.x
 */
extern DECLSPEC SDL_bool SDLCALL SDL_GpuOcclusionQueryPixelCount(
    SDL_GpuDevice *device,
    SDL_GpuOcclusionQuery *query,
    Uint32 *pixelCount
);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SDL_GPU_H */
