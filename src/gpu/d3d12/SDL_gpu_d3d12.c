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

#include "SDL_internal.h"

#if SDL_GPU_D3D12

#include "../../video/directx/SDL_d3d12.h"
#include "../SDL_sysgpu.h"
#include "SDL_hashtable.h"

/* Built-in shaders, compiled with compile_shaders.bat */

#define g_main D3D12_BlitFrom2D
#include "D3D12_BlitFrom2D.h"
#undef g_main

/*
#define g_main D3D11_BlitFrom2DArray
#include "D3D11_BlitFrom2DArray.h"
#undef g_main
*/

#define g_main D3D12_FullscreenVert
#include "D3D12_FullscreenVert.h"
#undef g_main

/* Macros */

#define ERROR_CHECK(msg)                                     \
    if (FAILED(res)) {                                       \
        D3D12_INTERNAL_LogError(renderer->device, msg, res); \
    }

#define ERROR_CHECK_RETURN(msg, ret)                         \
    if (FAILED(res)) {                                       \
        D3D12_INTERNAL_LogError(renderer->device, msg, res); \
        return ret;                                          \
    }

#define EXPAND_ARRAY_IF_NEEDED(arr, elementType, newCount, capacity, newCapacity) \
    if (newCount >= capacity) {                                                   \
        capacity = newCapacity;                                                   \
        arr = (elementType *)SDL_realloc(                                         \
            arr,                                                                  \
            sizeof(elementType) * capacity);                                      \
    }

/* Defines */
#if defined(_WIN32)
#define D3D12_DLL     "d3d12.dll"
#define DXGI_DLL      "dxgi.dll"
#define DXGIDEBUG_DLL "dxgidebug.dll"
#elif defined(__APPLE__)
#define D3D12_DLL     "libdxvk_d3d12.dylib"
#define DXGI_DLL      "libdxvk_dxgi.dylib"
#define DXGIDEBUG_DLL "libdxvk_dxgidebug.dylib"
#else
#define D3D12_DLL     "libdxvk_d3d12.so"
#define DXGI_DLL      "libdxvk_dxgi.so"
#define DXGIDEBUG_DLL "libdxvk_dxgidebug.so"
#endif

#define D3D12_CREATE_DEVICE_FUNC            "D3D12CreateDevice"
#define D3D12_SERIALIZE_ROOT_SIGNATURE_FUNC "D3D12SerializeRootSignature"
#define CREATE_DXGI_FACTORY1_FUNC           "CreateDXGIFactory1"
#define DXGI_GET_DEBUG_INTERFACE_FUNC       "DXGIGetDebugInterface"
#define D3D12_GET_DEBUG_INTERFACE_FUNC      "D3D12GetDebugInterface"
#define WINDOW_PROPERTY_DATA                "SDL_GpuD3D12WindowPropertyData"
#define D3D_FEATURE_LEVEL_CHOICE            D3D_FEATURE_LEVEL_11_1
#define D3D_FEATURE_LEVEL_CHOICE_STR        "11_1"
/* FIXME: just use sysgpu.h defines */
#define MAX_ROOT_SIGNATURE_PARAMETERS         64
#define VIEW_GPU_DESCRIPTOR_COUNT             65536
#define SAMPLER_GPU_DESCRIPTOR_COUNT          2048
#define VIEW_SAMPLER_STAGING_DESCRIPTOR_COUNT 1000000
#define TARGET_STAGING_DESCRIPTOR_COUNT       1000000
#define D3D12_FENCE_UNSIGNALED_VALUE          0
#define D3D12_FENCE_SIGNAL_VALUE              1

#define SDL_GPU_SHADERSTAGE_COMPUTE (SDL_GpuShaderStage)2

#define EXPAND_ELEMENTS_IF_NEEDED(arr, initialValue, type) \
    if (arr->count == arr->capacity) {                     \
        if (arr->capacity == 0) {                          \
            arr->capacity = initialValue;                  \
        } else {                                           \
            arr->capacity *= 2;                            \
        }                                                  \
        arr->elements = (type *)SDL_realloc(               \
            arr->elements,                                 \
            arr->capacity * sizeof(type));                 \
    }

#ifdef _WIN32
#define HRESULT_FMT "(0x%08lX)"
#else
#define HRESULT_FMT "(0x%08X)"
#endif

/* Function Pointer Signatures */
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY1)(const GUID *riid, void **ppFactory);
typedef HRESULT(WINAPI *PFN_DXGI_GET_DEBUG_INTERFACE)(const GUID *riid, void **ppDebug);

/* IIDs (from https://www.magnumdb.com/) */
static const IID D3D_IID_IDXGIFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const IID D3D_IID_IDXGIFactory4 = { 0x1bc6ea02, 0xef36, 0x464f, { 0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a } };
static const IID D3D_IID_IDXGIFactory5 = { 0x7632e1f5, 0xee65, 0x4dca, { 0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d } };
static const IID D3D_IID_IDXGIFactory6 = { 0xc1b6694f, 0xff09, 0x44a9, { 0xb0, 0x3c, 0x77, 0x90, 0x0a, 0x0a, 0x1d, 0x17 } };
static const IID D3D_IID_IDXGIAdapter1 = { 0x29038f61, 0x3839, 0x4626, { 0x91, 0xfd, 0x08, 0x68, 0x79, 0x01, 0x1a, 0x05 } };
static const IID D3D_IID_IDXGISwapChain3 = { 0x94d99bdb, 0xf1f8, 0x4ab0, { 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1 } };
static const IID D3D_IID_IDXGIDebug = { 0x119e7452, 0xde9e, 0x40fe, { 0x88, 0x06, 0x88, 0xf9, 0x0c, 0x12, 0xb4, 0x41 } };
static const IID D3D_IID_IDXGIInfoQueue = { 0xd67441c7, 0x672a, 0x476f, { 0x9e, 0x82, 0xcd, 0x55, 0xb4, 0x49, 0x49, 0xce } };
static const GUID D3D_IID_DXGI_DEBUG_ALL = { 0xe48ae283, 0xda80, 0x490b, { 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x08 } };
static const GUID D3D_IID_D3DDebugObjectName = { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };

// static const IID D3D_IID_ID3DUserDefinedAnnotation = { 0xb2daad8b, 0x03d4, 0x4dbf, { 0x95, 0xeb, 0x32, 0xab, 0x4b, 0x63, 0xd0, 0xab } };

static const IID D3D_IID_ID3D12Device = { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
static const IID D3D_IID_ID3D12CommandQueue = { 0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed } };
static const IID D3D_IID_ID3D12DescriptorHeap = { 0x8efb471d, 0x616c, 0x4f49, { 0x90, 0xf7, 0x12, 0x7b, 0xb7, 0x63, 0xfa, 0x51 } };
static const IID D3D_IID_ID3D12Resource = { 0x696442be, 0xa72e, 0x4059, { 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad } };
static const IID D3D_IID_ID3D12CommandAllocator = { 0x6102dee4, 0xaf59, 0x4b09, { 0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24 } };
static const IID D3D_IID_ID3D12CommandList = { 0x7116d91c, 0xe7e4, 0x47ce, { 0xb8, 0xc6, 0xec, 0x81, 0x68, 0xf4, 0x37, 0xe5 } };
static const IID D3D_IID_ID3D12GraphicsCommandList = { 0x5b160d0f, 0xac1b, 0x4185, { 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55 } };
static const IID D3D_IID_ID3D12Fence = { 0x0a753dcf, 0xc4d8, 0x4b91, { 0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76 } };
static const IID D3D_IID_ID3D12RootSignature = { 0xc54a6b66, 0x72df, 0x4ee8, { 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14 } };
static const IID D3D_IID_ID3D12CommandSignature = { 0xc36a797c, 0xec80, 0x4f0a, { 0x89, 0x85, 0xa7, 0xb2, 0x47, 0x50, 0x82, 0xd1 } };
static const IID D3D_IID_ID3D12PipelineState = { 0x765a30f3, 0xf624, 0x4c6f, { 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45 } };
static const IID D3D_IID_ID3D12Debug = { 0x344488b7, 0x6846, 0x474b, { 0xb9, 0x89, 0xf0, 0x27, 0x44, 0x82, 0x45, 0xe0 } };
static const IID D3D_IID_ID3D12InfoQueue = { 0x0742a90b, 0xc387, 0x483f, { 0xb9, 0x46, 0x30, 0xa7, 0xe4, 0xe6, 0x14, 0x58 } };

/* Enums */

typedef enum D3D12BufferType
{
    D3D12_BUFFER_TYPE_GPU,
    D3D12_BUFFER_TYPE_UNIFORM,
    D3D12_BUFFER_TYPE_UPLOAD,
    D3D12_BUFFER_TYPE_DOWNLOAD
} D3D12BufferType;

/* Conversions */

static DXGI_FORMAT SwapchainCompositionToTextureFormat[] = {
    DXGI_FORMAT_B8G8R8A8_UNORM,                /* SDR */
    DXGI_FORMAT_B8G8R8A8_UNORM, /* SDR_SRGB */ /* NOTE: The RTV uses the sRGB format */
    DXGI_FORMAT_R16G16B16A16_FLOAT,            /* HDR */
    DXGI_FORMAT_R10G10B10A2_UNORM,             /* HDR_ADVANCED*/
};

static DXGI_COLOR_SPACE_TYPE SwapchainCompositionToColorSpace[] = {
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,   /* SDR */
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,   /* SDR_SRGB */
    DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,   /* HDR */
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 /* HDR_ADVANCED */
};

static D3D12_BLEND SDLToD3D12_BlendFactor[] = {
    D3D12_BLEND_ZERO,             /* ZERO */
    D3D12_BLEND_ONE,              /* ONE */
    D3D12_BLEND_SRC_COLOR,        /* SRC_COLOR */
    D3D12_BLEND_INV_SRC_COLOR,    /* ONE_MINUS_SRC_COLOR */
    D3D12_BLEND_DEST_COLOR,       /* DST_COLOR */
    D3D12_BLEND_INV_DEST_COLOR,   /* ONE_MINUS_DST_COLOR */
    D3D12_BLEND_SRC_ALPHA,        /* SRC_ALPHA */
    D3D12_BLEND_INV_SRC_ALPHA,    /* ONE_MINUS_SRC_ALPHA */
    D3D12_BLEND_DEST_ALPHA,       /* DST_ALPHA */
    D3D12_BLEND_INV_DEST_ALPHA,   /* ONE_MINUS_DST_ALPHA */
    D3D12_BLEND_BLEND_FACTOR,     /* CONSTANT_COLOR */
    D3D12_BLEND_INV_BLEND_FACTOR, /* ONE_MINUS_CONSTANT_COLOR */
    D3D12_BLEND_SRC_ALPHA_SAT,    /* SRC_ALPHA_SATURATE */
};

static D3D12_BLEND SDLToD3D12_BlendFactorAlpha[] = {
    D3D12_BLEND_ZERO,             /* ZERO */
    D3D12_BLEND_ONE,              /* ONE */
    D3D12_BLEND_SRC_ALPHA,        /* SRC_COLOR */
    D3D12_BLEND_INV_SRC_ALPHA,    /* ONE_MINUS_SRC_COLOR */
    D3D12_BLEND_DEST_ALPHA,       /* DST_COLOR */
    D3D12_BLEND_INV_DEST_ALPHA,   /* ONE_MINUS_DST_COLOR */
    D3D12_BLEND_SRC_ALPHA,        /* SRC_ALPHA */
    D3D12_BLEND_INV_SRC_ALPHA,    /* ONE_MINUS_SRC_ALPHA */
    D3D12_BLEND_DEST_ALPHA,       /* DST_ALPHA */
    D3D12_BLEND_INV_DEST_ALPHA,   /* ONE_MINUS_DST_ALPHA */
    D3D12_BLEND_BLEND_FACTOR,     /* CONSTANT_COLOR */
    D3D12_BLEND_INV_BLEND_FACTOR, /* ONE_MINUS_CONSTANT_COLOR */
    D3D12_BLEND_SRC_ALPHA_SAT,    /* SRC_ALPHA_SATURATE */
};

static D3D12_BLEND_OP SDLToD3D12_BlendOp[] = {
    D3D12_BLEND_OP_ADD,          /* ADD */
    D3D12_BLEND_OP_SUBTRACT,     /* SUBTRACT */
    D3D12_BLEND_OP_REV_SUBTRACT, /* REVERSE_SUBTRACT */
    D3D12_BLEND_OP_MIN,          /* MIN */
    D3D12_BLEND_OP_MAX           /* MAX */
};

static DXGI_FORMAT SDLToD3D12_TextureFormat[] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,       /* R8G8B8A8 */
    DXGI_FORMAT_B8G8R8A8_UNORM,       /* B8G8R8A8 */
    DXGI_FORMAT_B5G6R5_UNORM,         /* B5G6R5 */
    DXGI_FORMAT_B5G5R5A1_UNORM,       /* B5G5R5A1 */
    DXGI_FORMAT_B4G4R4A4_UNORM,       /* B4G4R4A4 */
    DXGI_FORMAT_R10G10B10A2_UNORM,    /* R10G10B10A2 */
    DXGI_FORMAT_R16G16_UNORM,         /* R16G16 */
    DXGI_FORMAT_R16G16B16A16_UNORM,   /* R16G16B16A16 */
    DXGI_FORMAT_R8_UNORM,             /* R8 */
    DXGI_FORMAT_A8_UNORM,             /* A8 */
    DXGI_FORMAT_BC1_UNORM,            /* BC1 */
    DXGI_FORMAT_BC2_UNORM,            /* BC2 */
    DXGI_FORMAT_BC3_UNORM,            /* BC3 */
    DXGI_FORMAT_BC7_UNORM,            /* BC7 */
    DXGI_FORMAT_R8G8_SNORM,           /* R8G8_SNORM */
    DXGI_FORMAT_R8G8B8A8_SNORM,       /* R8G8B8A8_SNORM */
    DXGI_FORMAT_R16_FLOAT,            /* R16_SFLOAT */
    DXGI_FORMAT_R16G16_FLOAT,         /* R16G16_SFLOAT */
    DXGI_FORMAT_R16G16B16A16_FLOAT,   /* R16G16B16A16_SFLOAT */
    DXGI_FORMAT_R32_FLOAT,            /* R32_SFLOAT */
    DXGI_FORMAT_R32G32_FLOAT,         /* R32G32_SFLOAT */
    DXGI_FORMAT_R32G32B32A32_FLOAT,   /* R32G32B32A32_SFLOAT */
    DXGI_FORMAT_R8_UINT,              /* R8_UINT */
    DXGI_FORMAT_R8G8_UINT,            /* R8G8_UINT */
    DXGI_FORMAT_R8G8B8A8_UINT,        /* R8G8B8A8_UINT */
    DXGI_FORMAT_R16_UINT,             /* R16_UINT */
    DXGI_FORMAT_R16G16_UINT,          /* R16G16_UINT */
    DXGI_FORMAT_R16G16B16A16_UINT,    /* R16G16B16A16_UINT */
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  /* R8G8B8A8_SRGB */
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,  /* B8G8R8A8_SRGB */
    DXGI_FORMAT_BC3_UNORM_SRGB,       /* BC3_SRGB */
    DXGI_FORMAT_BC7_UNORM_SRGB,       /* BC7_SRGB */
    DXGI_FORMAT_D16_UNORM,            /* D16_UNORM */
    DXGI_FORMAT_D24_UNORM_S8_UINT,    /* D24_UNORM */
    DXGI_FORMAT_D32_FLOAT,            /* D32_SFLOAT */
    DXGI_FORMAT_D24_UNORM_S8_UINT,    /* D24_UNORM_S8_UINT */
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT, /* D32_SFLOAT_S8_UINT */
};

static D3D12_COMPARISON_FUNC SDLToD3D12_CompareOp[] = {
    D3D12_COMPARISON_FUNC_NEVER,         /* NEVER */
    D3D12_COMPARISON_FUNC_LESS,          /* LESS */
    D3D12_COMPARISON_FUNC_EQUAL,         /* EQUAL */
    D3D12_COMPARISON_FUNC_LESS_EQUAL,    /* LESS_OR_EQUAL */
    D3D12_COMPARISON_FUNC_GREATER,       /* GREATER */
    D3D12_COMPARISON_FUNC_NOT_EQUAL,     /* NOT_EQUAL */
    D3D12_COMPARISON_FUNC_GREATER_EQUAL, /* GREATER_OR_EQUAL */
    D3D12_COMPARISON_FUNC_ALWAYS         /* ALWAYS */
};

static D3D12_STENCIL_OP SDLToD3D12_StencilOp[] = {
    D3D12_STENCIL_OP_KEEP,     /* KEEP */
    D3D12_STENCIL_OP_ZERO,     /* ZERO */
    D3D12_STENCIL_OP_REPLACE,  /* REPLACE */
    D3D12_STENCIL_OP_INCR_SAT, /* INCREMENT_AND_CLAMP */
    D3D12_STENCIL_OP_DECR_SAT, /* DECREMENT_AND_CLAMP */
    D3D12_STENCIL_OP_INVERT,   /* INVERT */
    D3D12_STENCIL_OP_INCR,     /* INCREMENT_AND_WRAP */
    D3D12_STENCIL_OP_DECR      /* DECREMENT_AND_WRAP */
};

static D3D12_CULL_MODE SDLToD3D12_CullMode[] = {
    D3D12_CULL_MODE_NONE,  /* NONE */
    D3D12_CULL_MODE_FRONT, /* FRONT */
    D3D12_CULL_MODE_BACK   /* BACK */
};

static D3D12_FILL_MODE SDLToD3D12_FillMode[] = {
    D3D12_FILL_MODE_SOLID,    /* FILL */
    D3D12_FILL_MODE_WIREFRAME /* LINE */
};

static D3D12_INPUT_CLASSIFICATION SDLToD3D12_InputRate[] = {
    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,  /* VERTEX */
    D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA /* INSTANCE */
};

static DXGI_FORMAT SDLToD3D12_VertexFormat[] = {
    DXGI_FORMAT_R32_UINT,           /* UINT */
    DXGI_FORMAT_R32_FLOAT,          /* FLOAT */
    DXGI_FORMAT_R32G32_FLOAT,       /* VECTOR2 */
    DXGI_FORMAT_R32G32B32_FLOAT,    /* VECTOR3 */
    DXGI_FORMAT_R32G32B32A32_FLOAT, /* VECTOR4 */
    DXGI_FORMAT_R8G8B8A8_UNORM,     /* COLOR */
    DXGI_FORMAT_R8G8B8A8_UINT,      /* BYTE4 */
    DXGI_FORMAT_R16G16_SINT,        /* SHORT2 */
    DXGI_FORMAT_R16G16B16A16_SINT,  /* SHORT4 */
    DXGI_FORMAT_R16G16_SNORM,       /* NORMALIZEDSHORT2 */
    DXGI_FORMAT_R16G16B16A16_SNORM, /* NORMALIZEDSHORT4 */
    DXGI_FORMAT_R16G16_FLOAT,       /* HALFVECTOR2 */
    DXGI_FORMAT_R16G16B16A16_FLOAT  /* HALFVECTOR4 */
};

static int SDLToD3D12_SampleCount[] = {
    1, /* SDL_GPU_SAMPLECOUNT_1 */
    2, /* SDL_GPU_SAMPLECOUNT_2 */
    4, /* SDL_GPU_SAMPLECOUNT_4 */
    8, /* SDL_GPU_SAMPLECOUNT_8 */
};

static D3D12_PRIMITIVE_TOPOLOGY SDLToD3D12_PrimitiveType[] = {
    D3D_PRIMITIVE_TOPOLOGY_POINTLIST,    // POINTLIST
    D3D_PRIMITIVE_TOPOLOGY_LINELIST,     // LINELIST
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP,    // LINESTRIP
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // TRIANGLELIST
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP // TRIANGLESTRIP
};

static D3D12_TEXTURE_ADDRESS_MODE SDLToD3D12_SamplerAddressMode[] = {
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,   /* REPEAT */
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR, /* MIRRORED_REPEAT */
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP   /* CLAMP_TO_EDGE */
};

static D3D12_FILTER SDLToD3D12_Filter(
    SDL_GpuFilter minFilter,
    SDL_GpuFilter magFilter,
    SDL_GpuSamplerMipmapMode mipmapMode,
    SDL_bool comparisonEnabled,
    SDL_bool anisotropyEnabled)
{
    D3D12_FILTER result = D3D12_ENCODE_BASIC_FILTER(
        (minFilter == SDL_GPU_FILTER_LINEAR) ? 1 : 0,
        (magFilter == SDL_GPU_FILTER_LINEAR) ? 1 : 0,
        (mipmapMode == SDL_GPU_SAMPLERMIPMAPMODE_LINEAR) ? 1 : 0,
        comparisonEnabled);

    if (anisotropyEnabled) {
        result = (D3D12_FILTER)(result | D3D12_ANISOTROPIC_FILTERING_BIT);
    }

    return result;
}

/* Structures */
typedef struct D3D12Renderer D3D12Renderer;
typedef struct D3D12CommandBufferPool D3D12CommandBufferPool;
typedef struct D3D12CommandBuffer D3D12CommandBuffer;
typedef struct D3D12Texture D3D12Texture;
typedef struct D3D12Shader D3D12Shader;
typedef struct D3D12GraphicsPipeline D3D12GraphicsPipeline;
typedef struct D3D12ComputePipeline D3D12ComputePipeline;
typedef struct D3D12Buffer D3D12Buffer;
typedef struct D3D12BufferContainer D3D12BufferContainer;
typedef struct D3D12UniformBuffer D3D12UniformBuffer;
typedef struct D3D12DescriptorHeap D3D12DescriptorHeap;
typedef struct D3D12TextureDownload D3D12TextureDownload;

typedef struct D3D12Fence
{
    ID3D12Fence *handle;
    HANDLE event; /* used for blocking */
    SDL_AtomicInt referenceCount;
} D3D12Fence;

struct D3D12DescriptorHeap
{
    ID3D12DescriptorHeap *handle;
    D3D12_DESCRIPTOR_HEAP_TYPE heapType;
    D3D12_CPU_DESCRIPTOR_HANDLE descriptorHeapCPUStart;
    D3D12_GPU_DESCRIPTOR_HANDLE descriptorHeapGPUStart; /* only exists if staging is SDL_TRUE */
    Uint32 maxDescriptors;
    Uint32 descriptorSize;
    SDL_bool staging;

    Uint32 currentDescriptorIndex;

    Uint32 *inactiveDescriptorIndices; /* only exists if staging is SDL_TRUE */
    Uint32 inactiveDescriptorCount;
};

typedef struct D3D12DescriptorHeapPool
{
    Uint32 capacity;
    Uint32 count;
    D3D12DescriptorHeap **heaps;
    SDL_Mutex *lock;
} D3D12DescriptorHeapPool;

typedef struct D3D12CPUDescriptor
{
    D3D12DescriptorHeap *heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    Uint32 cpuHandleIndex;
} D3D12CPUDescriptor;

typedef struct D3D12TextureContainer
{
    TextureCommonHeader header;

    D3D12Texture *activeTexture;

    D3D12Texture **textures;
    Uint32 textureCapacity;
    Uint32 textureCount;

    /* Swapchain images cannot be cycled */
    SDL_bool canBeCycled;

    char *debugName;
} D3D12TextureContainer;

/* TODO: how should we represent null views? */
typedef struct D3D12TextureSubresource
{
    D3D12Texture *parent;
    Uint32 layer;
    Uint32 level;
    Uint32 index;

    D3D12CPUDescriptor rtvHandle; /* NULL if not a color target */
    D3D12CPUDescriptor dsvHandle; /* NULL if not a depth stencil target */
    D3D12CPUDescriptor srvHandle; /* NULL if not a storage texture */
    D3D12CPUDescriptor uavHandle; /* NULL if not a compute storage write texture */

    SDL_AtomicInt referenceCount;
} D3D12TextureSubresource;

struct D3D12Texture
{
    D3D12TextureContainer *container;
    Uint32 containerIndex;

    D3D12TextureSubresource *subresources;
    Uint32 subresourceCount; /* layerCount * levelCount */

    ID3D12Resource *resource;
    D3D12CPUDescriptor srvHandle;
};

typedef struct D3D12Sampler
{
    SDL_GpuSamplerCreateInfo createInfo;
    D3D12CPUDescriptor handle;
    SDL_AtomicInt referenceCount;
} D3D12Sampler;

typedef struct D3D12WindowData
{
    SDL_Window *window;
    IDXGISwapChain3 *swapchain;
    SDL_GpuPresentMode presentMode;
    SDL_GpuSwapchainComposition swapchainComposition;
    DXGI_FORMAT swapchainFormat;
    DXGI_COLOR_SPACE_TYPE swapchainColorSpace;
    Uint32 frameCounter;

    D3D12TextureContainer textureContainers[MAX_FRAMES_IN_FLIGHT];
    D3D12Fence *inFlightFences[MAX_FRAMES_IN_FLIGHT];
} D3D12WindowData;

typedef struct D3D12PresentData
{
    D3D12WindowData *windowData;
    Uint32 swapchainImageIndex;
} D3D12PresentData;

struct D3D12Renderer
{
    void *dxgidebug_dll;
    IDXGIDebug *dxgiDebug;
    IDXGIInfoQueue *dxgiInfoQueue;
    ID3D12Debug *d3d12Debug;
    void *dxgi_dll;
    IDXGIFactory4 *factory;
    SDL_bool supportsTearing;
    IDXGIAdapter1 *adapter;
    void *d3d12_dll;
    ID3D12Device *device;
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE D3D12SerializeRootSignature_func;
    const char *semantic;
    SDL_iconv_t iconv;

    ID3D12CommandQueue *commandQueue;

    SDL_bool debugMode;
    SDL_bool GPUUploadHeapSupported;
    /* FIXME: these might not be necessary since we're not using custom heaps */
    SDL_bool UMA;
    SDL_bool UMACacheCoherent;

    /* Indirect command signatures */
    ID3D12CommandSignature *indirectDrawCommandSignature;
    ID3D12CommandSignature *indirectIndexedDrawCommandSignature;
    ID3D12CommandSignature *indirectDispatchCommandSignature;

    /* Blit */
    SDL_GpuGraphicsPipeline *blitFrom2DPipeline;
    SDL_GpuSampler *blitNearestSampler;
    SDL_GpuSampler *blitLinearSampler;

    /* Resources */

    D3D12CommandBuffer **availableCommandBuffers;
    Uint32 availableCommandBufferCount;
    Uint32 availableCommandBufferCapacity;

    D3D12CommandBuffer **submittedCommandBuffers;
    Uint32 submittedCommandBufferCount;
    Uint32 submittedCommandBufferCapacity;

    D3D12UniformBuffer **uniformBufferPool;
    Uint32 uniformBufferPoolCount;
    Uint32 uniformBufferPoolCapacity;

    D3D12WindowData **claimedWindows;
    Uint32 claimedWindowCount;
    Uint32 claimedWindowCapacity;

    D3D12Fence **availableFences;
    Uint32 availableFenceCount;
    Uint32 availableFenceCapacity;

    D3D12DescriptorHeap *stagingDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
    D3D12DescriptorHeapPool descriptorHeapPools[2];

    /* Deferred resource releasing */

    D3D12Buffer **buffersToDestroy;
    Uint32 buffersToDestroyCount;
    Uint32 buffersToDestroyCapacity;

    D3D12Texture **texturesToDestroy;
    Uint32 texturesToDestroyCount;
    Uint32 texturesToDestroyCapacity;

    D3D12Sampler **samplersToDestroy;
    Uint32 samplersToDestroyCount;
    Uint32 samplersToDestroyCapacity;

    D3D12GraphicsPipeline **graphicsPipelinesToDestroy;
    Uint32 graphicsPipelinesToDestroyCount;
    Uint32 graphicsPipelinesToDestroyCapacity;

    D3D12ComputePipeline **computePipelinesToDestroy;
    Uint32 computePipelinesToDestroyCount;
    Uint32 computePipelinesToDestroyCapacity;

    /* Locks */
    SDL_Mutex *stagingDescriptorHeapLock;
    SDL_Mutex *acquireCommandBufferLock;
    SDL_Mutex *acquireUniformBufferLock;
    SDL_Mutex *submitLock;
    SDL_Mutex *windowLock;
    SDL_Mutex *fenceLock;
    SDL_Mutex *disposeLock;
};

struct D3D12CommandBuffer
{
    // reserved for SDL_gpu
    CommandBufferCommonHeader common;

    // non owning parent reference
    D3D12Renderer *renderer;

    ID3D12CommandAllocator *commandAllocator;
    ID3D12GraphicsCommandList *graphicsCommandList;
    D3D12Fence *inFlightFence;
    SDL_bool autoReleaseFence;

    /* Presentation data */
    D3D12PresentData *presentDatas;
    Uint32 presentDataCount;
    Uint32 presentDataCapacity;

    Uint32 colorAttachmentCount;
    D3D12TextureSubresource *colorAttachmentTextureSubresources[MAX_COLOR_TARGET_BINDINGS];
    D3D12TextureSubresource *depthStencilTextureSubresource;
    D3D12GraphicsPipeline *currentGraphicsPipeline;
    D3D12ComputePipeline *currentComputePipeline;

    /* Set at acquire time */
    D3D12DescriptorHeap *gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER + 1];

    D3D12UniformBuffer **usedUniformBuffers;
    Uint32 usedUniformBufferCount;
    Uint32 usedUniformBufferCapacity;

    /* Resource slot state */
    SDL_bool needVertexSamplerBind;
    SDL_bool needVertexStorageTextureBind;
    SDL_bool needVertexStorageBufferBind;
    SDL_bool needVertexUniformBufferBind[MAX_UNIFORM_BUFFERS_PER_STAGE];
    SDL_bool needFragmentSamplerBind;
    SDL_bool needFragmentStorageTextureBind;
    SDL_bool needFragmentStorageBufferBind;
    SDL_bool needFragmentUniformBufferBind[MAX_UNIFORM_BUFFERS_PER_STAGE];

    SDL_bool needComputeReadOnlyStorageTextureBind;
    SDL_bool needComputeReadOnlyStorageBufferBind;
    SDL_bool needComputeUniformBufferBind[MAX_UNIFORM_BUFFERS_PER_STAGE];

    D3D12Texture *vertexSamplerTextures[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12Sampler *vertexSamplers[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12TextureSubresource *vertexStorageTextureSubresources[MAX_STORAGE_TEXTURES_PER_STAGE];
    D3D12Buffer *vertexStorageBuffers[MAX_STORAGE_BUFFERS_PER_STAGE];
    D3D12UniformBuffer *vertexUniformBuffers[MAX_UNIFORM_BUFFERS_PER_STAGE];

    D3D12Texture *fragmentSamplerTextures[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12Sampler *fragmentSamplers[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12TextureSubresource *fragmentStorageTextureSlices[MAX_STORAGE_TEXTURES_PER_STAGE];
    D3D12Buffer *fragmentStorageBuffers[MAX_STORAGE_BUFFERS_PER_STAGE];
    D3D12UniformBuffer *fragmentUniformBuffers[MAX_UNIFORM_BUFFERS_PER_STAGE];

    D3D12TextureSubresource *computeReadOnlyStorageTextures[MAX_STORAGE_TEXTURES_PER_STAGE];
    D3D12Buffer *computeReadOnlyStorageBuffers[MAX_STORAGE_BUFFERS_PER_STAGE];
    /* FIXME: what's the actual UAV limit? */
    D3D12TextureSubresource *computeReadWriteStorageTextures[MAX_STORAGE_TEXTURES_PER_STAGE];
    Uint32 computeReadWriteStorageTextureCount;
    D3D12Buffer *computeReadWriteStorageBuffers[MAX_STORAGE_BUFFERS_PER_STAGE];
    Uint32 computeReadWriteStorageBufferCount;
    D3D12UniformBuffer *computeUniformBuffers[MAX_UNIFORM_BUFFERS_PER_STAGE];

    /* Resource tracking */
    D3D12TextureSubresource **usedTextureSubresources;
    Uint32 usedTextureSubresourceCount;
    Uint32 usedTextureSubresourceCapacity;

    D3D12Buffer **usedBuffers;
    Uint32 usedBufferCount;
    Uint32 usedBufferCapacity;

    D3D12Sampler **usedSamplers;
    Uint32 usedSamplerCount;
    Uint32 usedSamplerCapacity;

    D3D12GraphicsPipeline **usedGraphicsPipelines;
    Uint32 usedGraphicsPipelineCount;
    Uint32 usedGraphicsPipelineCapacity;

    D3D12ComputePipeline **usedComputePipelines;
    Uint32 usedComputePipelineCount;
    Uint32 usedComputePipelineCapacity;

    /* Used for texture pitch hack */
    D3D12TextureDownload **textureDownloads;
    Uint32 textureDownloadCount;
    Uint32 textureDownloadCapacity;
};

struct D3D12Shader
{
    // todo cleanup
    void *bytecode;
    size_t bytecodeSize;

    Uint32 samplerCount;
    Uint32 uniformBufferCount;
    Uint32 storageBufferCount;
    Uint32 storageTextureCount;
};

typedef struct D3D12GraphicsRootSignature
{
    ID3D12RootSignature *handle;

    Sint32 vertexSamplerRootIndex;
    Sint32 vertexSamplerTextureRootIndex;
    Sint32 vertexStorageTextureRootIndex;
    Sint32 vertexStorageBufferRootIndex;

    Sint32 vertexUniformBufferRootIndex[MAX_UNIFORM_BUFFERS_PER_STAGE];

    Sint32 fragmentSamplerRootIndex;
    Sint32 fragmentSamplerTextureRootIndex;
    Sint32 fragmentStorageTextureRootIndex;
    Sint32 fragmentStorageBufferRootIndex;

    Sint32 fragmentUniformBufferRootIndex[MAX_UNIFORM_BUFFERS_PER_STAGE];
} D3D12GraphicsRootSignature;

struct D3D12GraphicsPipeline
{
    ID3D12PipelineState *pipelineState;
    D3D12GraphicsRootSignature *rootSignature;
    SDL_GpuPrimitiveType primitiveType;

    Uint32 vertexStrides[MAX_BUFFER_BINDINGS];

    float blendConstants[4];
    Uint32 stencilRef;

    Uint32 vertexSamplerCount;
    Uint32 vertexUniformBufferCount;
    Uint32 vertexStorageBufferCount;
    Uint32 vertexStorageTextureCount;

    Uint32 fragmentSamplerCount;
    Uint32 fragmentUniformBufferCount;
    Uint32 fragmentStorageBufferCount;
    Uint32 fragmentStorageTextureCount;

    SDL_AtomicInt referenceCount;
};

typedef struct D3D12ComputeRootSignature
{
    ID3D12RootSignature *handle;

    Uint32 readOnlyStorageTextureRootIndex;
    Uint32 readOnlyStorageBufferRootIndex;
    Uint32 readWriteStorageTextureRootIndex;
    Uint32 readWriteStorageBufferRootIndex;
    Uint32 uniformBufferRootIndex[MAX_UNIFORM_BUFFERS_PER_STAGE];
} D3D12ComputeRootSignature;

struct D3D12ComputePipeline
{
    ID3D12PipelineState *pipelineState;
    D3D12ComputeRootSignature *rootSignature;

    Uint32 readOnlyStorageTextureCount;
    Uint32 readOnlyStorageBufferCount;
    Uint32 readWriteStorageTextureCount;
    Uint32 readWriteStorageBufferCount;
    Uint32 uniformBufferCount;

    SDL_AtomicInt referenceCount;
};

struct D3D12TextureDownload
{
    D3D12Buffer *destinationBuffer;
    D3D12Buffer *temporaryBuffer;
    Uint32 width;
    Uint32 height;
    Uint32 depth;
    Uint32 bufferOffset;
    Uint32 bytesPerRow;
    Uint32 bytesPerDepthSlice;
    Uint32 alignedBytesPerRow;
};

struct D3D12Buffer
{
    D3D12BufferContainer *container;
    Uint32 containerIndex;

    ID3D12Resource *handle;
    D3D12CPUDescriptor uavDescriptor;
    D3D12CPUDescriptor srvDescriptor;
    D3D12CPUDescriptor cbvDescriptor;
    D3D12_GPU_VIRTUAL_ADDRESS virtualAddress;
    Uint8 *mapPointer; /* NULL except for upload buffers and fast uniform buffers */
    SDL_AtomicInt referenceCount;
    SDL_bool transitioned; /* used for initial resource barrier */
};

struct D3D12BufferContainer
{
    SDL_GpuBufferUsageFlags usageFlags;
    Uint32 size;
    D3D12BufferType type;

    D3D12Buffer *activeBuffer;

    D3D12Buffer **buffers;
    Uint32 bufferCapacity;
    Uint32 bufferCount;

    D3D12_RESOURCE_DESC bufferDesc;

    char *debugName;
};

struct D3D12UniformBuffer
{
    D3D12Buffer *buffer;
    Uint32 writeOffset;
    Uint32 drawOffset;
    Uint32 currentBlockSize;
};

typedef struct BlitFragmentUniforms
{
    /* texcoord space */
    float left;
    float top;
    float width;
    float height;
} BlitFragmentUniforms;

/* Foward function declarations */

static void D3D12_UnclaimWindow(SDL_GpuRenderer *driverData, SDL_Window *window);
static void D3D12_Wait(SDL_GpuRenderer *driverData);
static void D3D12_WaitForFences(SDL_GpuRenderer *driverData, SDL_bool waitAll, SDL_GpuFence **pFences, Uint32 fenceCount);
static void D3D12_INTERNAL_ReleaseBlitPipelines(D3D12Renderer *renderer);

/* Helpers */

static Uint32 D3D12_INTERNAL_Align(Uint32 location, Uint32 alignment)
{
    return (location + (alignment - 1)) & ~(alignment - 1);
}

/* Logging */

static void
D3D12_INTERNAL_LogError(
    ID3D12Device *device,
    const char *msg,
    HRESULT res)
{
#define MAX_ERROR_LEN 1024 /* FIXME: Arbitrary! */

    /* Buffer for text, ensure space for \0 terminator after buffer */
    char wszMsgBuff[MAX_ERROR_LEN + 1];
    DWORD dwChars; /* Number of chars returned. */

    if (res == DXGI_ERROR_DEVICE_REMOVED) {
        if (device) {
            res = ID3D12Device_GetDeviceRemovedReason(device);
        }
    }

    /* Try to get the message from the system errors. */
    dwChars = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        res,
        0,
        wszMsgBuff,
        MAX_ERROR_LEN,
        NULL);

    /* No message? Screw it, just post the code. */
    if (dwChars == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s! Error Code: " HRESULT_FMT, msg, res);
        return;
    }

    /* Ensure valid range */
    dwChars = SDL_min(dwChars, MAX_ERROR_LEN);

    /* Trim whitespace from tail of message */
    while (dwChars > 0) {
        if (wszMsgBuff[dwChars - 1] <= ' ') {
            dwChars--;
        } else {
            break;
        }
    }

    /* Ensure null-terminated string */
    wszMsgBuff[dwChars] = '\0';

    SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s! Error Code: %s " HRESULT_FMT, msg, wszMsgBuff, res);
}

/* Debug Naming */

static void D3D12_INTERNAL_SetResourceName(
    D3D12Renderer *renderer,
    ID3D12Resource *resource,
    const char *text)
{
    if (renderer->debugMode) {
        ID3D12DeviceChild_SetPrivateData(
            resource,
            D3D_GUID(D3D_IID_D3DDebugObjectName),
            (UINT)SDL_strlen(text),
            text);
    }
}

/* Release / Cleanup */

/* TODO: call this when releasing resources */
static void D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
    D3D12Renderer *renderer,
    D3D12CPUDescriptor *cpuDescriptor)
{
    D3D12DescriptorHeap *heap = cpuDescriptor->heap;

    if (heap != NULL) {
        SDL_LockMutex(renderer->stagingDescriptorHeapLock);
        heap->inactiveDescriptorIndices[heap->inactiveDescriptorCount] = cpuDescriptor->cpuHandleIndex;
        heap->inactiveDescriptorCount += 1;
        SDL_UnlockMutex(renderer->stagingDescriptorHeapLock);
    }

    cpuDescriptor->heap = NULL;
    cpuDescriptor->cpuHandle.ptr = 0;
    cpuDescriptor->cpuHandleIndex = SDL_MAX_UINT32;
}

static void D3D12_INTERNAL_DestroyBuffer(
    D3D12Renderer *renderer,
    D3D12Buffer *buffer)
{
    if (!buffer) {
        return;
    }

    if (buffer->mapPointer != NULL) {
        ID3D12Resource_Unmap(
            buffer->handle,
            0,
            NULL);
    }
    D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
        renderer,
        &buffer->srvDescriptor);
    D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
        renderer,
        &buffer->uavDescriptor);
    D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
        renderer,
        &buffer->cbvDescriptor);

    if (buffer->handle) {
        ID3D12Resource_Release(buffer->handle);
    }
    SDL_free(buffer);
}

static void D3D12_INTERNAL_ReleaseBuffer(
    D3D12Renderer *renderer,
    D3D12Buffer *buffer)
{
    SDL_LockMutex(renderer->disposeLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->buffersToDestroy,
        D3D12Buffer *,
        renderer->buffersToDestroyCount + 1,
        renderer->buffersToDestroyCapacity,
        renderer->buffersToDestroyCapacity * 2)

    renderer->buffersToDestroy[renderer->buffersToDestroyCount] = buffer;
    renderer->buffersToDestroyCount += 1;

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_ReleaseBufferContainer(
    D3D12Renderer *renderer,
    D3D12BufferContainer *container)
{
    SDL_LockMutex(renderer->disposeLock);

    for (Uint32 i = 0; i < container->bufferCount; i += 1) {
        D3D12_INTERNAL_ReleaseBuffer(
            renderer,
            container->buffers[i]);
    }

    /* Containers are just client handles, so we can free immediately */
    if (container->debugName) {
        SDL_free(container->debugName);
    }
    SDL_free(container->buffers);
    SDL_free(container);

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_DestroyTexture(
    D3D12Renderer *renderer,
    D3D12Texture *texture)
{
    if (!texture) {
        return;
    }
    for (Uint32 i = 0; i < texture->subresourceCount; i += 1) {
        D3D12TextureSubresource *subresource = &texture->subresources[i];
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &subresource->rtvHandle);
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &subresource->dsvHandle);
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &subresource->srvHandle);
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &subresource->uavHandle);
    }
    SDL_free(texture->subresources);

    D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
        renderer,
        &texture->srvHandle);

    if (texture->resource) {
        ID3D12Resource_Release(texture->resource);
    }

    SDL_free(texture);
}

static void D3D12_INTERNAL_ReleaseTexture(
    D3D12Renderer *renderer,
    D3D12Texture *texture)
{
    SDL_LockMutex(renderer->disposeLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->texturesToDestroy,
        D3D12Texture *,
        renderer->texturesToDestroyCount + 1,
        renderer->texturesToDestroyCapacity,
        renderer->texturesToDestroyCapacity * 2)

    renderer->texturesToDestroy[renderer->texturesToDestroyCount] = texture;
    renderer->texturesToDestroyCount += 1;

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_ReleaseTextureContainer(
    D3D12Renderer *renderer,
    D3D12TextureContainer *container)
{
    SDL_LockMutex(renderer->disposeLock);

    for (Uint32 i = 0; i < container->textureCount; i += 1) {
        D3D12_INTERNAL_ReleaseTexture(
            renderer,
            container->textures[i]);
    }

    /* Containers are just client handles, so we can destroy immediately */
    if (container->debugName) {
        SDL_free(container->debugName);
    }
    SDL_free(container->textures);
    SDL_free(container);

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_DestroySampler(
    D3D12Renderer *renderer,
    D3D12Sampler *sampler)
{
    D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
        renderer,
        &sampler->handle);

    SDL_free(sampler);
}

static void D3D12_INTERNAL_DestroyGraphicsRootSignature(
    D3D12GraphicsRootSignature *rootSignature)
{
    if (!rootSignature) {
        return;
    }
    if (rootSignature->handle) {
        ID3D12RootSignature_Release(rootSignature->handle);
    }
    SDL_free(rootSignature);
}

static void D3D12_INTERNAL_DestroyGraphicsPipeline(
    D3D12GraphicsPipeline *graphicsPipeline)
{
    if (graphicsPipeline->pipelineState) {
        ID3D12PipelineState_Release(graphicsPipeline->pipelineState);
    }
    D3D12_INTERNAL_DestroyGraphicsRootSignature(graphicsPipeline->rootSignature);
    SDL_free(graphicsPipeline);
}

static void D3D12_INTERNAL_DestroyComputeRootSignature(
    D3D12ComputeRootSignature *rootSignature)
{
    if (!rootSignature) {
        return;
    }
    if (rootSignature->handle) {
        ID3D12RootSignature_Release(rootSignature->handle);
    }
    SDL_free(rootSignature);
}

static void D3D12_INTERNAL_DestroyComputePipeline(
    D3D12ComputePipeline *computePipeline)
{
    if (computePipeline->pipelineState) {
        ID3D12PipelineState_Release(computePipeline->pipelineState);
    }
    D3D12_INTERNAL_DestroyComputeRootSignature(computePipeline->rootSignature);
    SDL_free(computePipeline);
}

static void D3D12_INTERNAL_ReleaseFenceToPool(
    D3D12Renderer *renderer,
    D3D12Fence *fence)
{
    SDL_LockMutex(renderer->fenceLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->availableFences,
        D3D12Fence *,
        renderer->availableFenceCount + 1,
        renderer->availableFenceCapacity,
        renderer->availableFenceCapacity * 2);

    renderer->availableFences[renderer->availableFenceCount] = fence;
    renderer->availableFenceCount += 1;

    SDL_UnlockMutex(renderer->fenceLock);
}

static void D3D12_ReleaseFence(
    SDL_GpuRenderer *driverData,
    SDL_GpuFence *fence)
{
    D3D12Fence *d3d12Fence = (D3D12Fence *)fence;

    if (SDL_AtomicDecRef(&d3d12Fence->referenceCount)) {
        D3D12_INTERNAL_ReleaseFenceToPool(
            (D3D12Renderer *)driverData,
            d3d12Fence);
    }
}

static SDL_bool D3D12_QueryFence(
    SDL_GpuRenderer *driverData,
    SDL_GpuFence *fence)
{
    D3D12Fence *d3d12Fence = (D3D12Fence *)fence;
    return ID3D12Fence_GetCompletedValue(d3d12Fence->handle) == D3D12_FENCE_SIGNAL_VALUE;
}

static void D3D12_INTERNAL_DestroyDescriptorHeap(D3D12DescriptorHeap *descriptorHeap)
{
    if (!descriptorHeap) {
        return;
    }
    SDL_free(descriptorHeap->inactiveDescriptorIndices);
    if (descriptorHeap->handle) {
        ID3D12DescriptorHeap_Release(descriptorHeap->handle);
    }
    SDL_free(descriptorHeap);
}

static void D3D12_INTERNAL_DestroyCommandBuffer(D3D12CommandBuffer *commandBuffer)
{
    if (!commandBuffer) {
        return;
    }
    if (commandBuffer->graphicsCommandList) {
        ID3D12GraphicsCommandList_Release(commandBuffer->graphicsCommandList);
    }
    if (commandBuffer->commandAllocator) {
        ID3D12CommandAllocator_Release(commandBuffer->commandAllocator);
    }
    SDL_free(commandBuffer->presentDatas);
    SDL_free(commandBuffer->usedTextureSubresources);
    SDL_free(commandBuffer->usedBuffers);
    SDL_free(commandBuffer->usedSamplers);
    SDL_free(commandBuffer->usedGraphicsPipelines);
    SDL_free(commandBuffer->usedComputePipelines);
    SDL_free(commandBuffer->usedUniformBuffers);
    SDL_free(commandBuffer);
}

static void D3D12_INTERNAL_DestroyFence(D3D12Fence *fence)
{
    if (!fence) {
        return;
    }
    if (fence->handle) {
        ID3D12Fence_Release(fence->handle);
    }
    if (fence->event) {
        CloseHandle(fence->event);
    }
    SDL_free(fence);
}

/* FIXME: just move this into DestroyDevice */
static void D3D12_INTERNAL_DestroyRenderer(D3D12Renderer *renderer)
{
    if (!renderer) {
        return;
    }

    /* Release blit pipeline structures */
    D3D12_INTERNAL_ReleaseBlitPipelines(renderer);

    /* Flush any remaining GPU work... */
    D3D12_Wait((SDL_GpuRenderer *)renderer);

    /* Release window data */
    for (Sint32 i = renderer->claimedWindowCount - 1; i >= 0; i -= 1) {
        D3D12_UnclaimWindow((SDL_GpuRenderer *)renderer, renderer->claimedWindows[i]->window);
    }
    SDL_free(renderer->claimedWindows);

    /* Release uniform buffers */
    for (Uint32 i = 0; i < renderer->uniformBufferPoolCount; i += 1) {
        D3D12_INTERNAL_DestroyBuffer(
            renderer,
            renderer->uniformBufferPool[i]->buffer);
        SDL_free(renderer->uniformBufferPool[i]);
    }

    /* Clean up descriptor heaps */
    for (Uint32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i += 1) {
        if (renderer->stagingDescriptorHeaps[i]) {
            D3D12_INTERNAL_DestroyDescriptorHeap(renderer->stagingDescriptorHeaps[i]);
            renderer->stagingDescriptorHeaps[i] = NULL;
        }
    }

    for (Uint32 i = 0; i < 2; i += 1) {
        if (renderer->descriptorHeapPools[i].heaps) {
            for (Uint32 j = 0; j < renderer->descriptorHeapPools[i].count; j += 1) {
                if (renderer->descriptorHeapPools[i].heaps[j]) {
                    D3D12_INTERNAL_DestroyDescriptorHeap(renderer->descriptorHeapPools[i].heaps[j]);
                    renderer->descriptorHeapPools[i].heaps[j] = NULL;
                }
            }
            SDL_free(renderer->descriptorHeapPools[i].heaps);
        }
        if (renderer->descriptorHeapPools[i].lock) {
            SDL_DestroyMutex(renderer->descriptorHeapPools[i].lock);
            renderer->descriptorHeapPools[i].lock = NULL;
        }
    }

    /* Release command buffers */
    for (Uint32 i = 0; i < renderer->availableCommandBufferCount; i += 1) {
        if (renderer->availableCommandBuffers[i]) {
            D3D12_INTERNAL_DestroyCommandBuffer(renderer->availableCommandBuffers[i]);
            renderer->availableCommandBuffers[i] = NULL;
        }
    }

    /* Release fences */
    for (Uint32 i = 0; i < renderer->availableFenceCount; i += 1) {
        if (renderer->availableFences[i]) {
            D3D12_INTERNAL_DestroyFence(renderer->availableFences[i]);
            renderer->availableFences[i] = NULL;
        }
    }

    /* Clean up allocations */
    SDL_free(renderer->availableCommandBuffers);
    SDL_free(renderer->submittedCommandBuffers);
    SDL_free(renderer->availableFences);
    SDL_free(renderer->uniformBufferPool);

    /* Tear down D3D12 objects */
    if (renderer->indirectDrawCommandSignature) {
        ID3D12CommandSignature_Release(renderer->indirectDrawCommandSignature);
        renderer->indirectDrawCommandSignature = NULL;
    }
    if (renderer->indirectIndexedDrawCommandSignature) {
        ID3D12CommandSignature_Release(renderer->indirectIndexedDrawCommandSignature);
        renderer->indirectIndexedDrawCommandSignature = NULL;
    }
    if (renderer->indirectDispatchCommandSignature) {
        ID3D12CommandSignature_Release(renderer->indirectDispatchCommandSignature);
        renderer->indirectDispatchCommandSignature = NULL;
    }
    if (renderer->commandQueue) {
        ID3D12CommandQueue_Release(renderer->commandQueue);
        renderer->commandQueue = NULL;
    }
    if (renderer->device) {
        ID3D12Device_Release(renderer->device);
        renderer->device = NULL;
    }
    if (renderer->adapter) {
        IDXGIAdapter1_Release(renderer->adapter);
        renderer->adapter = NULL;
    }
    if (renderer->factory) {
        IDXGIFactory4_Release(renderer->factory);
        renderer->factory = NULL;
    }
    if (renderer->dxgiDebug) {
        IDXGIDebug_ReportLiveObjects(
            renderer->dxgiDebug,
            D3D_IID_DXGI_DEBUG_ALL,
            (DXGI_DEBUG_RLO_FLAGS)(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_DETAIL));
        IDXGIDebug_Release(renderer->dxgiDebug);
        renderer->dxgiDebug = NULL;
    }
    if (renderer->d3d12_dll) {
        SDL_UnloadObject(renderer->d3d12_dll);
        renderer->d3d12_dll = NULL;
    }
    if (renderer->dxgi_dll) {
        SDL_UnloadObject(renderer->dxgi_dll);
        renderer->dxgi_dll = NULL;
    }
    if (renderer->dxgidebug_dll) {
        SDL_UnloadObject(renderer->dxgidebug_dll);
        renderer->dxgidebug_dll = NULL;
    }
    renderer->D3D12SerializeRootSignature_func = NULL;

    if (renderer->iconv) {
        SDL_iconv_close(renderer->iconv);
    }

    SDL_DestroyMutex(renderer->stagingDescriptorHeapLock);
    SDL_DestroyMutex(renderer->acquireCommandBufferLock);
    SDL_DestroyMutex(renderer->acquireUniformBufferLock);
    SDL_DestroyMutex(renderer->submitLock);
    SDL_DestroyMutex(renderer->windowLock);
    SDL_DestroyMutex(renderer->fenceLock);
    SDL_DestroyMutex(renderer->disposeLock);
    SDL_free(renderer);
}

static void D3D12_DestroyDevice(SDL_GpuDevice *device)
{
    D3D12Renderer *renderer = (D3D12Renderer *)device->driverData;
    D3D12_INTERNAL_DestroyRenderer(renderer);
    SDL_free(device);
}

/* Barriers */

static inline Uint32 D3D12_INTERNAL_CalcSubresource(
    Uint32 mipLevel,
    Uint32 layer,
    Uint32 numLevels)
{
    return mipLevel + (layer * numLevels);
}

static void D3D12_INTERNAL_ResourceBarrier(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES sourceState,
    D3D12_RESOURCE_STATES destinationState,
    ID3D12Resource *resource,
    Uint32 subresourceIndex,
    SDL_bool needsUavBarrier)
{
    D3D12_RESOURCE_BARRIER barrierDesc[2];
    Uint32 numBarriers = 0;

    /* No transition barrier is needed if the state is not changing. */
    if (sourceState != destinationState) {
        barrierDesc[numBarriers].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[numBarriers].Flags = (D3D12_RESOURCE_BARRIER_FLAGS)0;
        barrierDesc[numBarriers].Transition.StateBefore = sourceState;
        barrierDesc[numBarriers].Transition.StateAfter = destinationState;
        barrierDesc[numBarriers].Transition.pResource = resource;
        barrierDesc[numBarriers].Transition.Subresource = subresourceIndex;

        numBarriers += 1;
    }

    if (needsUavBarrier) {
        barrierDesc[numBarriers].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrierDesc[numBarriers].Flags = (D3D12_RESOURCE_BARRIER_FLAGS)0;
        barrierDesc[numBarriers].UAV.pResource = resource;

        numBarriers += 1;
    }

    if (numBarriers > 0) {
        ID3D12GraphicsCommandList_ResourceBarrier(
            commandBuffer->graphicsCommandList,
            numBarriers,
            barrierDesc);
    }
}

static void D3D12_INTERNAL_TextureSubresourceBarrier(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES sourceState,
    D3D12_RESOURCE_STATES destinationState,
    D3D12TextureSubresource *textureSubresource)
{
    D3D12_INTERNAL_ResourceBarrier(
        commandBuffer,
        sourceState,
        destinationState,
        textureSubresource->parent->resource,
        textureSubresource->index,
        textureSubresource->parent->container->header.info.usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT);
}

static D3D12_RESOURCE_STATES D3D12_INTERNAL_DefaultTextureResourceState(SDL_GpuTextureUsageFlags usageFlags)
{
    /* NOTE: order matters here! */

    if (usageFlags & SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT) {
        return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    } else if (usageFlags & SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT) {
        return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    } else if (usageFlags & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT) {
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    } else if (usageFlags & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT) {
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    } else if (usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT) {
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    } else if (usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Texture has no default usage mode!");
        return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    }
}

static void D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES destinationUsageMode,
    D3D12TextureSubresource *textureSubresource)
{
    D3D12_INTERNAL_TextureSubresourceBarrier(
        commandBuffer,
        D3D12_INTERNAL_DefaultTextureResourceState(textureSubresource->parent->container->header.info.usageFlags),
        destinationUsageMode,
        textureSubresource);
}

static void D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES sourceUsageMode,
    D3D12TextureSubresource *textureSubresource)
{
    D3D12_INTERNAL_TextureSubresourceBarrier(
        commandBuffer,
        sourceUsageMode,
        D3D12_INTERNAL_DefaultTextureResourceState(textureSubresource->parent->container->header.info.usageFlags),
        textureSubresource);
}

static D3D12_RESOURCE_STATES D3D12_INTERNAL_DefaultBufferResourceState(
    D3D12Buffer *buffer)
{
    if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_VERTEX_BIT) {
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    } else if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_INDEX_BIT) {
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    } else if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_INDIRECT_BIT) {
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    } else if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT) {
        return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    } else if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ_BIT) {
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    } else if (buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Buffer has no default usage mode!");
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    }
}

static void D3D12_INTERNAL_BufferBarrier(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES sourceState,
    D3D12_RESOURCE_STATES destinationState,
    D3D12Buffer *buffer)
{
    D3D12_INTERNAL_ResourceBarrier(
        commandBuffer,
        buffer->transitioned ? sourceState : D3D12_RESOURCE_STATE_COMMON,
        destinationState,
        buffer->handle,
        0,
        buffer->container->usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT);

    buffer->transitioned = SDL_TRUE;
}

static void D3D12_INTERNAL_BufferTransitionFromDefaultUsage(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES destinationState,
    D3D12Buffer *buffer)
{
    D3D12_INTERNAL_BufferBarrier(
        commandBuffer,
        D3D12_INTERNAL_DefaultBufferResourceState(buffer),
        destinationState,
        buffer);
}

static void D3D12_INTERNAL_BufferTransitionToDefaultUsage(
    D3D12CommandBuffer *commandBuffer,
    D3D12_RESOURCE_STATES sourceState,
    D3D12Buffer *buffer)
{
    D3D12_INTERNAL_BufferBarrier(
        commandBuffer,
        sourceState,
        D3D12_INTERNAL_DefaultBufferResourceState(buffer),
        buffer);
}

/* Resource tracking */

#define TRACK_RESOURCE(resource, type, array, count, capacity) \
    Uint32 i;                                                  \
                                                               \
    for (i = 0; i < commandBuffer->count; i += 1) {            \
        if (commandBuffer->array[i] == resource) {             \
            return;                                            \
        }                                                      \
    }                                                          \
                                                               \
    if (commandBuffer->count == commandBuffer->capacity) {     \
        commandBuffer->capacity += 1;                          \
        commandBuffer->array = (type *)SDL_realloc(            \
            commandBuffer->array,                              \
            commandBuffer->capacity * sizeof(type));           \
    }                                                          \
    commandBuffer->array[commandBuffer->count] = resource;     \
    commandBuffer->count += 1;                                 \
    SDL_AtomicIncRef(&resource->referenceCount);

static void D3D12_INTERNAL_TrackTextureSubresource(
    D3D12CommandBuffer *commandBuffer,
    D3D12TextureSubresource *textureSubresource)
{
    TRACK_RESOURCE(
        textureSubresource,
        D3D12TextureSubresource *,
        usedTextureSubresources,
        usedTextureSubresourceCount,
        usedTextureSubresourceCapacity)
}

static void D3D12_INTERNAL_TrackBuffer(
    D3D12CommandBuffer *commandBuffer,
    D3D12Buffer *buffer)
{
    TRACK_RESOURCE(
        buffer,
        D3D12Buffer *,
        usedBuffers,
        usedBufferCount,
        usedBufferCapacity)
}

static void D3D12_INTERNAL_TrackSampler(
    D3D12CommandBuffer *commandBuffer,
    D3D12Sampler *sampler)
{
    TRACK_RESOURCE(
        sampler,
        D3D12Sampler *,
        usedSamplers,
        usedSamplerCount,
        usedSamplerCapacity)
}

static void D3D12_INTERNAL_TrackGraphicsPipeline(
    D3D12CommandBuffer *commandBuffer,
    D3D12GraphicsPipeline *graphicsPipeline)
{
    TRACK_RESOURCE(
        graphicsPipeline,
        D3D12GraphicsPipeline *,
        usedGraphicsPipelines,
        usedGraphicsPipelineCount,
        usedGraphicsPipelineCapacity)
}

static void D3D12_INTERNAL_TrackComputePipeline(
    D3D12CommandBuffer *commandBuffer,
    D3D12ComputePipeline *computePipeline)
{
    TRACK_RESOURCE(
        computePipeline,
        D3D12ComputePipeline *,
        usedComputePipelines,
        usedComputePipelineCount,
        usedComputePipelineCapacity)
}

#undef TRACK_RESOURCE

/* State Creation */

static D3D12DescriptorHeap *D3D12_INTERNAL_CreateDescriptorHeap(
    D3D12Renderer *renderer,
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    Uint32 descriptorCount,
    SDL_bool staging)
{
    D3D12DescriptorHeap *heap;
    ID3D12DescriptorHeap *handle;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    HRESULT res;

    heap = (D3D12DescriptorHeap *)SDL_calloc(1, sizeof(D3D12DescriptorHeap));
    if (!heap) {
        return NULL;
    }

    heap->currentDescriptorIndex = 0;
    heap->inactiveDescriptorCount = 0;
    heap->inactiveDescriptorIndices = NULL;

    if (staging) {
        heap->inactiveDescriptorIndices = (Uint32 *)SDL_calloc(descriptorCount, sizeof(Uint32));
        if (!heap->inactiveDescriptorIndices) {
            D3D12_INTERNAL_DestroyDescriptorHeap(heap);
            return NULL;
        }
    }

    heapDesc.NumDescriptors = descriptorCount;
    heapDesc.Type = type;
    heapDesc.Flags = staging ? D3D12_DESCRIPTOR_HEAP_FLAG_NONE : D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask = 0;

    res = ID3D12Device_CreateDescriptorHeap(
        renderer->device,
        &heapDesc,
        D3D_GUID(D3D_IID_ID3D12DescriptorHeap),
        (void **)&handle);

    if (FAILED(res)) {
        D3D12_INTERNAL_LogError(renderer->device, "Failed to create descriptor heap!", res);
        D3D12_INTERNAL_DestroyDescriptorHeap(heap);
        return NULL;
    }

    heap->handle = handle;
    heap->heapType = type;
    heap->maxDescriptors = descriptorCount;
    heap->staging = staging;
    heap->descriptorSize = ID3D12Device_GetDescriptorHandleIncrementSize(renderer->device, type);
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(handle, &heap->descriptorHeapCPUStart);
    if (!staging) {
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(handle, &heap->descriptorHeapGPUStart);
    }

    return heap;
}

static D3D12DescriptorHeap *D3D12_INTERNAL_AcquireDescriptorHeapFromPool(
    D3D12CommandBuffer *commandBuffer,
    D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType)
{
    D3D12DescriptorHeap *result;
    D3D12Renderer *renderer = commandBuffer->renderer;
    D3D12DescriptorHeapPool *pool = &renderer->descriptorHeapPools[descriptorHeapType];

    SDL_LockMutex(pool->lock);
    if (pool->count > 0) {
        result = pool->heaps[pool->count - 1];
        pool->count -= 1;
    } else {
        result = D3D12_INTERNAL_CreateDescriptorHeap(
            renderer,
            descriptorHeapType,
            descriptorHeapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? VIEW_GPU_DESCRIPTOR_COUNT : SAMPLER_GPU_DESCRIPTOR_COUNT,
            SDL_FALSE);
    }
    SDL_UnlockMutex(pool->lock);

    return result;
}

static void D3D12_INTERNAL_ReturnDescriptorHeapToPool(
    D3D12Renderer *renderer,
    D3D12DescriptorHeap *heap)
{
    D3D12DescriptorHeapPool *pool = &renderer->descriptorHeapPools[heap->heapType];

    heap->currentDescriptorIndex = 0;

    SDL_LockMutex(pool->lock);
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->heaps = (D3D12DescriptorHeap **)SDL_realloc(
            pool->heaps,
            pool->capacity * sizeof(D3D12DescriptorHeap *));
    }

    pool->heaps[pool->count] = heap;
    pool->count += 1;
    SDL_UnlockMutex(pool->lock);
}

/*
 * The root signature lets us define "root parameters" which are essentially bind points for resources.
 * These let us define the register ranges as well as the register "space".
 * The register space is akin to the descriptor set index in Vulkan, which allows us to group resources
 * by stage so that the registers from the vertex and fragment shaders don't clobber each other.
 *
 * Most of our root parameters are implemented as "descriptor tables" so we can
 * copy and then point to contiguous descriptor regions.
 * Uniform buffers are the exception - these have to be implemented as raw "root descriptors" so
 * that we can dynamically update the address that the constant buffer view points to.
 *
 * The root signature has a maximum size of 64 DWORDs.
 * A descriptor table uses 1 DWORD.
 * A root descriptor uses 2 DWORDS.
 * This means our biggest root signature uses 24 DWORDs total, well under the limit.
 *
 * The root parameter indices are created dynamically and stored in the D3D12GraphicsRootSignature struct.
 */
static D3D12GraphicsRootSignature *D3D12_INTERNAL_CreateGraphicsRootSignature(
    D3D12Renderer *renderer,
    D3D12Shader *vertexShader,
    D3D12Shader *fragmentShader)
{
    /* FIXME: I think the max can be smaller... */
    D3D12_ROOT_PARAMETER rootParameters[MAX_ROOT_SIGNATURE_PARAMETERS];
    D3D12_DESCRIPTOR_RANGE descriptorRanges[MAX_ROOT_SIGNATURE_PARAMETERS];
    Uint32 parameterCount = 0;
    Uint32 rangeCount = 0;
    D3D12_DESCRIPTOR_RANGE descriptorRange;
    D3D12_ROOT_PARAMETER rootParameter;
    D3D12GraphicsRootSignature *d3d12GraphicsRootSignature =
        (D3D12GraphicsRootSignature *)SDL_calloc(1, sizeof(D3D12GraphicsRootSignature));
    if (!d3d12GraphicsRootSignature) {
        return NULL;
    }

    SDL_zeroa(rootParameters);
    SDL_zeroa(descriptorRanges);
    SDL_zero(rootParameter);

    d3d12GraphicsRootSignature->vertexSamplerRootIndex = -1;
    d3d12GraphicsRootSignature->vertexSamplerTextureRootIndex = -1;
    d3d12GraphicsRootSignature->vertexStorageTextureRootIndex = -1;
    d3d12GraphicsRootSignature->vertexStorageBufferRootIndex = -1;

    d3d12GraphicsRootSignature->fragmentSamplerRootIndex = -1;
    d3d12GraphicsRootSignature->fragmentSamplerTextureRootIndex = -1;
    d3d12GraphicsRootSignature->fragmentStorageTextureRootIndex = -1;
    d3d12GraphicsRootSignature->fragmentStorageBufferRootIndex = -1;

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        d3d12GraphicsRootSignature->vertexUniformBufferRootIndex[i] = -1;
        d3d12GraphicsRootSignature->fragmentUniformBufferRootIndex[i] = -1;
    }

    if (vertexShader->samplerCount > 0) {
        /* Vertex Samplers */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        descriptorRange.NumDescriptors = vertexShader->samplerCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->vertexSamplerRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;

        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = vertexShader->samplerCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->vertexSamplerTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (vertexShader->storageTextureCount) {
        /* Vertex storage textures */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = vertexShader->storageTextureCount;
        descriptorRange.BaseShaderRegister = vertexShader->samplerCount;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->vertexStorageTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (vertexShader->storageBufferCount) {

        /* Vertex storage buffers */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = vertexShader->storageBufferCount;
        descriptorRange.BaseShaderRegister = vertexShader->samplerCount + vertexShader->storageTextureCount;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->vertexStorageBufferRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    /* Vertex Uniforms */
    for (Uint32 i = 0; i < vertexShader->uniformBufferCount; i += 1) {
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.ShaderRegister = i;
        rootParameter.Descriptor.RegisterSpace = 1;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->vertexUniformBufferRootIndex[i] = parameterCount;
        parameterCount += 1;
    }

    if (fragmentShader->samplerCount) {
        /* Fragment Samplers */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        descriptorRange.NumDescriptors = fragmentShader->samplerCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 2;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->fragmentSamplerRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;

        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = fragmentShader->samplerCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 2;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->fragmentSamplerTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (fragmentShader->storageTextureCount) {
        /* Fragment Storage Textures */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = fragmentShader->storageTextureCount;
        descriptorRange.BaseShaderRegister = fragmentShader->samplerCount;
        descriptorRange.RegisterSpace = 2;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->fragmentStorageTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (fragmentShader->storageBufferCount) {
        /* Fragment Storage Buffers */
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = fragmentShader->storageBufferCount;
        descriptorRange.BaseShaderRegister = fragmentShader->samplerCount + fragmentShader->storageTextureCount;
        descriptorRange.RegisterSpace = 2;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->fragmentStorageBufferRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    /* Fragment Uniforms */
    for (Uint32 i = 0; i < fragmentShader->uniformBufferCount; i += 1) {
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.ShaderRegister = i;
        rootParameter.Descriptor.RegisterSpace = 3;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rootParameters[parameterCount] = rootParameter;
        d3d12GraphicsRootSignature->fragmentUniformBufferRootIndex[i] = parameterCount;
        parameterCount += 1;
    }

    /* FIXME: shouldn't have to assert here */
    SDL_assert(parameterCount <= MAX_ROOT_SIGNATURE_PARAMETERS);
    SDL_assert(rangeCount <= MAX_ROOT_SIGNATURE_PARAMETERS);

    // Create the root signature description
    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.NumParameters = parameterCount;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = NULL;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // Serialize the root signature
    ID3DBlob *serializedRootSignature;
    ID3DBlob *errorBlob;
    HRESULT res = renderer->D3D12SerializeRootSignature_func(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSignature, &errorBlob);

    if (FAILED(res)) {
        if (errorBlob) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to serialize RootSignature: %s", (const char *)ID3D10Blob_GetBufferPointer(errorBlob));
            ID3D10Blob_Release(errorBlob);
        }
        D3D12_INTERNAL_DestroyGraphicsRootSignature(d3d12GraphicsRootSignature);
        return NULL;
    }

    // Create the root signature
    ID3D12RootSignature *rootSignature;

    res = ID3D12Device_CreateRootSignature(
        renderer->device,
        0,
        ID3D10Blob_GetBufferPointer(serializedRootSignature),
        ID3D10Blob_GetBufferSize(serializedRootSignature),
        D3D_GUID(D3D_IID_ID3D12RootSignature),
        (void **)&rootSignature);

    if (FAILED(res)) {
        if (errorBlob) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create RootSignature");
            ID3D10Blob_Release(errorBlob);
        }
        D3D12_INTERNAL_DestroyGraphicsRootSignature(d3d12GraphicsRootSignature);
        return NULL;
    }

    d3d12GraphicsRootSignature->handle = rootSignature;
    return d3d12GraphicsRootSignature;
}

static SDL_bool D3D12_INTERNAL_CreateShaderBytecode(
    D3D12Renderer *renderer,
    Uint32 stage,
    SDL_GpuShaderFormat format,
    const Uint8 *code,
    size_t codeSize,
    const char *entryPointName,
    void **pBytecode,
    size_t *pBytecodeSize)
{
    /* TODO: accept DXIL */
    if (format != SDL_GPU_SHADERFORMAT_DXBC) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Incompatible shader format for D3D12");
        return SDL_FALSE;
    }

    if (pBytecode != NULL) {
        *pBytecode = SDL_malloc(codeSize);
        if (!*pBytecode) {
            return SDL_FALSE;
        }
        SDL_memcpy(*pBytecode, code, codeSize);
        *pBytecodeSize = codeSize;
    }

    return SDL_TRUE;
}

static D3D12ComputeRootSignature *D3D12_INTERNAL_CreateComputeRootSignature(
    D3D12Renderer *renderer,
    SDL_GpuComputePipelineCreateInfo *createInfo)
{
    /* FIXME: I think the max can be smaller... */
    D3D12_ROOT_PARAMETER rootParameters[MAX_ROOT_SIGNATURE_PARAMETERS];
    D3D12_DESCRIPTOR_RANGE descriptorRanges[MAX_ROOT_SIGNATURE_PARAMETERS];
    Uint32 parameterCount = 0;
    Uint32 rangeCount = 0;
    D3D12_DESCRIPTOR_RANGE descriptorRange;
    D3D12_ROOT_PARAMETER rootParameter;
    D3D12ComputeRootSignature *d3d12ComputeRootSignature =
        (D3D12ComputeRootSignature *)SDL_calloc(1, sizeof(D3D12ComputeRootSignature));
    if (!d3d12ComputeRootSignature) {
        return NULL;
    }

    SDL_zeroa(rootParameters);
    SDL_zeroa(descriptorRanges);
    SDL_zero(rootParameter);

    d3d12ComputeRootSignature->readOnlyStorageTextureRootIndex = -1;
    d3d12ComputeRootSignature->readOnlyStorageBufferRootIndex = -1;
    d3d12ComputeRootSignature->readWriteStorageTextureRootIndex = -1;
    d3d12ComputeRootSignature->readWriteStorageBufferRootIndex = -1;

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        d3d12ComputeRootSignature->uniformBufferRootIndex[i] = -1;
    }

    if (createInfo->readOnlyStorageTextureCount) {
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = createInfo->readOnlyStorageTextureCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; /* ALL is used for compute */
        rootParameters[parameterCount] = rootParameter;
        d3d12ComputeRootSignature->readOnlyStorageTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (createInfo->readOnlyStorageBufferCount) {
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        descriptorRange.NumDescriptors = createInfo->readOnlyStorageBufferCount;
        descriptorRange.BaseShaderRegister = createInfo->readOnlyStorageTextureCount;
        descriptorRange.RegisterSpace = 0;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; /* ALL is used for compute */
        rootParameters[parameterCount] = rootParameter;
        d3d12ComputeRootSignature->readOnlyStorageBufferRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (createInfo->readWriteStorageTextureCount) {
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorRange.NumDescriptors = createInfo->readWriteStorageTextureCount;
        descriptorRange.BaseShaderRegister = 0;
        descriptorRange.RegisterSpace = 1;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; /* ALL is used for compute */
        rootParameters[parameterCount] = rootParameter;
        d3d12ComputeRootSignature->readWriteStorageTextureRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    if (createInfo->readWriteStorageBufferCount) {
        descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        descriptorRange.NumDescriptors = createInfo->readWriteStorageBufferCount;
        descriptorRange.BaseShaderRegister = createInfo->readWriteStorageTextureCount;
        descriptorRange.RegisterSpace = 1;
        descriptorRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        descriptorRanges[rangeCount] = descriptorRange;

        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = 1;
        rootParameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[rangeCount];
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; /* ALL is used for compute */
        rootParameters[parameterCount] = rootParameter;
        d3d12ComputeRootSignature->readWriteStorageBufferRootIndex = parameterCount;
        rangeCount += 1;
        parameterCount += 1;
    }

    for (int i = 0; i < createInfo->uniformBufferCount; i += 1) {
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameter.Descriptor.ShaderRegister = i;
        rootParameter.Descriptor.RegisterSpace = 2;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; /* ALL is used for compute */
        rootParameters[parameterCount] = rootParameter;
        d3d12ComputeRootSignature->uniformBufferRootIndex[i] = parameterCount;
        parameterCount += 1;
    }

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.NumParameters = parameterCount;
    rootSignatureDesc.pParameters = rootParameters;
    rootSignatureDesc.NumStaticSamplers = 0;
    rootSignatureDesc.pStaticSamplers = NULL;
    rootSignatureDesc.Flags = (D3D12_ROOT_SIGNATURE_FLAGS)0;

    ID3DBlob *serializedRootSignature;
    ID3DBlob *errorBlob;
    HRESULT res = renderer->D3D12SerializeRootSignature_func(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSignature,
        &errorBlob);

    if (FAILED(res)) {
        if (errorBlob) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to serialize RootSignature: %s", (const char *)ID3D10Blob_GetBufferPointer(errorBlob));
            ID3D10Blob_Release(errorBlob);
        }
        D3D12_INTERNAL_DestroyComputeRootSignature(d3d12ComputeRootSignature);
        return NULL;
    }

    ID3D12RootSignature *rootSignature;

    res = ID3D12Device_CreateRootSignature(
        renderer->device,
        0,
        ID3D10Blob_GetBufferPointer(serializedRootSignature),
        ID3D10Blob_GetBufferSize(serializedRootSignature),
        D3D_GUID(D3D_IID_ID3D12RootSignature),
        (void **)&rootSignature);

    if (FAILED(res)) {
        if (errorBlob) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create RootSignature");
            ID3D10Blob_Release(errorBlob);
        }
        D3D12_INTERNAL_DestroyComputeRootSignature(d3d12ComputeRootSignature);
        return NULL;
    }

    d3d12ComputeRootSignature->handle = rootSignature;
    return d3d12ComputeRootSignature;
}

static SDL_GpuComputePipeline *D3D12_CreateComputePipeline(
    SDL_GpuRenderer *driverData,
    SDL_GpuComputePipelineCreateInfo *pipelineCreateInfo)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    void *bytecode;
    size_t bytecodeSize;
    ID3D12PipelineState *pipelineState;

    if (!D3D12_INTERNAL_CreateShaderBytecode(
            renderer,
            SDL_GPU_SHADERSTAGE_COMPUTE,
            pipelineCreateInfo->format,
            pipelineCreateInfo->code,
            pipelineCreateInfo->codeSize,
            pipelineCreateInfo->entryPointName,
            &bytecode,
            &bytecodeSize)) {
        return NULL;
    }

    D3D12ComputeRootSignature *rootSignature = D3D12_INTERNAL_CreateComputeRootSignature(
        renderer,
        pipelineCreateInfo);

    if (rootSignature == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not create root signature!");
        SDL_free(bytecode);
        return NULL;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc;
    pipelineDesc.CS.pShaderBytecode = bytecode;
    pipelineDesc.CS.BytecodeLength = bytecodeSize;
    pipelineDesc.pRootSignature = rootSignature->handle;
    pipelineDesc.CachedPSO.CachedBlobSizeInBytes = 0;
    pipelineDesc.CachedPSO.pCachedBlob = NULL;
    pipelineDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    pipelineDesc.NodeMask = 0;

    HRESULT res = ID3D12Device_CreateComputePipelineState(
        renderer->device,
        &pipelineDesc,
        D3D_GUID(D3D_IID_ID3D12PipelineState),
        (void **)&pipelineState);

    if (FAILED(res)) {
        D3D12_INTERNAL_LogError(renderer->device, "Could not create compute pipeline state", res);
        SDL_free(bytecode);
        return NULL;
    }

    D3D12ComputePipeline *computePipeline =
        (D3D12ComputePipeline *)SDL_calloc(1, sizeof(D3D12ComputePipeline));

    if (!computePipeline) {
        ID3D12PipelineState_Release(pipelineState);
        SDL_free(bytecode);
        return NULL;
    }

    computePipeline->pipelineState = pipelineState;
    computePipeline->rootSignature = rootSignature;
    computePipeline->readOnlyStorageTextureCount = pipelineCreateInfo->readOnlyStorageTextureCount;
    computePipeline->readOnlyStorageBufferCount = pipelineCreateInfo->readOnlyStorageBufferCount;
    computePipeline->readWriteStorageTextureCount = pipelineCreateInfo->readWriteStorageTextureCount;
    computePipeline->readWriteStorageBufferCount = pipelineCreateInfo->readWriteStorageBufferCount;
    computePipeline->uniformBufferCount = pipelineCreateInfo->uniformBufferCount;
    SDL_AtomicSet(&computePipeline->referenceCount, 0);

    return (SDL_GpuComputePipeline *)computePipeline;
}

static SDL_bool D3D12_INTERNAL_ConvertRasterizerState(SDL_GpuRasterizerState rasterizerState, D3D12_RASTERIZER_DESC *desc)
{
    if (!desc) {
        return SDL_FALSE;
    }

    desc->FillMode = SDLToD3D12_FillMode[rasterizerState.fillMode];
    desc->CullMode = SDLToD3D12_CullMode[rasterizerState.cullMode];

    switch (rasterizerState.frontFace) {
    case SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE:
        desc->FrontCounterClockwise = TRUE;
        break;
    case SDL_GPU_FRONTFACE_CLOCKWISE:
        desc->FrontCounterClockwise = FALSE;
        break;
    default:
        return SDL_FALSE;
    }

    if (rasterizerState.depthBiasEnable) {
        desc->DepthBias = SDL_lroundf(rasterizerState.depthBiasConstantFactor);
        desc->DepthBiasClamp = rasterizerState.depthBiasClamp;
        desc->SlopeScaledDepthBias = rasterizerState.depthBiasSlopeFactor;
    } else {
        desc->DepthBias = 0;
        desc->DepthBiasClamp = 0.0f;
        desc->SlopeScaledDepthBias = 0.0f;
    }

    desc->DepthClipEnable = TRUE;
    desc->MultisampleEnable = FALSE;
    desc->AntialiasedLineEnable = FALSE;
    desc->ForcedSampleCount = 0;
    desc->ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    return SDL_TRUE;
}

static SDL_bool D3D12_INTERNAL_ConvertBlendState(SDL_GpuGraphicsPipelineCreateInfo *pipelineInfo, D3D12_BLEND_DESC *blendDesc)
{
    if (!blendDesc) {
        return SDL_FALSE;
    }

    SDL_zerop(blendDesc);
    blendDesc->AlphaToCoverageEnable = FALSE;
    blendDesc->IndependentBlendEnable = FALSE;

    for (int i = 0; i < MAX_COLOR_TARGET_BINDINGS; i += 1) {
        D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc;
        rtBlendDesc.BlendEnable = FALSE;
        rtBlendDesc.LogicOpEnable = FALSE;
        rtBlendDesc.SrcBlend = D3D12_BLEND_ONE;
        rtBlendDesc.DestBlend = D3D12_BLEND_ZERO;
        rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
        rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
        rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rtBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
        rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // If attachmentInfo has more blend states, you can set IndependentBlendEnable to TRUE and assign different blend states to each render target slot
        if (i < pipelineInfo->attachmentInfo.colorAttachmentCount) {

            SDL_GpuColorAttachmentBlendState sdlBlendState = pipelineInfo->attachmentInfo.colorAttachmentDescriptions[i].blendState;

            rtBlendDesc.BlendEnable = sdlBlendState.blendEnable;
            rtBlendDesc.SrcBlend = SDLToD3D12_BlendFactor[sdlBlendState.srcColorBlendFactor];
            rtBlendDesc.DestBlend = SDLToD3D12_BlendFactor[sdlBlendState.dstColorBlendFactor];
            rtBlendDesc.BlendOp = SDLToD3D12_BlendOp[sdlBlendState.colorBlendOp];
            rtBlendDesc.SrcBlendAlpha = SDLToD3D12_BlendFactorAlpha[sdlBlendState.srcAlphaBlendFactor];
            rtBlendDesc.DestBlendAlpha = SDLToD3D12_BlendFactorAlpha[sdlBlendState.dstAlphaBlendFactor];
            rtBlendDesc.BlendOpAlpha = SDLToD3D12_BlendOp[sdlBlendState.alphaBlendOp];
            SDL_assert(sdlBlendState.colorWriteMask <= SDL_MAX_UINT8);
            rtBlendDesc.RenderTargetWriteMask = (UINT8)sdlBlendState.colorWriteMask;

            if (i > 0) {
                blendDesc->IndependentBlendEnable = TRUE;
            }
        }

        blendDesc->RenderTarget[i] = rtBlendDesc;
    }

    return SDL_TRUE;
}

static SDL_bool D3D12_INTERNAL_ConvertDepthStencilState(SDL_GpuDepthStencilState depthStencilState, D3D12_DEPTH_STENCIL_DESC *desc)
{
    if (desc == NULL) {
        return SDL_FALSE;
    }

    desc->DepthEnable = depthStencilState.depthTestEnable == SDL_TRUE ? TRUE : FALSE;
    desc->DepthWriteMask = depthStencilState.depthWriteEnable == SDL_TRUE ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc->DepthFunc = SDLToD3D12_CompareOp[depthStencilState.compareOp];
    desc->StencilEnable = depthStencilState.stencilTestEnable == SDL_TRUE ? TRUE : FALSE;
    desc->StencilReadMask = (UINT8)depthStencilState.compareMask;
    desc->StencilWriteMask = (UINT8)depthStencilState.writeMask;

    desc->FrontFace.StencilFailOp = SDLToD3D12_StencilOp[depthStencilState.frontStencilState.failOp];
    desc->FrontFace.StencilDepthFailOp = SDLToD3D12_StencilOp[depthStencilState.frontStencilState.depthFailOp];
    desc->FrontFace.StencilPassOp = SDLToD3D12_StencilOp[depthStencilState.frontStencilState.passOp];
    desc->FrontFace.StencilFunc = SDLToD3D12_CompareOp[depthStencilState.frontStencilState.compareOp];

    desc->BackFace.StencilFailOp = SDLToD3D12_StencilOp[depthStencilState.backStencilState.failOp];
    desc->BackFace.StencilDepthFailOp = SDLToD3D12_StencilOp[depthStencilState.backStencilState.depthFailOp];
    desc->BackFace.StencilPassOp = SDLToD3D12_StencilOp[depthStencilState.backStencilState.passOp];
    desc->BackFace.StencilFunc = SDLToD3D12_CompareOp[depthStencilState.backStencilState.compareOp];

    return SDL_TRUE;
}

static SDL_bool D3D12_INTERNAL_ConvertVertexInputState(SDL_GpuVertexInputState vertexInputState, D3D12_INPUT_ELEMENT_DESC *desc, const char *semantic)
{
    if (desc == NULL || vertexInputState.vertexAttributeCount == 0) {
        return SDL_FALSE;
    }

    for (int i = 0; i < vertexInputState.vertexAttributeCount; i += 1) {
        SDL_GpuVertexAttribute attribute = vertexInputState.vertexAttributes[i];

        desc[i].SemanticName = semantic;
        desc[i].SemanticIndex = attribute.location;
        desc[i].Format = SDLToD3D12_VertexFormat[attribute.format];
        desc[i].InputSlot = attribute.binding;
        desc[i].AlignedByteOffset = attribute.offset;
        desc[i].InputSlotClass = SDLToD3D12_InputRate[vertexInputState.vertexBindings[attribute.binding].inputRate];
        desc[i].InstanceDataStepRate = vertexInputState.vertexBindings[attribute.binding].stepRate;
    }

    return SDL_TRUE;
}

static void D3D12_INTERNAL_AssignCpuDescriptorHandle(
    D3D12Renderer *renderer,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    D3D12CPUDescriptor *cpuDescriptor)
{
    D3D12DescriptorHeap *heap = renderer->stagingDescriptorHeaps[heapType];
    Uint32 descriptorIndex;

    cpuDescriptor->heap = heap;

    SDL_LockMutex(renderer->stagingDescriptorHeapLock);

    if (heap->inactiveDescriptorCount > 0) {
        descriptorIndex = heap->inactiveDescriptorIndices[heap->inactiveDescriptorCount - 1];
        heap->inactiveDescriptorCount -= 1;
    } else if (heap->currentDescriptorIndex < heap->maxDescriptors) {
        descriptorIndex = heap->currentDescriptorIndex;
        heap->currentDescriptorIndex += 1;
    } else {
        cpuDescriptor->cpuHandleIndex = SDL_MAX_UINT32;
        cpuDescriptor->cpuHandle.ptr = 0;
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Out of CPU descriptor handles, many bad things are going to happen!");
        SDL_UnlockMutex(renderer->stagingDescriptorHeapLock);
        return;
    }

    SDL_UnlockMutex(renderer->stagingDescriptorHeapLock);

    cpuDescriptor->cpuHandleIndex = descriptorIndex;
    cpuDescriptor->cpuHandle.ptr = heap->descriptorHeapCPUStart.ptr + (descriptorIndex * heap->descriptorSize);
}

static SDL_GpuGraphicsPipeline *D3D12_CreateGraphicsPipeline(
    SDL_GpuRenderer *driverData,
    SDL_GpuGraphicsPipelineCreateInfo *pipelineCreateInfo)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12Shader *vertShader = (D3D12Shader *)pipelineCreateInfo->vertexShader;
    D3D12Shader *fragShader = (D3D12Shader *)pipelineCreateInfo->fragmentShader;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    SDL_zero(psoDesc);
    psoDesc.VS.pShaderBytecode = vertShader->bytecode;
    psoDesc.VS.BytecodeLength = vertShader->bytecodeSize;
    psoDesc.PS.pShaderBytecode = fragShader->bytecode;
    psoDesc.PS.BytecodeLength = fragShader->bytecodeSize;

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
    if (pipelineCreateInfo->vertexInputState.vertexAttributeCount > 0) {
        psoDesc.InputLayout.pInputElementDescs = inputElementDescs;
        psoDesc.InputLayout.NumElements = pipelineCreateInfo->vertexInputState.vertexAttributeCount;
        D3D12_INTERNAL_ConvertVertexInputState(pipelineCreateInfo->vertexInputState, inputElementDescs, renderer->semantic);
    }

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    if (!D3D12_INTERNAL_ConvertRasterizerState(pipelineCreateInfo->rasterizerState, &psoDesc.RasterizerState)) {
        return NULL;
    }
    if (!D3D12_INTERNAL_ConvertBlendState(pipelineCreateInfo, &psoDesc.BlendState)) {
        return NULL;
    }
    if (!D3D12_INTERNAL_ConvertDepthStencilState(pipelineCreateInfo->depthStencilState, &psoDesc.DepthStencilState)) {
        return NULL;
    }

    D3D12GraphicsPipeline *pipeline = (D3D12GraphicsPipeline *)SDL_calloc(1, sizeof(D3D12GraphicsPipeline));
    if (!pipeline) {
        return NULL;
    }

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = SDLToD3D12_SampleCount[pipelineCreateInfo->multisampleState.sampleCount];
    psoDesc.SampleDesc.Quality = 0;

    psoDesc.DSVFormat = SDLToD3D12_TextureFormat[pipelineCreateInfo->attachmentInfo.depthStencilFormat];
    psoDesc.NumRenderTargets = pipelineCreateInfo->attachmentInfo.colorAttachmentCount;
    for (int i = 0; i < pipelineCreateInfo->attachmentInfo.colorAttachmentCount; i += 1) {
        psoDesc.RTVFormats[i] = SDLToD3D12_TextureFormat[pipelineCreateInfo->attachmentInfo.colorAttachmentDescriptions[i].format];
    }

    // Assuming some default values or further initialization
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
    psoDesc.CachedPSO.pCachedBlob = NULL;

    psoDesc.NodeMask = 0;

    D3D12GraphicsRootSignature *rootSignature = D3D12_INTERNAL_CreateGraphicsRootSignature(
        renderer,
        vertShader,
        fragShader);

    if (rootSignature == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not create root signature!");
        D3D12_INTERNAL_DestroyGraphicsPipeline(pipeline);
        return NULL;
    }
    pipeline->rootSignature = rootSignature;

    psoDesc.pRootSignature = rootSignature->handle;
    ID3D12PipelineState *pipelineState;

    HRESULT res = ID3D12Device_CreateGraphicsPipelineState(
        renderer->device,
        &psoDesc,
        D3D_GUID(D3D_IID_ID3D12PipelineState),
        (void **)&pipelineState);
    if (FAILED(res)) {
        D3D12_INTERNAL_LogError(renderer->device, "Could not create graphics pipeline state", res);
        D3D12_INTERNAL_DestroyGraphicsPipeline(pipeline);
        return NULL;
    }

    pipeline->pipelineState = pipelineState;

    for (int i = 0; i < pipelineCreateInfo->vertexInputState.vertexBindingCount; i += 1) {
        pipeline->vertexStrides[i] = pipelineCreateInfo->vertexInputState.vertexBindings[i].stride;
    }

    pipeline->primitiveType = pipelineCreateInfo->primitiveType;
    pipeline->blendConstants[0] = pipelineCreateInfo->blendConstants[0];
    pipeline->blendConstants[1] = pipelineCreateInfo->blendConstants[1];
    pipeline->blendConstants[2] = pipelineCreateInfo->blendConstants[2];
    pipeline->blendConstants[3] = pipelineCreateInfo->blendConstants[3];
    pipeline->stencilRef = pipelineCreateInfo->depthStencilState.reference;

    pipeline->vertexSamplerCount = vertShader->samplerCount;
    pipeline->vertexStorageTextureCount = vertShader->storageTextureCount;
    pipeline->vertexStorageBufferCount = vertShader->storageBufferCount;
    pipeline->vertexUniformBufferCount = vertShader->uniformBufferCount;

    pipeline->fragmentSamplerCount = fragShader->samplerCount;
    pipeline->fragmentStorageTextureCount = fragShader->storageTextureCount;
    pipeline->fragmentStorageBufferCount = fragShader->storageBufferCount;
    pipeline->fragmentUniformBufferCount = fragShader->uniformBufferCount;

    SDL_AtomicSet(&pipeline->referenceCount, 0);
    return (SDL_GpuGraphicsPipeline *)pipeline;
}

static SDL_GpuSampler *D3D12_CreateSampler(
    SDL_GpuRenderer *driverData,
    SDL_GpuSamplerCreateInfo *samplerCreateInfo)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12Sampler *sampler = (D3D12Sampler *)SDL_calloc(1, sizeof(D3D12Sampler));
    if (!sampler) {
        return NULL;
    }
    D3D12_SAMPLER_DESC samplerDesc;

    samplerDesc.Filter = SDLToD3D12_Filter(
        samplerCreateInfo->minFilter,
        samplerCreateInfo->magFilter,
        samplerCreateInfo->mipmapMode,
        samplerCreateInfo->compareEnable,
        samplerCreateInfo->anisotropyEnable);
    samplerDesc.AddressU = SDLToD3D12_SamplerAddressMode[samplerCreateInfo->addressModeU];
    samplerDesc.AddressV = SDLToD3D12_SamplerAddressMode[samplerCreateInfo->addressModeV];
    samplerDesc.AddressW = SDLToD3D12_SamplerAddressMode[samplerCreateInfo->addressModeW];
    samplerDesc.MaxAnisotropy = (Uint32)samplerCreateInfo->maxAnisotropy;
    samplerDesc.ComparisonFunc = SDLToD3D12_CompareOp[samplerCreateInfo->compareOp];
    samplerDesc.MinLOD = samplerCreateInfo->minLod;
    samplerDesc.MaxLOD = samplerCreateInfo->maxLod;
    samplerDesc.MipLODBias = samplerCreateInfo->mipLodBias;
    samplerDesc.BorderColor[0] = 0;
    samplerDesc.BorderColor[1] = 0;
    samplerDesc.BorderColor[2] = 0;
    samplerDesc.BorderColor[3] = 0;

    D3D12_INTERNAL_AssignCpuDescriptorHandle(
        renderer,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
        &sampler->handle);

    ID3D12Device_CreateSampler(
        renderer->device,
        &samplerDesc,
        sampler->handle.cpuHandle);

    sampler->createInfo = *samplerCreateInfo;
    SDL_AtomicSet(&sampler->referenceCount, 0);
    return (SDL_GpuSampler *)sampler;
}

static SDL_GpuShader *D3D12_CreateShader(
    SDL_GpuRenderer *driverData,
    SDL_GpuShaderCreateInfo *shaderCreateInfo)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    void *bytecode;
    size_t bytecodeSize;
    D3D12Shader *shader;

    if ((shaderCreateInfo->stage != SDL_GPU_SHADERSTAGE_VERTEX) && (shaderCreateInfo->stage != SDL_GPU_SHADERSTAGE_FRAGMENT)) {
        SDL_assert(SDL_FALSE);
    }

    if (!D3D12_INTERNAL_CreateShaderBytecode(
            renderer,
            shaderCreateInfo->stage,
            shaderCreateInfo->format,
            shaderCreateInfo->code,
            shaderCreateInfo->codeSize,
            shaderCreateInfo->entryPointName,
            &bytecode,
            &bytecodeSize)) {
        return NULL;
    }
    shader = (D3D12Shader *)SDL_calloc(1, sizeof(D3D12Shader));
    if (!shader) {
        SDL_free(bytecode);
        return NULL;
    }
    shader->samplerCount = shaderCreateInfo->samplerCount;
    shader->storageBufferCount = shaderCreateInfo->storageBufferCount;
    shader->storageTextureCount = shaderCreateInfo->storageTextureCount;
    shader->uniformBufferCount = shaderCreateInfo->uniformBufferCount;

    shader->bytecode = bytecode;
    shader->bytecodeSize = bytecodeSize;

    return (SDL_GpuShader *)shader;
}

static D3D12Texture *D3D12_INTERNAL_CreateTexture(
    D3D12Renderer *renderer,
    SDL_GpuTextureCreateInfo *textureCreateInfo)
{
    D3D12Texture *texture;
    ID3D12Resource *handle;
    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_HEAP_FLAGS heapFlags = (D3D12_HEAP_FLAGS)0;
    D3D12_RESOURCE_DESC desc;
    D3D12_RESOURCE_FLAGS resourceFlags = (D3D12_RESOURCE_FLAGS)0;
    D3D12_RESOURCE_STATES initialState = (D3D12_RESOURCE_STATES)0;
    HRESULT res;

    texture = (D3D12Texture *)SDL_calloc(1, sizeof(D3D12Texture));
    if (!texture) {
        return NULL;
    }

    if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT) {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT) {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 0; /* We don't do multi-adapter operation */
    heapProperties.VisibleNodeMask = 0;  /* We don't do multi-adapter operation */

    heapFlags = D3D12_HEAP_FLAG_NONE;

    if (textureCreateInfo->depth <= 1) {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width = textureCreateInfo->width;
        desc.Height = textureCreateInfo->height;
        desc.DepthOrArraySize = textureCreateInfo->isCube ? 6 : textureCreateInfo->layerCount;
        desc.MipLevels = textureCreateInfo->levelCount;
        desc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; /* Apparently this is the most efficient choice */
        desc.Flags = resourceFlags;
    } else {
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        desc.Width = textureCreateInfo->width;
        desc.Height = textureCreateInfo->height;
        desc.DepthOrArraySize = textureCreateInfo->depth;
        desc.MipLevels = textureCreateInfo->levelCount;
        desc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = resourceFlags;
    }

    initialState = D3D12_INTERNAL_DefaultTextureResourceState(textureCreateInfo->usageFlags);

    res = ID3D12Device_CreateCommittedResource(
        renderer->device,
        &heapProperties,
        heapFlags,
        &desc,
        initialState,
        NULL,
        D3D_GUID(D3D_IID_ID3D12Resource),
        (void **)&handle);
    if (FAILED(res)) {
        D3D12_INTERNAL_LogError(renderer->device, "Failed to create texture!", res);
        D3D12_INTERNAL_DestroyTexture(renderer, texture);
        return NULL;
    }

    texture->resource = handle;

    /* Create the SRV if applicable */
    if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;

        D3D12_INTERNAL_AssignCpuDescriptorHandle(
            renderer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            &texture->srvHandle);

        srvDesc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (textureCreateInfo->isCube) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = textureCreateInfo->levelCount;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.ResourceMinLODClamp = 0;
        } else if (textureCreateInfo->layerCount > 1) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MipLevels = textureCreateInfo->levelCount;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize = textureCreateInfo->layerCount;
            srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
            srvDesc.Texture2DArray.PlaneSlice = 0;
        } else if (textureCreateInfo->depth > 1) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srvDesc.Texture3D.MipLevels = textureCreateInfo->levelCount;
            srvDesc.Texture3D.MostDetailedMip = 0;
            srvDesc.Texture3D.ResourceMinLODClamp = 0; /* default behavior */
        } else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = textureCreateInfo->levelCount;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0; /* default behavior */
        }

        ID3D12Device_CreateShaderResourceView(
            renderer->device,
            handle,
            &srvDesc,
            texture->srvHandle.cpuHandle);
    }

    texture->subresourceCount = textureCreateInfo->levelCount * textureCreateInfo->layerCount;
    texture->subresources = (D3D12TextureSubresource *)SDL_calloc(
        texture->subresourceCount, sizeof(D3D12TextureSubresource));
    if (!texture->subresources) {
        D3D12_INTERNAL_DestroyTexture(renderer, texture);
        return NULL;
    }
    for (int layerIndex = 0; layerIndex < textureCreateInfo->layerCount; layerIndex += 1) {
        for (int levelIndex = 0; levelIndex < textureCreateInfo->levelCount; levelIndex += 1) {
            Uint32 subresourceIndex = D3D12_INTERNAL_CalcSubresource(
                (int)levelIndex,
                (int)layerIndex,
                (int)textureCreateInfo->levelCount);

            texture->subresources[subresourceIndex].parent = texture;
            texture->subresources[subresourceIndex].layer = layerIndex;
            texture->subresources[subresourceIndex].level = levelIndex;
            texture->subresources[subresourceIndex].index = subresourceIndex;

            texture->subresources[subresourceIndex].rtvHandle.heap = NULL;
            texture->subresources[subresourceIndex].dsvHandle.heap = NULL;
            texture->subresources[subresourceIndex].srvHandle.heap = NULL;
            texture->subresources[subresourceIndex].uavHandle.heap = NULL;
            SDL_AtomicSet(&texture->subresources[subresourceIndex].referenceCount, 0);

            /* Create RTV if needed */
            if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT) {
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;

                D3D12_INTERNAL_AssignCpuDescriptorHandle(
                    renderer,
                    D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                    &texture->subresources[subresourceIndex].rtvHandle);

                rtvDesc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];

                if (textureCreateInfo->layerCount > 1) {
                    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtvDesc.Texture2DArray.MipSlice = levelIndex;
                    rtvDesc.Texture2DArray.FirstArraySlice = layerIndex;
                    rtvDesc.Texture2DArray.ArraySize = 1;
                    rtvDesc.Texture2DArray.PlaneSlice = 0;
                } else if (textureCreateInfo->depth > 1) {
                    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
                    rtvDesc.Texture3D.MipSlice = levelIndex;
                    rtvDesc.Texture3D.FirstWSlice = 0;
                    rtvDesc.Texture3D.WSize = -1; /* all depths */
                } else {
                    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                    rtvDesc.Texture2D.MipSlice = levelIndex;
                    rtvDesc.Texture2D.PlaneSlice = 0;
                }

                ID3D12Device_CreateRenderTargetView(
                    renderer->device,
                    texture->resource,
                    &rtvDesc,
                    texture->subresources[subresourceIndex].rtvHandle.cpuHandle);
            }

            /* Create DSV if needed */
            if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT) {
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;

                D3D12_INTERNAL_AssignCpuDescriptorHandle(
                    renderer,
                    D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                    &texture->subresources[subresourceIndex].dsvHandle);

                dsvDesc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];
                dsvDesc.Flags = (D3D12_DSV_FLAGS)0;
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = levelIndex;

                ID3D12Device_CreateDepthStencilView(
                    renderer->device,
                    texture->resource,
                    &dsvDesc,
                    texture->subresources[subresourceIndex].dsvHandle.cpuHandle);
            }

            /* Create subresource SRV if needed */
            if (
                (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT) ||
                (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT)) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;

                D3D12_INTERNAL_AssignCpuDescriptorHandle(
                    renderer,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    &texture->subresources[subresourceIndex].srvHandle);

                srvDesc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                if (textureCreateInfo->layerCount > 1) {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    srvDesc.Texture2DArray.ArraySize = 1;
                    srvDesc.Texture2DArray.FirstArraySlice = layerIndex;
                    srvDesc.Texture2DArray.MipLevels = 1;
                    srvDesc.Texture2DArray.MostDetailedMip = levelIndex;
                    srvDesc.Texture2DArray.PlaneSlice = 0;
                    srvDesc.Texture2DArray.ResourceMinLODClamp = 0;
                } else if (textureCreateInfo->depth > 1) {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    srvDesc.Texture3D.MipLevels = 1;
                    srvDesc.Texture3D.MostDetailedMip = levelIndex;
                    srvDesc.Texture3D.ResourceMinLODClamp = 0;
                } else {
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Texture2D.MipLevels = 1;
                    srvDesc.Texture2D.MostDetailedMip = levelIndex;
                    srvDesc.Texture2D.PlaneSlice = 0;
                    srvDesc.Texture2D.ResourceMinLODClamp = 0;
                }

                ID3D12Device_CreateShaderResourceView(
                    renderer->device,
                    texture->resource,
                    &srvDesc,
                    texture->subresources[subresourceIndex].srvHandle.cpuHandle);
            }

            /* Create subresource UAV if necessary */
            if (textureCreateInfo->usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;

                D3D12_INTERNAL_AssignCpuDescriptorHandle(
                    renderer,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                    &texture->subresources[subresourceIndex].uavHandle);

                uavDesc.Format = SDLToD3D12_TextureFormat[textureCreateInfo->format];

                if (textureCreateInfo->layerCount > 1) {
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                    uavDesc.Texture2DArray.MipSlice = levelIndex;
                    uavDesc.Texture2DArray.FirstArraySlice = layerIndex;
                    uavDesc.Texture2DArray.ArraySize = 1;
                } else if (textureCreateInfo->depth > 1) {
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                    uavDesc.Texture3D.MipSlice = levelIndex;
                    uavDesc.Texture3D.FirstWSlice = 0;
                    uavDesc.Texture3D.WSize = textureCreateInfo->layerCount;
                } else {
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    uavDesc.Texture2D.MipSlice = levelIndex;
                    uavDesc.Texture2D.PlaneSlice = 0;
                }

                ID3D12Device_CreateUnorderedAccessView(
                    renderer->device,
                    texture->resource,
                    NULL,
                    &uavDesc,
                    texture->subresources[subresourceIndex].uavHandle.cpuHandle);
            }
        }
    }

    return texture;
}

static SDL_GpuTexture *D3D12_CreateTexture(
    SDL_GpuRenderer *driverData,
    SDL_GpuTextureCreateInfo *textureCreateInfo)
{
    D3D12TextureContainer *container = (D3D12TextureContainer *)SDL_calloc(1, sizeof(D3D12TextureContainer));
    if (!container) {
        return NULL;
    }

    container->header.info = *textureCreateInfo;
    container->textureCapacity = 1;
    container->textureCount = 1;
    container->textures = (D3D12Texture **)SDL_calloc(
        container->textureCapacity, sizeof(D3D12Texture *));

    if (!container->textures) {
        SDL_free(container);
        return NULL;
    }

    container->debugName = NULL;
    container->canBeCycled = SDL_TRUE;

    D3D12Texture *texture = D3D12_INTERNAL_CreateTexture(
        (D3D12Renderer *)driverData,
        textureCreateInfo);

    if (!texture) {
        SDL_free(container->textures);
        SDL_free(container);
        return NULL;
    }

    container->textures[0] = texture;
    container->activeTexture = texture;

    texture->container = container;
    texture->containerIndex = 0;

    return (SDL_GpuTexture *)container;
}

static D3D12Buffer *D3D12_INTERNAL_CreateBuffer(
    D3D12Renderer *renderer,
    SDL_GpuBufferUsageFlags usageFlags,
    Uint32 sizeInBytes,
    D3D12BufferType type)
{
    D3D12Buffer *buffer;
    ID3D12Resource *handle;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    D3D12_HEAP_PROPERTIES heapProperties;
    D3D12_RESOURCE_DESC desc;
    D3D12_HEAP_FLAGS heapFlags = (D3D12_HEAP_FLAGS)0;
    D3D12_RESOURCE_FLAGS resourceFlags = (D3D12_RESOURCE_FLAGS)0;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    HRESULT res;

    buffer = (D3D12Buffer *)SDL_calloc(1, sizeof(D3D12Buffer));

    if (!buffer) {
        return NULL;
    }

    if (usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
        resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    heapProperties.CreationNodeMask = 0; /* We don't do multi-adapter operation */
    heapProperties.VisibleNodeMask = 0;  /* We don't do multi-adapter operation */

    if (type == D3D12_BUFFER_TYPE_GPU) {
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapFlags = D3D12_HEAP_FLAG_NONE;
    } else if (type == D3D12_BUFFER_TYPE_UPLOAD) {
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapFlags = D3D12_HEAP_FLAG_NONE;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (type == D3D12_BUFFER_TYPE_DOWNLOAD) {
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapFlags = D3D12_HEAP_FLAG_NONE;
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    } else if (type == D3D12_BUFFER_TYPE_UNIFORM) {
        /* D3D12 is badly designed, so we have to check if the fast path for uniform buffers is enabled */
        if (renderer->GPUUploadHeapSupported) {
            heapProperties.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        } else {
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        }
        heapFlags = D3D12_HEAP_FLAG_NONE;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Unrecognized buffer type!");
        return NULL;
    }

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Width = sizeInBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = resourceFlags;

    res = ID3D12Device_CreateCommittedResource(
        renderer->device,
        &heapProperties,
        heapFlags,
        &desc,
        initialState,
        NULL,
        D3D_GUID(D3D_IID_ID3D12Resource),
        (void **)&handle);
    if (FAILED(res)) {
        D3D12_INTERNAL_LogError(renderer->device, "Could not create buffer!", res);
        D3D12_INTERNAL_DestroyBuffer(renderer, buffer);
        return NULL;
    }

    buffer->handle = handle;
    SDL_AtomicSet(&buffer->referenceCount, 0);

    buffer->uavDescriptor.heap = NULL;
    buffer->srvDescriptor.heap = NULL;
    buffer->cbvDescriptor.heap = NULL;

    if (usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT) {
        D3D12_INTERNAL_AssignCpuDescriptorHandle(
            renderer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            &buffer->uavDescriptor);

        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = sizeInBytes / sizeof(Uint32);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.CounterOffsetInBytes = 0; /* TODO: support counters? */
        uavDesc.Buffer.StructureByteStride = 0;

        /* Create UAV */
        ID3D12Device_CreateUnorderedAccessView(
            renderer->device,
            handle,
            NULL, /* TODO: support counters? */
            &uavDesc,
            buffer->uavDescriptor.cpuHandle);
    }

    if (
        (usageFlags & SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ_BIT) ||
        (usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ_BIT)) {
        D3D12_INTERNAL_AssignCpuDescriptorHandle(
            renderer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            &buffer->srvDescriptor);

        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = sizeInBytes / sizeof(Uint32);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        srvDesc.Buffer.StructureByteStride = 0;

        /* Create SRV */
        ID3D12Device_CreateShaderResourceView(
            renderer->device,
            handle,
            &srvDesc,
            buffer->srvDescriptor.cpuHandle);
    }

    /* FIXME: we may not need a CBV since we use root descriptors */
    if (type == D3D12_BUFFER_TYPE_UNIFORM) {
        D3D12_INTERNAL_AssignCpuDescriptorHandle(
            renderer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            &buffer->cbvDescriptor);

        cbvDesc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(handle);
        cbvDesc.SizeInBytes = sizeInBytes;

        /* Create CBV */
        ID3D12Device_CreateConstantBufferView(
            renderer->device,
            &cbvDesc,
            buffer->cbvDescriptor.cpuHandle);
    }

    buffer->virtualAddress = 0;
    if (type == D3D12_BUFFER_TYPE_GPU || type == D3D12_BUFFER_TYPE_UNIFORM) {
        buffer->virtualAddress = ID3D12Resource_GetGPUVirtualAddress(buffer->handle);
    }

    buffer->mapPointer = NULL;
    /* Persistently map upload buffers */
    if (type == D3D12_BUFFER_TYPE_UPLOAD) {
        res = ID3D12Resource_Map(
            buffer->handle,
            0,
            NULL,
            (void **)&buffer->mapPointer);
        if (FAILED(res)) {
            D3D12_INTERNAL_LogError(renderer->device, "Failed to map upload buffer!", res);
            D3D12_INTERNAL_DestroyBuffer(renderer, buffer);
            return NULL;
        }
    }

    buffer->container = NULL;
    buffer->containerIndex = 0;

    buffer->transitioned = initialState != D3D12_RESOURCE_STATE_COMMON;
    SDL_AtomicSet(&buffer->referenceCount, 0);
    return buffer;
}

/* TODO */
static D3D12BufferContainer *D3D12_INTERNAL_CreateBufferContainer(
    D3D12Renderer *renderer,
    SDL_GpuBufferUsageFlags usageFlags,
    Uint32 sizeInBytes,
    D3D12BufferType type)
{
    D3D12BufferContainer *container;
    D3D12Buffer *buffer;

    container = (D3D12BufferContainer *)SDL_calloc(1, sizeof(D3D12BufferContainer));
    if (!container) {
        return NULL;
    }

    container->usageFlags = usageFlags;
    container->size = sizeInBytes;
    container->type = type;

    container->bufferCapacity = 1;
    container->bufferCount = 1;
    container->buffers = (D3D12Buffer **)SDL_calloc(
        container->bufferCapacity, sizeof(D3D12Buffer *));
    if (!container->buffers) {
        SDL_free(container);
        return NULL;
    }
    container->debugName = NULL;

    buffer = D3D12_INTERNAL_CreateBuffer(
        renderer,
        usageFlags,
        sizeInBytes,
        type);

    if (buffer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create buffer!");
        SDL_free(container->buffers);
        SDL_free(container);
        return NULL;
    }

    container->activeBuffer = buffer;
    container->buffers[0] = buffer;
    buffer->container = container;
    buffer->containerIndex = 0;

    return container;
}

static SDL_GpuBuffer *D3D12_CreateBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuBufferUsageFlags usageFlags,
    Uint32 sizeInBytes)
{
    return (SDL_GpuBuffer *)D3D12_INTERNAL_CreateBufferContainer(
        (D3D12Renderer *)driverData,
        usageFlags,
        sizeInBytes,
        D3D12_BUFFER_TYPE_GPU);
}

static SDL_GpuTransferBuffer *D3D12_CreateTransferBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuTransferBufferUsage usage,
    Uint32 sizeInBytes)
{
    return (SDL_GpuTransferBuffer *)D3D12_INTERNAL_CreateBufferContainer(
        (D3D12Renderer *)driverData,
        0,
        sizeInBytes,
        usage == SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD ? D3D12_BUFFER_TYPE_UPLOAD : D3D12_BUFFER_TYPE_DOWNLOAD);
}

/* Debug Naming */

static void D3D12_SetBufferName(
    SDL_GpuRenderer *driverData,
    SDL_GpuBuffer *buffer,
    const char *text)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12BufferContainer *container = (D3D12BufferContainer *)buffer;
    size_t textLength = SDL_strlen(text) + 1;

    if (renderer->debugMode) {
        container->debugName = (char *)SDL_realloc(
            container->debugName,
            textLength);

        SDL_utf8strlcpy(
            container->debugName,
            text,
            textLength);

        for (Uint32 i = 0; i < container->bufferCount; i += 1) {
            D3D12_INTERNAL_SetResourceName(
                renderer,
                container->buffers[i]->handle,
                text);
        }
    }
}

static void D3D12_SetTextureName(
    SDL_GpuRenderer *driverData,
    SDL_GpuTexture *texture,
    const char *text)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12TextureContainer *container = (D3D12TextureContainer *)texture;
    size_t textLength = SDL_strlen(text) + 1;

    if (renderer->debugMode) {
        container->debugName = (char *)SDL_realloc(
            container->debugName,
            textLength);

        SDL_utf8strlcpy(
            container->debugName,
            text,
            textLength);

        for (Uint32 i = 0; i < container->textureCount; i += 1) {
            D3D12_INTERNAL_SetResourceName(
                renderer,
                container->textures[i]->resource,
                text);
        }
    }
}

/* These debug functions are all marked as "for internal usage only"
 * on D3D12... works on renderdoc!
 */

static SDL_bool D3D12_INTERNAL_StrToWStr(
    D3D12Renderer *renderer,
    const char *str,
    wchar_t *wstr,
    size_t wstr_size,
    Uint32 *outSize)
{
    size_t inlen, result;
    size_t outlen = wstr_size;

    if (renderer->iconv == NULL) {
        renderer->iconv = SDL_iconv_open("WCHAR_T", "UTF-8");
        SDL_assert(renderer->iconv);
    }

    /* Convert... */
    inlen = SDL_strlen(str) + 1;
    result = SDL_iconv(
        renderer->iconv,
        &str,
        &inlen,
        (char **)&wstr,
        &outlen);

    *outSize = (Uint32)outlen;

    /* Check... */
    switch (result) {
    case SDL_ICONV_ERROR:
    case SDL_ICONV_E2BIG:
    case SDL_ICONV_EILSEQ:
    case SDL_ICONV_EINVAL:
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Failed to convert string to wchar_t!");
        return SDL_FALSE;
    default:
        break;
    }

    return SDL_TRUE;
}

static void D3D12_InsertDebugLabel(
    SDL_GpuCommandBuffer *commandBuffer,
    const char *text)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    wchar_t wstr[256];
    Uint32 convSize;

    if (!D3D12_INTERNAL_StrToWStr(
        d3d12CommandBuffer->renderer,
        text,
        wstr,
        sizeof(wstr),
        &convSize)) {
        return;
    }

    ID3D12GraphicsCommandList_SetMarker(
        d3d12CommandBuffer->graphicsCommandList,
        0,
        wstr,
        convSize);
}

static void D3D12_PushDebugGroup(
    SDL_GpuCommandBuffer *commandBuffer,
    const char *name)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    wchar_t wstr[256];
    Uint32 convSize;

    if (!D3D12_INTERNAL_StrToWStr(
        d3d12CommandBuffer->renderer,
        name,
        wstr,
        sizeof(wstr),
        &convSize)) {
        return;
    }

    ID3D12GraphicsCommandList_BeginEvent(
        d3d12CommandBuffer->graphicsCommandList,
        0,
        wstr,
        convSize);
}

static void D3D12_PopDebugGroup(
    SDL_GpuCommandBuffer *commandBuffer)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    ID3D12GraphicsCommandList_EndEvent(d3d12CommandBuffer->graphicsCommandList);
}

/* Disposal */

static void D3D12_ReleaseTexture(
    SDL_GpuRenderer *driverData,
    SDL_GpuTexture *texture)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12TextureContainer *container = (D3D12TextureContainer *)texture;

    D3D12_INTERNAL_ReleaseTextureContainer(
        renderer,
        container);
}

static void D3D12_ReleaseSampler(
    SDL_GpuRenderer *driverData,
    SDL_GpuSampler *sampler)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12Sampler *d3d12Sampler = (D3D12Sampler *)sampler;

    SDL_LockMutex(renderer->disposeLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->samplersToDestroy,
        D3D12Sampler *,
        renderer->samplersToDestroyCount + 1,
        renderer->samplersToDestroyCapacity,
        renderer->samplersToDestroyCapacity * 2)

    renderer->samplersToDestroy[renderer->samplersToDestroyCount] = d3d12Sampler;
    renderer->samplersToDestroyCount += 1;

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_ReleaseBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuBuffer *buffer)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12BufferContainer *bufferContainer = (D3D12BufferContainer *)buffer;

    D3D12_INTERNAL_ReleaseBufferContainer(
        renderer,
        bufferContainer);
}

static void D3D12_ReleaseTransferBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuTransferBuffer *transferBuffer)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12BufferContainer *transferBufferContainer = (D3D12BufferContainer *)transferBuffer;

    D3D12_INTERNAL_ReleaseBufferContainer(
        renderer,
        transferBufferContainer);
}

static void D3D12_ReleaseShader(
    SDL_GpuRenderer *driverData,
    SDL_GpuShader *shader)
{
    /* D3D12Renderer *renderer = (D3D12Renderer *)driverData; */
    D3D12Shader *d3d12shader = (D3D12Shader *)shader;

    if (d3d12shader->bytecode) {
        SDL_free(d3d12shader->bytecode);
        d3d12shader->bytecode = NULL;
    }
    SDL_free(d3d12shader);
}

static void D3D12_ReleaseComputePipeline(
    SDL_GpuRenderer *driverData,
    SDL_GpuComputePipeline *computePipeline)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12ComputePipeline *d3d12ComputePipeline = (D3D12ComputePipeline *)computePipeline;

    SDL_LockMutex(renderer->disposeLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->computePipelinesToDestroy,
        D3D12ComputePipeline *,
        renderer->computePipelinesToDestroyCount + 1,
        renderer->computePipelinesToDestroyCapacity,
        renderer->computePipelinesToDestroyCapacity * 2)

    renderer->computePipelinesToDestroy[renderer->computePipelinesToDestroyCount] = d3d12ComputePipeline;
    renderer->computePipelinesToDestroyCount += 1;

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_ReleaseGraphicsPipeline(
    SDL_GpuRenderer *driverData,
    SDL_GpuGraphicsPipeline *graphicsPipeline)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12GraphicsPipeline *d3d12GraphicsPipeline = (D3D12GraphicsPipeline *)graphicsPipeline;

    SDL_LockMutex(renderer->disposeLock);

    EXPAND_ARRAY_IF_NEEDED(
        renderer->graphicsPipelinesToDestroy,
        D3D12GraphicsPipeline *,
        renderer->graphicsPipelinesToDestroyCount + 1,
        renderer->graphicsPipelinesToDestroyCapacity,
        renderer->graphicsPipelinesToDestroyCapacity * 2)

    renderer->graphicsPipelinesToDestroy[renderer->graphicsPipelinesToDestroyCount] = d3d12GraphicsPipeline;
    renderer->graphicsPipelinesToDestroyCount += 1;

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_ReleaseBlitPipelines(D3D12Renderer *renderer)
{
    /* Release blit pipeline structures */
    D3D12_ReleaseSampler((SDL_GpuRenderer *)renderer, renderer->blitLinearSampler);
    D3D12_ReleaseSampler((SDL_GpuRenderer *)renderer, renderer->blitNearestSampler);

    D3D12_ReleaseGraphicsPipeline((SDL_GpuRenderer *)renderer, renderer->blitFrom2DPipeline);
}

/* Render Pass */

static void D3D12_SetViewport(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuViewport *viewport)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_VIEWPORT d3d12Viewport;
    d3d12Viewport.TopLeftX = viewport->x;
    d3d12Viewport.TopLeftY = viewport->y;
    d3d12Viewport.Width = viewport->w;
    d3d12Viewport.Height = viewport->h;
    d3d12Viewport.MinDepth = viewport->minDepth;
    d3d12Viewport.MaxDepth = viewport->maxDepth;
    ID3D12GraphicsCommandList_RSSetViewports(d3d12CommandBuffer->graphicsCommandList, 1, &d3d12Viewport);
}

static void D3D12_SetScissor(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_Rect *scissor)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_RECT scissorRect;
    scissorRect.left = scissor->x;
    scissorRect.top = scissor->y;
    scissorRect.right = scissor->x + scissor->w;
    scissorRect.bottom = scissor->y + scissor->h;
    ID3D12GraphicsCommandList_RSSetScissorRects(d3d12CommandBuffer->graphicsCommandList, 1, &scissorRect);
}

static D3D12TextureSubresource *D3D12_INTERNAL_FetchTextureSubresource(
    D3D12TextureContainer *container,
    Uint32 layer,
    Uint32 level)
{
    Uint32 index = D3D12_INTERNAL_CalcSubresource(
        level,
        layer,
        container->header.info.levelCount);
    return &container->activeTexture->subresources[index];
}

static void D3D12_INTERNAL_CycleActiveTexture(
    D3D12Renderer *renderer,
    D3D12TextureContainer *container)
{
    D3D12Texture *texture;
    Sint32 refCountTotal;

    /* If a previously-cycled texture is available, we can use that. */
    for (Uint32 i = 0; i < container->textureCount; i += 1) {
        texture = container->textures[i];

        refCountTotal = 0;
        for (Uint32 j = 0; j < texture->subresourceCount; j += 1) {
            refCountTotal += SDL_AtomicGet(&texture->subresources[j].referenceCount);
        }

        if (refCountTotal == 0) {
            container->activeTexture = texture;
            return;
        }
    }

    /* No texture is available, generate a new one. */
    texture = D3D12_INTERNAL_CreateTexture(
        renderer,
        &container->header.info);

    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to cycle active texture!");
        return;
    }

    EXPAND_ARRAY_IF_NEEDED(
        container->textures,
        D3D12Texture *,
        container->textureCount + 1,
        container->textureCapacity,
        container->textureCapacity * 2);

    container->textures[container->textureCount] = texture;
    texture->container = container;
    texture->containerIndex = container->textureCount;
    container->textureCount += 1;

    container->activeTexture = texture;

    if (renderer->debugMode && container->debugName != NULL) {
        D3D12_INTERNAL_SetResourceName(
            renderer,
            container->activeTexture->resource,
            container->debugName);
    }
}

static D3D12TextureSubresource *D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
    D3D12CommandBuffer *commandBuffer,
    D3D12TextureContainer *container,
    Uint32 layer,
    Uint32 level,
    SDL_bool cycle,
    D3D12_RESOURCE_STATES destinationUsageMode)
{
    D3D12TextureSubresource *subresource = D3D12_INTERNAL_FetchTextureSubresource(
        container,
        layer,
        level);

    if (
        container->canBeCycled &&
        cycle &&
        SDL_AtomicGet(&subresource->referenceCount) > 0) {
        D3D12_INTERNAL_CycleActiveTexture(
            commandBuffer->renderer,
            container);

        subresource = D3D12_INTERNAL_FetchTextureSubresource(
            container,
            layer,
            level);
    }

    D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
        commandBuffer,
        destinationUsageMode,
        subresource);

    return subresource;
}

static void D3D12_INTERNAL_CycleActiveBuffer(
    D3D12Renderer *renderer,
    D3D12BufferContainer *container)
{
    /* If a previously-cycled buffer is available, we can use that. */
    for (Uint32 i = 0; i < container->bufferCount; i += 1) {
        D3D12Buffer *buffer = container->buffers[i];
        if (SDL_AtomicGet(&buffer->referenceCount) == 0) {
            container->activeBuffer = buffer;
            return;
        }
    }

    /* No buffer handle is available, create a new one. */
    D3D12Buffer *buffer = D3D12_INTERNAL_CreateBuffer(
        renderer,
        container->usageFlags,
        container->size,
        container->type);

    if (!buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to cycle active buffer!");
        return;
    }

    EXPAND_ARRAY_IF_NEEDED(
        container->buffers,
        D3D12Buffer *,
        container->bufferCount + 1,
        container->bufferCapacity,
        container->bufferCapacity * 2);

    container->buffers[container->bufferCount] = buffer;
    buffer->container = container;
    buffer->containerIndex = container->bufferCount;
    container->bufferCount += 1;

    container->activeBuffer = buffer;

    if (renderer->debugMode && container->debugName != NULL) {
        D3D12_INTERNAL_SetResourceName(
            renderer,
            container->activeBuffer->handle,
            container->debugName);
    }
}

static D3D12Buffer *D3D12_INTERNAL_PrepareBufferForWrite(
    D3D12CommandBuffer *commandBuffer,
    D3D12BufferContainer *container,
    SDL_bool cycle,
    D3D12_RESOURCE_STATES destinationState)
{
    if (
        cycle &&
        SDL_AtomicGet(&container->activeBuffer->referenceCount) > 0) {
        D3D12_INTERNAL_CycleActiveBuffer(
            commandBuffer->renderer,
            container);
    }

    D3D12_INTERNAL_BufferTransitionFromDefaultUsage(
        commandBuffer,
        destinationState,
        container->activeBuffer);

    return container->activeBuffer;
}

static void D3D12_BeginRenderPass(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuColorAttachmentInfo *colorAttachmentInfos,
    Uint32 colorAttachmentCount,
    SDL_GpuDepthStencilAttachmentInfo *depthStencilAttachmentInfo)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    /* D3D12Renderer *renderer = d3d12CommandBuffer->renderer; */

    Uint32 framebufferWidth = SDL_MAX_UINT32;
    Uint32 framebufferHeight = SDL_MAX_UINT32;

    for (Uint32 i = 0; i < colorAttachmentCount; i += 1) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)colorAttachmentInfos[i].textureSlice.texture;
        Uint32 h = container->header.info.height >> colorAttachmentInfos[i].textureSlice.mipLevel;
        Uint32 w = container->header.info.width >> colorAttachmentInfos[i].textureSlice.mipLevel;

        /* The framebuffer cannot be larger than the smallest attachment. */

        if (w < framebufferWidth) {
            framebufferWidth = w;
        }

        if (h < framebufferHeight) {
            framebufferHeight = h;
        }

        if (!(container->header.info.usageFlags & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Color attachment texture was not designated as a color target!");
            return;
        }
    }

    if (depthStencilAttachmentInfo != NULL) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)depthStencilAttachmentInfo->textureSlice.texture;

        Uint32 h = container->header.info.height >> depthStencilAttachmentInfo->textureSlice.mipLevel;
        Uint32 w = container->header.info.width >> depthStencilAttachmentInfo->textureSlice.mipLevel;

        /* The framebuffer cannot be larger than the smallest attachment. */

        if (w < framebufferWidth) {
            framebufferWidth = w;
        }

        if (h < framebufferHeight) {
            framebufferHeight = h;
        }

        /* Fixme: */
        if (!(container->header.info.usageFlags & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Depth stencil attachment texture was not designated as a depth target!");
            return;
        }
    }

    /* Layout transitions */

    d3d12CommandBuffer->colorAttachmentCount = colorAttachmentCount;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[MAX_COLOR_TARGET_BINDINGS];

    for (Uint32 i = 0; i < colorAttachmentCount; i += 1) {
        SDL_bool cycle;
        if (colorAttachmentInfos[i].loadOp == SDL_GPU_LOADOP_LOAD) {
            cycle = SDL_FALSE;
        } else {
            cycle = colorAttachmentInfos[i].cycle;
        }

        D3D12TextureContainer *container = (D3D12TextureContainer *)colorAttachmentInfos[i].textureSlice.texture;
        D3D12TextureSubresource *subresource =
            D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
                d3d12CommandBuffer,
                container,
                colorAttachmentInfos[i].textureSlice.layer,
                colorAttachmentInfos[i].textureSlice.mipLevel,
                cycle,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

        if (colorAttachmentInfos[i].loadOp == SDL_GPU_LOADOP_CLEAR) {
            float clearColor[4];
            clearColor[0] = colorAttachmentInfos[i].clearColor.r;
            clearColor[1] = colorAttachmentInfos[i].clearColor.g;
            clearColor[2] = colorAttachmentInfos[i].clearColor.b;
            clearColor[3] = colorAttachmentInfos[i].clearColor.a;

            ID3D12GraphicsCommandList_ClearRenderTargetView(
                d3d12CommandBuffer->graphicsCommandList,
                subresource->rtvHandle.cpuHandle,
                clearColor,
                0,
                NULL);
        }

        rtvs[i] = subresource->rtvHandle.cpuHandle;
        d3d12CommandBuffer->colorAttachmentTextureSubresources[i] = subresource;
        D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, subresource);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    if (depthStencilAttachmentInfo != NULL) {
        SDL_bool cycle;

        if (
            depthStencilAttachmentInfo->loadOp == SDL_GPU_LOADOP_LOAD ||
            depthStencilAttachmentInfo->stencilLoadOp == SDL_GPU_LOADOP_LOAD) {
            cycle = SDL_FALSE;
        } else {
            cycle = depthStencilAttachmentInfo->cycle;
        }

        D3D12TextureContainer *container = (D3D12TextureContainer *)depthStencilAttachmentInfo->textureSlice.texture;
        D3D12TextureSubresource *subresource = D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
            d3d12CommandBuffer,
            container,
            depthStencilAttachmentInfo->textureSlice.layer,
            depthStencilAttachmentInfo->textureSlice.mipLevel,
            cycle,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);

        if (
            depthStencilAttachmentInfo->loadOp == SDL_GPU_LOADOP_LOAD ||
            depthStencilAttachmentInfo->stencilLoadOp == SDL_GPU_LOADOP_LOAD) {
            D3D12_CLEAR_FLAGS clearFlags = (D3D12_CLEAR_FLAGS)0;
            if (depthStencilAttachmentInfo->loadOp == SDL_GPU_LOADOP_LOAD) {
                clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
            }
            if (depthStencilAttachmentInfo->stencilLoadOp == SDL_GPU_LOADOP_LOAD) {
                clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }

            ID3D12GraphicsCommandList_ClearDepthStencilView(
                d3d12CommandBuffer->graphicsCommandList,
                subresource->dsvHandle.cpuHandle,
                clearFlags,
                depthStencilAttachmentInfo->depthStencilClearValue.depth,
                depthStencilAttachmentInfo->depthStencilClearValue.stencil,
                0,
                NULL);
        }

        dsv = subresource->dsvHandle.cpuHandle;
        d3d12CommandBuffer->depthStencilTextureSubresource = subresource;
        D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, subresource);
    }

    ID3D12GraphicsCommandList_OMSetRenderTargets(
        d3d12CommandBuffer->graphicsCommandList,
        colorAttachmentCount,
        rtvs,
        SDL_FALSE,
        (depthStencilAttachmentInfo == NULL) ? NULL : &dsv);

    /* Set sensible default viewport state */
    SDL_GpuViewport defaultViewport;
    defaultViewport.x = 0;
    defaultViewport.y = 0;
    defaultViewport.w = (float)framebufferWidth;
    defaultViewport.h = (float)framebufferHeight;
    defaultViewport.minDepth = 0;
    defaultViewport.maxDepth = 1;

    D3D12_SetViewport(
        commandBuffer,
        &defaultViewport);

    SDL_Rect defaultScissor;
    defaultScissor.x = 0;
    defaultScissor.y = 0;
    defaultScissor.w = (Sint32)framebufferWidth;
    defaultScissor.h = (Sint32)framebufferHeight;

    D3D12_SetScissor(
        commandBuffer,
        &defaultScissor);
}

static void D3D12_INTERNAL_TrackUniformBuffer(
    D3D12CommandBuffer *commandBuffer,
    D3D12UniformBuffer *uniformBuffer)
{
    Uint32 i;
    for (i = 0; i < commandBuffer->usedUniformBufferCount; i += 1) {
        if (commandBuffer->usedUniformBuffers[i] == uniformBuffer) {
            return;
        }
    }

    if (commandBuffer->usedUniformBufferCount == commandBuffer->usedUniformBufferCapacity) {
        commandBuffer->usedUniformBufferCapacity += 1;
        commandBuffer->usedUniformBuffers = (D3D12UniformBuffer **)SDL_realloc(
            commandBuffer->usedUniformBuffers,
            commandBuffer->usedUniformBufferCapacity * sizeof(D3D12UniformBuffer *));
        for (i = commandBuffer->usedUniformBufferCount; i < commandBuffer->usedUniformBufferCapacity; i += 1) {
            SDL_zerop(commandBuffer->usedUniformBuffers[i]);
        }
    }

    commandBuffer->usedUniformBuffers[commandBuffer->usedUniformBufferCount] = uniformBuffer;
    commandBuffer->usedUniformBufferCount += 1;

    D3D12_INTERNAL_TrackBuffer(
        commandBuffer,
        uniformBuffer->buffer);
}

static D3D12UniformBuffer *D3D12_INTERNAL_AcquireUniformBufferFromPool(
    D3D12CommandBuffer *commandBuffer)
{
    D3D12Renderer *renderer = commandBuffer->renderer;
    D3D12UniformBuffer *uniformBuffer;

    SDL_LockMutex(renderer->acquireUniformBufferLock);

    if (renderer->uniformBufferPoolCount > 0) {
        uniformBuffer = renderer->uniformBufferPool[renderer->uniformBufferPoolCount - 1];
        renderer->uniformBufferPoolCount -= 1;
    } else {
        uniformBuffer = (D3D12UniformBuffer *)SDL_calloc(1, sizeof(D3D12UniformBuffer));
        if (!uniformBuffer) {
            SDL_UnlockMutex(renderer->acquireUniformBufferLock);
            return NULL;
        }

        uniformBuffer->buffer = D3D12_INTERNAL_CreateBuffer(
            renderer,
            0,
            UNIFORM_BUFFER_SIZE,
            D3D12_BUFFER_TYPE_UNIFORM);
        if (!uniformBuffer->buffer) {
            SDL_UnlockMutex(renderer->acquireUniformBufferLock);
            return NULL;
        }
    }

    SDL_UnlockMutex(renderer->acquireUniformBufferLock);

    uniformBuffer->currentBlockSize = 0;
    uniformBuffer->drawOffset = 0;
    uniformBuffer->writeOffset = 0;

    HRESULT res = ID3D12Resource_Map(
        uniformBuffer->buffer->handle,
        0,
        NULL,
        (void **)&uniformBuffer->buffer->mapPointer);
    ERROR_CHECK_RETURN("Failed to map buffer pool!", NULL);

    D3D12_INTERNAL_TrackUniformBuffer(commandBuffer, uniformBuffer);

    return uniformBuffer;
}

static void D3D12_INTERNAL_ReturnUniformBufferToPool(
    D3D12Renderer *renderer,
    D3D12UniformBuffer *uniformBuffer)
{
    if (renderer->uniformBufferPoolCount >= renderer->uniformBufferPoolCapacity) {
        renderer->uniformBufferPoolCapacity *= 2;
        renderer->uniformBufferPool = (D3D12UniformBuffer **)SDL_realloc(
            renderer->uniformBufferPool,
            renderer->uniformBufferPoolCapacity * sizeof(D3D12UniformBuffer *));
    }

    renderer->uniformBufferPool[renderer->uniformBufferPoolCount] = uniformBuffer;
    renderer->uniformBufferPoolCount += 1;
}

static void D3D12_INTERNAL_PushUniformData(
    D3D12CommandBuffer *commandBuffer,
    SDL_GpuShaderStage shaderStage,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    D3D12UniformBuffer *uniformBuffer;

    if (shaderStage == SDL_GPU_SHADERSTAGE_VERTEX) {
        if (commandBuffer->vertexUniformBuffers[slotIndex] == NULL) {
            commandBuffer->vertexUniformBuffers[slotIndex] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                commandBuffer);
        }
        uniformBuffer = commandBuffer->vertexUniformBuffers[slotIndex];
    } else if (shaderStage == SDL_GPU_SHADERSTAGE_FRAGMENT) {
        if (commandBuffer->fragmentUniformBuffers[slotIndex] == NULL) {
            commandBuffer->fragmentUniformBuffers[slotIndex] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                commandBuffer);
        }
        uniformBuffer = commandBuffer->fragmentUniformBuffers[slotIndex];
    } else if (shaderStage == SDL_GPU_SHADERSTAGE_COMPUTE) {
        if (commandBuffer->computeUniformBuffers[slotIndex] == NULL) {
            commandBuffer->computeUniformBuffers[slotIndex] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                commandBuffer);
        }
        uniformBuffer = commandBuffer->computeUniformBuffers[slotIndex];
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Unrecognized shader stage!");
        return;
    }

    uniformBuffer->currentBlockSize =
        D3D12_INTERNAL_Align(
            dataLengthInBytes,
            256);

    /* If there is no more room, acquire a new uniform buffer */
    if (uniformBuffer->writeOffset + uniformBuffer->currentBlockSize >= UNIFORM_BUFFER_SIZE) {
        ID3D12Resource_Unmap(
            uniformBuffer->buffer->handle,
            0,
            NULL);
        uniformBuffer->buffer->mapPointer = NULL;

        uniformBuffer = D3D12_INTERNAL_AcquireUniformBufferFromPool(commandBuffer);

        uniformBuffer->drawOffset = 0;
        uniformBuffer->writeOffset = 0;

        if (shaderStage == SDL_GPU_SHADERSTAGE_VERTEX) {
            commandBuffer->vertexUniformBuffers[slotIndex] = uniformBuffer;
        } else if (shaderStage == SDL_GPU_SHADERSTAGE_FRAGMENT) {
            commandBuffer->fragmentUniformBuffers[slotIndex] = uniformBuffer;
        } else if (shaderStage == SDL_GPU_SHADERSTAGE_COMPUTE) {
            commandBuffer->computeUniformBuffers[slotIndex] = uniformBuffer;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Unrecognized shader stage!");
        }
    }

    uniformBuffer->drawOffset = uniformBuffer->writeOffset;

    SDL_memcpy(
        (Uint8 *)uniformBuffer->buffer->mapPointer + uniformBuffer->writeOffset,
        data,
        dataLengthInBytes);

    uniformBuffer->writeOffset += uniformBuffer->currentBlockSize;

    if (shaderStage == SDL_GPU_SHADERSTAGE_VERTEX) {
        commandBuffer->needVertexUniformBufferBind[slotIndex] = SDL_TRUE;
    } else if (shaderStage == SDL_GPU_SHADERSTAGE_FRAGMENT) {
        commandBuffer->needFragmentUniformBufferBind[slotIndex] = SDL_TRUE;
    } else if (shaderStage == SDL_GPU_SHADERSTAGE_COMPUTE) {
        commandBuffer->needComputeUniformBufferBind[slotIndex] = SDL_TRUE;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Unrecognized shader stage!");
    }
}

static void D3D12_BindGraphicsPipeline(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuGraphicsPipeline *graphicsPipeline)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12GraphicsPipeline *pipeline = (D3D12GraphicsPipeline *)graphicsPipeline;
    Uint32 i;

    d3d12CommandBuffer->currentGraphicsPipeline = pipeline;

    // Set the pipeline state
    ID3D12GraphicsCommandList_SetPipelineState(d3d12CommandBuffer->graphicsCommandList, pipeline->pipelineState);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(d3d12CommandBuffer->graphicsCommandList, pipeline->rootSignature->handle);

    ID3D12GraphicsCommandList_IASetPrimitiveTopology(d3d12CommandBuffer->graphicsCommandList, SDLToD3D12_PrimitiveType[pipeline->primitiveType]);

    float blendFactor[4] = {
        pipeline->blendConstants[0],
        pipeline->blendConstants[1],
        pipeline->blendConstants[2],
        pipeline->blendConstants[3]
    };
    ID3D12GraphicsCommandList_OMSetBlendFactor(d3d12CommandBuffer->graphicsCommandList, blendFactor);

    ID3D12GraphicsCommandList_OMSetStencilRef(d3d12CommandBuffer->graphicsCommandList, pipeline->stencilRef);

    // Mark that bindings are needed
    d3d12CommandBuffer->needVertexSamplerBind = SDL_TRUE;
    d3d12CommandBuffer->needVertexStorageTextureBind = SDL_TRUE;
    d3d12CommandBuffer->needVertexStorageBufferBind = SDL_TRUE;
    d3d12CommandBuffer->needFragmentSamplerBind = SDL_TRUE;
    d3d12CommandBuffer->needFragmentStorageTextureBind = SDL_TRUE;
    d3d12CommandBuffer->needFragmentStorageBufferBind = SDL_TRUE;

    for (i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        d3d12CommandBuffer->needVertexUniformBufferBind[i] = SDL_TRUE;
        d3d12CommandBuffer->needFragmentUniformBufferBind[i] = SDL_TRUE;
    }

    for (i = 0; i < pipeline->vertexUniformBufferCount; i += 1) {
        if (d3d12CommandBuffer->vertexUniformBuffers[i] == NULL) {
            d3d12CommandBuffer->vertexUniformBuffers[i] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                d3d12CommandBuffer);
        }
    }

    for (i = 0; i < pipeline->fragmentUniformBufferCount; i += 1) {
        if (d3d12CommandBuffer->fragmentUniformBuffers[i] == NULL) {
            d3d12CommandBuffer->fragmentUniformBuffers[i] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                d3d12CommandBuffer);
        }
    }

    D3D12_INTERNAL_TrackGraphicsPipeline(d3d12CommandBuffer, pipeline);
}

static void D3D12_BindVertexBuffers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstBinding,
    SDL_GpuBufferBinding *pBindings,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_VERTEX_BUFFER_VIEW views[MAX_BUFFER_BINDINGS];

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12Buffer *currentBuffer = ((D3D12BufferContainer *)pBindings[i].buffer)->activeBuffer;
        views[i].BufferLocation = currentBuffer->virtualAddress + pBindings[i].offset;
        views[i].SizeInBytes = currentBuffer->container->size - pBindings[i].offset;
        views[i].StrideInBytes = d3d12CommandBuffer->currentGraphicsPipeline->vertexStrides[i];

        D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, currentBuffer);
    }

    ID3D12GraphicsCommandList_IASetVertexBuffers(
        d3d12CommandBuffer->graphicsCommandList,
        firstBinding,
        bindingCount,
        views);
}

static void D3D12_BindIndexBuffer(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBufferBinding *pBinding,
    SDL_GpuIndexElementSize indexElementSize)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Buffer *buffer = ((D3D12BufferContainer *)pBinding->buffer)->activeBuffer;
    D3D12_INDEX_BUFFER_VIEW view;

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, buffer);

    view.BufferLocation = buffer->virtualAddress + pBinding->offset;
    view.SizeInBytes = buffer->container->size - pBinding->offset;
    view.Format =
        indexElementSize == SDL_GPU_INDEXELEMENTSIZE_16BIT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

    ID3D12GraphicsCommandList_IASetIndexBuffer(
        d3d12CommandBuffer->graphicsCommandList,
        &view);
}

static void D3D12_BindVertexSamplers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuTextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)textureSamplerBindings[i].texture;
        D3D12Sampler *sampler = (D3D12Sampler *)textureSamplerBindings[i].sampler;

        for (Uint32 j = 0; j < container->activeTexture->subresourceCount; j += 1) {
            D3D12_INTERNAL_TrackTextureSubresource(
                d3d12CommandBuffer,
                &container->activeTexture->subresources[j]);
        }

        D3D12_INTERNAL_TrackSampler(
            d3d12CommandBuffer,
            sampler);

        d3d12CommandBuffer->vertexSamplers[firstSlot + i] = sampler;
        d3d12CommandBuffer->vertexSamplerTextures[firstSlot + i] = container->activeTexture;
    }

    d3d12CommandBuffer->needVertexSamplerBind = SDL_TRUE;
}

static void D3D12_BindVertexStorageTextures(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuTextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)storageTextureSlices[i].texture;
        D3D12TextureSubresource *subresource = D3D12_INTERNAL_FetchTextureSubresource(
            container,
            storageTextureSlices[i].layer,
            storageTextureSlices[i].mipLevel);

        D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, subresource);

        d3d12CommandBuffer->vertexStorageTextureSubresources[firstSlot + i] = subresource;
    }

    d3d12CommandBuffer->needVertexStorageTextureBind = SDL_TRUE;
}

static void D3D12_BindVertexStorageBuffers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuBuffer **storageBuffers,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12BufferContainer *container = (D3D12BufferContainer *)storageBuffers[i];

        D3D12_INTERNAL_TrackBuffer(
            d3d12CommandBuffer,
            container->activeBuffer);

        d3d12CommandBuffer->vertexStorageBuffers[firstSlot + i] = container->activeBuffer;
    }

    d3d12CommandBuffer->needVertexStorageBufferBind = SDL_TRUE;
}

static void D3D12_BindFragmentSamplers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuTextureSamplerBinding *textureSamplerBindings,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)textureSamplerBindings[i].texture;
        D3D12Sampler *sampler = (D3D12Sampler *)textureSamplerBindings[i].sampler;

        for (Uint32 j = 0; j < container->activeTexture->subresourceCount; j += 1) {
            D3D12_INTERNAL_TrackTextureSubresource(
                d3d12CommandBuffer,
                &container->activeTexture->subresources[j]);
        }

        D3D12_INTERNAL_TrackSampler(
            d3d12CommandBuffer,
            sampler);

        d3d12CommandBuffer->fragmentSamplers[firstSlot + i] = sampler;
        d3d12CommandBuffer->fragmentSamplerTextures[firstSlot + i] = container->activeTexture;
    }

    d3d12CommandBuffer->needFragmentSamplerBind = SDL_TRUE;
}

static void D3D12_BindFragmentStorageTextures(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuTextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12TextureContainer *container = (D3D12TextureContainer *)storageTextureSlices[i].texture;
        D3D12TextureSubresource *subresource = D3D12_INTERNAL_FetchTextureSubresource(
            container,
            storageTextureSlices[i].layer,
            storageTextureSlices[i].mipLevel);

        D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, subresource);

        d3d12CommandBuffer->fragmentStorageTextureSlices[firstSlot + i] = subresource;
    }

    d3d12CommandBuffer->needFragmentStorageTextureBind = SDL_TRUE;
}

static void D3D12_BindFragmentStorageBuffers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuBuffer **storageBuffers,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        D3D12BufferContainer *container = (D3D12BufferContainer *)storageBuffers[i];

        D3D12_INTERNAL_TrackBuffer(
            d3d12CommandBuffer,
            container->activeBuffer);

        d3d12CommandBuffer->fragmentStorageBuffers[firstSlot + i] = container->activeBuffer;
    }

    d3d12CommandBuffer->needFragmentStorageBufferBind = SDL_TRUE;
}

static void D3D12_PushVertexUniformData(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    D3D12_INTERNAL_PushUniformData(
        d3d12CommandBuffer,
        SDL_GPU_SHADERSTAGE_VERTEX,
        slotIndex,
        data,
        dataLengthInBytes);
}

static void D3D12_PushFragmentUniformData(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    D3D12_INTERNAL_PushUniformData(
        d3d12CommandBuffer,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        slotIndex,
        data,
        dataLengthInBytes);
}

static void D3D12_INTERNAL_WriteGPUDescriptors(
    D3D12CommandBuffer *commandBuffer,
    D3D12_DESCRIPTOR_HEAP_TYPE heapType,
    D3D12_CPU_DESCRIPTOR_HANDLE *resourceDescriptorHandles,
    Uint32 resourceHandleCount,
    D3D12_GPU_DESCRIPTOR_HANDLE *gpuBaseDescriptor)
{
    D3D12DescriptorHeap *heap = commandBuffer->gpuDescriptorHeaps[heapType];
    D3D12_CPU_DESCRIPTOR_HANDLE gpuHeapCpuHandle;

    /* FIXME: need to error on overflow */
    gpuHeapCpuHandle.ptr = heap->descriptorHeapCPUStart.ptr + (heap->currentDescriptorIndex * heap->descriptorSize);
    gpuBaseDescriptor->ptr = heap->descriptorHeapGPUStart.ptr + (heap->currentDescriptorIndex * heap->descriptorSize);

    for (Uint32 i = 0; i < resourceHandleCount; i += 1) {
        ID3D12Device_CopyDescriptorsSimple(
            commandBuffer->renderer->device,
            1,
            gpuHeapCpuHandle,
            resourceDescriptorHandles[i],
            heapType);

        heap->currentDescriptorIndex += 1;
        gpuHeapCpuHandle.ptr += heap->descriptorSize;
    }
}

static void D3D12_INTERNAL_BindGraphicsResources(
    D3D12CommandBuffer *commandBuffer)
{
    D3D12GraphicsPipeline *graphicsPipeline = commandBuffer->currentGraphicsPipeline;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandles[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;

    if (commandBuffer->needVertexSamplerBind) {
        if (graphicsPipeline->vertexSamplerCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->vertexSamplerCount; i += 1) {
                cpuHandles[i] = commandBuffer->vertexSamplers[i]->handle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                cpuHandles,
                graphicsPipeline->vertexSamplerCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->vertexSamplerRootIndex,
                gpuDescriptorHandle);

            for (Uint32 i = 0; i < graphicsPipeline->vertexSamplerCount; i += 1) {
                cpuHandles[i] = commandBuffer->vertexSamplerTextures[i]->srvHandle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->vertexSamplerCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->vertexSamplerTextureRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needVertexSamplerBind = SDL_FALSE;
    }

    if (commandBuffer->needVertexStorageTextureBind) {
        if (graphicsPipeline->vertexStorageTextureCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->vertexStorageTextureCount; i += 1) {
                cpuHandles[i] = commandBuffer->vertexStorageTextureSubresources[i]->srvHandle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->vertexStorageTextureCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->vertexStorageTextureRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needVertexStorageTextureBind = SDL_FALSE;
    }

    if (commandBuffer->needVertexStorageBufferBind) {
        if (graphicsPipeline->vertexStorageBufferCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->vertexStorageBufferCount; i += 1) {
                cpuHandles[i] = commandBuffer->vertexStorageBuffers[i]->srvDescriptor.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->vertexStorageBufferCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->vertexStorageBufferRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needVertexStorageBufferBind = SDL_FALSE;
    }

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        if (commandBuffer->needVertexUniformBufferBind[i]) {
            if (graphicsPipeline->vertexUniformBufferCount > i) {
                ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
                    commandBuffer->graphicsCommandList,
                    graphicsPipeline->rootSignature->vertexUniformBufferRootIndex[i],
                    commandBuffer->vertexUniformBuffers[i]->buffer->virtualAddress + commandBuffer->vertexUniformBuffers[i]->drawOffset);
            }
            commandBuffer->needVertexUniformBufferBind[i] = SDL_FALSE;
        }
    }

    if (commandBuffer->needFragmentSamplerBind) {
        if (graphicsPipeline->fragmentSamplerCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->fragmentSamplerCount; i += 1) {
                cpuHandles[i] = commandBuffer->fragmentSamplers[i]->handle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                cpuHandles,
                graphicsPipeline->fragmentSamplerCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->fragmentSamplerRootIndex,
                gpuDescriptorHandle);

            for (Uint32 i = 0; i < graphicsPipeline->fragmentSamplerCount; i += 1) {
                cpuHandles[i] = commandBuffer->fragmentSamplerTextures[i]->srvHandle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->fragmentSamplerCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->fragmentSamplerTextureRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needFragmentSamplerBind = SDL_FALSE;
    }

    if (commandBuffer->needFragmentStorageTextureBind) {
        if (graphicsPipeline->fragmentStorageTextureCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->fragmentStorageTextureCount; i += 1) {
                cpuHandles[i] = commandBuffer->fragmentStorageTextureSlices[i]->srvHandle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->fragmentStorageTextureCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->fragmentStorageTextureRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needFragmentStorageTextureBind = SDL_FALSE;
    }

    if (commandBuffer->needFragmentStorageBufferBind) {
        if (graphicsPipeline->fragmentStorageBufferCount > 0) {
            for (Uint32 i = 0; i < graphicsPipeline->fragmentStorageBufferCount; i += 1) {
                cpuHandles[i] = commandBuffer->fragmentStorageBuffers[i]->srvDescriptor.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                graphicsPipeline->fragmentStorageBufferCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                graphicsPipeline->rootSignature->fragmentStorageBufferRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needFragmentStorageBufferBind = SDL_FALSE;
    }

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        if (commandBuffer->needFragmentUniformBufferBind[i]) {
            if (graphicsPipeline->fragmentUniformBufferCount > i) {
                ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(
                    commandBuffer->graphicsCommandList,
                    graphicsPipeline->rootSignature->fragmentUniformBufferRootIndex[i],
                    commandBuffer->fragmentUniformBuffers[i]->buffer->virtualAddress + commandBuffer->fragmentUniformBuffers[i]->drawOffset);
            }
            commandBuffer->needFragmentUniformBufferBind[i] = SDL_FALSE;
        }
    }
}

static void D3D12_DrawIndexedPrimitives(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 baseVertex,
    Uint32 startIndex,
    Uint32 vertexCount,
    Uint32 instanceCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_INTERNAL_BindGraphicsResources(d3d12CommandBuffer);

    ID3D12GraphicsCommandList_DrawIndexedInstanced(
        d3d12CommandBuffer->graphicsCommandList,
        vertexCount,
        instanceCount,
        startIndex,
        baseVertex,
        0);
}

static void D3D12_DrawPrimitives(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 vertexStart,
    Uint32 vertexCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_INTERNAL_BindGraphicsResources(d3d12CommandBuffer);

    ID3D12GraphicsCommandList_DrawInstanced(
        d3d12CommandBuffer->graphicsCommandList,
        vertexCount,
        1, /* Instance count */
        vertexStart,
        0 /* Start instance location */
    );
}

static void D3D12_DrawPrimitivesIndirect(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBuffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Buffer *d3d12Buffer = ((D3D12BufferContainer *)buffer)->activeBuffer;

    D3D12_INTERNAL_BindGraphicsResources(d3d12CommandBuffer);

    ID3D12GraphicsCommandList_ExecuteIndirect(
        d3d12CommandBuffer->graphicsCommandList,
        d3d12CommandBuffer->renderer->indirectDrawCommandSignature,
        drawCount,
        d3d12Buffer->handle,
        offsetInBytes,
        NULL,
        0);
}

static void D3D12_DrawIndexedPrimitivesIndirect(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBuffer *buffer,
    Uint32 offsetInBytes,
    Uint32 drawCount,
    Uint32 stride)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Buffer *d3d12Buffer = ((D3D12BufferContainer *)buffer)->activeBuffer;

    D3D12_INTERNAL_BindGraphicsResources(d3d12CommandBuffer);

    ID3D12GraphicsCommandList_ExecuteIndirect(
        d3d12CommandBuffer->graphicsCommandList,
        d3d12CommandBuffer->renderer->indirectIndexedDrawCommandSignature,
        drawCount,
        d3d12Buffer->handle,
        offsetInBytes,
        NULL,
        0);
}

static void D3D12_EndRenderPass(
    SDL_GpuCommandBuffer *commandBuffer)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    Uint32 i;

    for (i = 0; i < d3d12CommandBuffer->colorAttachmentCount; i += 1) {
        D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
            d3d12CommandBuffer,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            d3d12CommandBuffer->colorAttachmentTextureSubresources[i]);

        d3d12CommandBuffer->colorAttachmentTextureSubresources[i] = NULL;
    }
    d3d12CommandBuffer->colorAttachmentCount = 0;

    if (d3d12CommandBuffer->depthStencilTextureSubresource != NULL) {
        D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
            d3d12CommandBuffer,
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            d3d12CommandBuffer->depthStencilTextureSubresource);

        d3d12CommandBuffer->depthStencilTextureSubresource = NULL;
    }

    d3d12CommandBuffer->currentGraphicsPipeline = NULL;

    ID3D12GraphicsCommandList_OMSetRenderTargets(
        d3d12CommandBuffer->graphicsCommandList,
        0,
        NULL,
        SDL_FALSE,
        NULL);
}

/* Compute Pass */

static void D3D12_BeginComputePass(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuStorageTextureReadWriteBinding *storageTextureBindings,
    Uint32 storageTextureBindingCount,
    SDL_GpuStorageBufferReadWriteBinding *storageBufferBindings,
    Uint32 storageBufferBindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    d3d12CommandBuffer->computeReadWriteStorageTextureCount = storageTextureBindingCount;
    d3d12CommandBuffer->computeReadWriteStorageBufferCount = storageBufferBindingCount;

    /* Read-write resources will be actually bound in BindComputePipeline
     * after the root signature is set.
     */
    if (storageTextureBindingCount > 0) {
        for (Uint32 i = 0; i < storageTextureBindingCount; i += 1) {
            D3D12TextureContainer *container = (D3D12TextureContainer *)storageTextureBindings[i].textureSlice.texture;
            if (!(container->header.info.usageFlags & SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT)) {
                SDL_LogError(SDL_LOG_CATEGORY_GPU, "Attempted to bind read-only texture as compute write texture");
            }

            D3D12TextureSubresource *subresource = D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
                d3d12CommandBuffer,
                container,
                storageTextureBindings[i].textureSlice.layer,
                storageTextureBindings[i].textureSlice.mipLevel,
                storageTextureBindings[i].cycle,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            d3d12CommandBuffer->computeReadWriteStorageTextures[i] = subresource;

            D3D12_INTERNAL_TrackTextureSubresource(
                d3d12CommandBuffer,
                subresource);
        }
    }

    if (storageBufferBindingCount > 0) {
        for (Uint32 i = 0; i < storageBufferBindingCount; i += 1) {
            D3D12BufferContainer *container = (D3D12BufferContainer *)storageBufferBindings[i].buffer;
            if (!(container->usageFlags & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE_BIT)) {
                SDL_LogError(SDL_LOG_CATEGORY_GPU, "Attempted to bind read-only texture as compute write texture");
            }
            D3D12Buffer *buffer = D3D12_INTERNAL_PrepareBufferForWrite(
                d3d12CommandBuffer,
                container,
                storageBufferBindings[i].cycle,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            d3d12CommandBuffer->computeReadWriteStorageBuffers[i] = buffer;

            D3D12_INTERNAL_TrackBuffer(
                d3d12CommandBuffer,
                buffer);
        }
    }
}

static void D3D12_BindComputePipeline(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuComputePipeline *computePipeline)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12ComputePipeline *pipeline = (D3D12ComputePipeline *)computePipeline;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandles[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;

    ID3D12GraphicsCommandList_SetPipelineState(
        d3d12CommandBuffer->graphicsCommandList,
        pipeline->pipelineState);

    ID3D12GraphicsCommandList_SetComputeRootSignature(
        d3d12CommandBuffer->graphicsCommandList,
        pipeline->rootSignature->handle);

    d3d12CommandBuffer->currentComputePipeline = pipeline;

    d3d12CommandBuffer->needComputeReadOnlyStorageTextureBind = SDL_TRUE;
    d3d12CommandBuffer->needComputeReadOnlyStorageBufferBind = SDL_TRUE;

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        d3d12CommandBuffer->needComputeUniformBufferBind[i] = SDL_TRUE;
    }

    for (Uint32 i = 0; i < pipeline->uniformBufferCount; i += 1) {
        if (d3d12CommandBuffer->computeUniformBuffers[i] == NULL) {
            d3d12CommandBuffer->computeUniformBuffers[i] = D3D12_INTERNAL_AcquireUniformBufferFromPool(
                d3d12CommandBuffer);
        }
    }

    D3D12_INTERNAL_TrackComputePipeline(d3d12CommandBuffer, pipeline);

    /* Bind read-write resources after setting root signature */
    if (d3d12CommandBuffer->computeReadWriteStorageTextureCount > 0) {
        for (Uint32 i = 0; i < d3d12CommandBuffer->computeReadWriteStorageTextureCount; i += 1) {
            cpuHandles[i] = d3d12CommandBuffer->computeReadWriteStorageTextures[i]->uavHandle.cpuHandle;
        }

        D3D12_INTERNAL_WriteGPUDescriptors(
            d3d12CommandBuffer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            cpuHandles,
            d3d12CommandBuffer->computeReadWriteStorageTextureCount,
            &gpuDescriptorHandle);

        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
            d3d12CommandBuffer->graphicsCommandList,
            d3d12CommandBuffer->currentComputePipeline->rootSignature->readWriteStorageTextureRootIndex,
            gpuDescriptorHandle);
    }

    if (d3d12CommandBuffer->computeReadWriteStorageBufferCount > 0) {
        for (Uint32 i = 0; i < d3d12CommandBuffer->computeReadWriteStorageBufferCount; i += 1) {
            cpuHandles[i] = d3d12CommandBuffer->computeReadWriteStorageBuffers[i]->uavDescriptor.cpuHandle;
        }

        D3D12_INTERNAL_WriteGPUDescriptors(
            d3d12CommandBuffer,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            cpuHandles,
            d3d12CommandBuffer->computeReadWriteStorageBufferCount,
            &gpuDescriptorHandle);

        ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
            d3d12CommandBuffer->graphicsCommandList,
            d3d12CommandBuffer->currentComputePipeline->rootSignature->readWriteStorageBufferRootIndex,
            gpuDescriptorHandle);
    }
}

static void D3D12_BindComputeStorageTextures(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuTextureSlice *storageTextureSlices,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        if (d3d12CommandBuffer->computeReadOnlyStorageTextures[firstSlot + i] != NULL) {
            D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                d3d12CommandBuffer->computeReadOnlyStorageTextures[firstSlot + i]);
        }

        D3D12TextureContainer *container = (D3D12TextureContainer *)storageTextureSlices[i].texture;
        D3D12TextureSubresource *subresource = D3D12_INTERNAL_FetchTextureSubresource(
            container,
            storageTextureSlices[i].layer,
            storageTextureSlices[i].mipLevel);

        d3d12CommandBuffer->computeReadOnlyStorageTextures[firstSlot + i] = subresource;

        D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
            d3d12CommandBuffer,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            subresource);

        D3D12_INTERNAL_TrackTextureSubresource(
            d3d12CommandBuffer,
            subresource);
    }

    d3d12CommandBuffer->needComputeReadOnlyStorageTextureBind = SDL_TRUE;
}

static void D3D12_BindComputeStorageBuffers(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 firstSlot,
    SDL_GpuBuffer **storageBuffers,
    Uint32 bindingCount)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < bindingCount; i += 1) {
        if (d3d12CommandBuffer->computeReadOnlyStorageBuffers[firstSlot + i] != NULL) {
            D3D12_INTERNAL_BufferTransitionToDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                d3d12CommandBuffer->computeReadOnlyStorageBuffers[firstSlot + i]);
        }

        D3D12BufferContainer *container = (D3D12BufferContainer *)storageBuffers[i];
        D3D12Buffer *buffer = container->activeBuffer;

        d3d12CommandBuffer->computeReadOnlyStorageBuffers[firstSlot + i] = buffer;

        D3D12_INTERNAL_BufferTransitionFromDefaultUsage(
            d3d12CommandBuffer,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            buffer);

        D3D12_INTERNAL_TrackBuffer(
            d3d12CommandBuffer,
            buffer);
    }

    d3d12CommandBuffer->needComputeReadOnlyStorageBufferBind = SDL_TRUE;
}

static void D3D12_PushComputeUniformData(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 slotIndex,
    const void *data,
    Uint32 dataLengthInBytes)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    D3D12_INTERNAL_PushUniformData(
        d3d12CommandBuffer,
        SDL_GPU_SHADERSTAGE_COMPUTE,
        slotIndex,
        data,
        dataLengthInBytes);
}

static void D3D12_INTERNAL_BindComputeResources(
    D3D12CommandBuffer *commandBuffer)
{
    D3D12ComputePipeline *computePipeline = commandBuffer->currentComputePipeline;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandles[MAX_TEXTURE_SAMPLERS_PER_STAGE];
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;

    if (commandBuffer->needComputeReadOnlyStorageTextureBind) {
        if (computePipeline->readOnlyStorageTextureCount > 0) {
            for (Uint32 i = 0; i < computePipeline->readOnlyStorageTextureCount; i += 1) {
                cpuHandles[i] = commandBuffer->computeReadOnlyStorageTextures[i]->srvHandle.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                computePipeline->readOnlyStorageTextureCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                computePipeline->rootSignature->readOnlyStorageTextureRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needComputeReadOnlyStorageTextureBind = SDL_FALSE;
    }

    if (commandBuffer->needComputeReadOnlyStorageBufferBind) {
        if (computePipeline->readOnlyStorageBufferCount > 0) {
            for (Uint32 i = 0; i < computePipeline->readOnlyStorageBufferCount; i += 1) {
                cpuHandles[i] = commandBuffer->computeReadOnlyStorageBuffers[i]->srvDescriptor.cpuHandle;
            }

            D3D12_INTERNAL_WriteGPUDescriptors(
                commandBuffer,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                cpuHandles,
                computePipeline->readOnlyStorageBufferCount,
                &gpuDescriptorHandle);

            ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(
                commandBuffer->graphicsCommandList,
                computePipeline->rootSignature->readOnlyStorageBufferRootIndex,
                gpuDescriptorHandle);
        }
        commandBuffer->needComputeReadOnlyStorageBufferBind = SDL_FALSE;
    }

    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        if (commandBuffer->needComputeUniformBufferBind[i]) {
            if (computePipeline->uniformBufferCount > i) {
                ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(
                    commandBuffer->graphicsCommandList,
                    computePipeline->rootSignature->uniformBufferRootIndex[i],
                    commandBuffer->computeUniformBuffers[i]->buffer->virtualAddress + commandBuffer->computeUniformBuffers[i]->drawOffset);
            }
        }
        commandBuffer->needComputeUniformBufferBind[i] = SDL_FALSE;
    }
}

static void D3D12_DispatchCompute(
    SDL_GpuCommandBuffer *commandBuffer,
    Uint32 groupCountX,
    Uint32 groupCountY,
    Uint32 groupCountZ)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    D3D12_INTERNAL_BindComputeResources(d3d12CommandBuffer);
    ID3D12GraphicsCommandList_Dispatch(
        d3d12CommandBuffer->graphicsCommandList,
        groupCountX,
        groupCountY,
        groupCountZ);
}

static void D3D12_DispatchComputeIndirect(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBuffer *buffer,
    Uint32 offsetInBytes)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Buffer *d3d12Buffer = ((D3D12BufferContainer *)buffer)->activeBuffer;

    D3D12_INTERNAL_BindComputeResources(d3d12CommandBuffer);
    ID3D12GraphicsCommandList_ExecuteIndirect(
        d3d12CommandBuffer->graphicsCommandList,
        d3d12CommandBuffer->renderer->indirectDispatchCommandSignature,
        1,
        d3d12Buffer->handle,
        offsetInBytes,
        NULL,
        0);
}

static void D3D12_EndComputePass(
    SDL_GpuCommandBuffer *commandBuffer)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;

    for (Uint32 i = 0; i < MAX_STORAGE_TEXTURES_PER_STAGE; i += 1) {
        if (d3d12CommandBuffer->computeReadWriteStorageTextures[i]) {
            D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                d3d12CommandBuffer->computeReadWriteStorageTextures[i]);

            d3d12CommandBuffer->computeReadWriteStorageTextures[i] = NULL;
        }
    }

    for (Uint32 i = 0; i < MAX_STORAGE_BUFFERS_PER_STAGE; i += 1) {
        if (d3d12CommandBuffer->computeReadWriteStorageBuffers[i]) {
            D3D12_INTERNAL_BufferTransitionToDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                d3d12CommandBuffer->computeReadWriteStorageBuffers[i]);

            d3d12CommandBuffer->computeReadWriteStorageBuffers[i] = NULL;
        }
    }

    for (Uint32 i = 0; i < MAX_STORAGE_TEXTURES_PER_STAGE; i += 1) {
        if (d3d12CommandBuffer->computeReadOnlyStorageTextures[i]) {
            D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                d3d12CommandBuffer->computeReadOnlyStorageTextures[i]);

            d3d12CommandBuffer->computeReadOnlyStorageTextures[i] = NULL;
        }
    }

    for (Uint32 i = 0; i < MAX_STORAGE_BUFFERS_PER_STAGE; i += 1) {
        if (d3d12CommandBuffer->computeReadOnlyStorageBuffers[i]) {
            D3D12_INTERNAL_BufferTransitionToDefaultUsage(
                d3d12CommandBuffer,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                d3d12CommandBuffer->computeReadOnlyStorageBuffers[i]);

            d3d12CommandBuffer->computeReadOnlyStorageBuffers[i] = NULL;
        }
    }

    d3d12CommandBuffer->currentComputePipeline = NULL;
}

/* TransferBuffer Data */

static void D3D12_MapTransferBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuTransferBuffer *transferBuffer,
    SDL_bool cycle,
    void **ppData)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12BufferContainer *container = (D3D12BufferContainer *)transferBuffer;

    if (
        cycle &&
        SDL_AtomicGet(&container->activeBuffer->referenceCount) > 0) {
        D3D12_INTERNAL_CycleActiveBuffer(
            renderer,
            container);
    }

    /* Upload buffers are persistently mapped, download buffers are not */
    if (container->type == D3D12_BUFFER_TYPE_UPLOAD) {
        *ppData = container->activeBuffer->mapPointer;
    } else {
        ID3D12Resource_Map(
            container->activeBuffer->handle,
            0,
            NULL,
            (void **)ppData);
    }
}

static void D3D12_UnmapTransferBuffer(
    SDL_GpuRenderer *driverData,
    SDL_GpuTransferBuffer *transferBuffer)
{
    (void)driverData;
    D3D12BufferContainer *container = (D3D12BufferContainer *)transferBuffer;

    /* Upload buffers are persistently mapped, download buffers are not */
    if (container->type == D3D12_BUFFER_TYPE_DOWNLOAD) {
        ID3D12Resource_Unmap(
            container->activeBuffer->handle,
            0,
            NULL);
    }
}

/* Copy Pass */

static void D3D12_BeginCopyPass(
    SDL_GpuCommandBuffer *commandBuffer)
{
    /* no-op */
    (void)commandBuffer;
}

static void D3D12_UploadToTexture(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTextureTransferInfo *source,
    SDL_GpuTextureRegion *destination,
    SDL_bool cycle)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12BufferContainer *transferBufferContainer = (D3D12BufferContainer *)source->transferBuffer;
    D3D12TextureContainer *textureContainer = (D3D12TextureContainer *)destination->textureSlice.texture;
    D3D12Buffer *temporaryBuffer = NULL;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation;
    D3D12_TEXTURE_COPY_LOCATION destinationLocation;
    Uint32 pixelsPerRow = (Uint32)source->imagePitch;
    Uint32 rowPitch;
    Uint32 alignedRowPitch;
    Uint32 rowsPerSlice = (Uint32)source->imageHeight;
    Uint32 bytesPerSlice;
    SDL_bool needsRealignment;
    SDL_bool needsPlacementCopy;

    /* Note that the transfer buffer does not need a barrier, as it is synced by the client. */

    D3D12TextureSubresource *textureSubresource = D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
        d3d12CommandBuffer,
        textureContainer,
        destination->textureSlice.layer,
        destination->textureSlice.mipLevel,
        cycle,
        D3D12_RESOURCE_STATE_COPY_DEST);

    /* D3D12 requires texture data row pitch to be 256 byte aligned, which is obviously insane.
     * Instead of exposing that restriction to the client, which is a huge rake to step on,
     * and a restriction that no other backend requires, we're going to copy data to a temporary buffer,
     * copy THAT data to the texture, and then get rid of the temporary buffer ASAP.
     * If we're lucky and the row pitch and depth pitch are already aligned, we can skip all of that.
     *
     * D3D12 also requires offsets to be 512 byte aligned. We'll fix that for the client and warn them as well.
     *
     * And just for some extra fun, D3D12 doesn't actually support depth pitch, so we have to realign that too!
     */

    if (pixelsPerRow == 0) {
        pixelsPerRow = destination->w;
    }

    rowPitch = BytesPerRow(pixelsPerRow, textureContainer->header.info.format);

    if (rowsPerSlice == 0) {
        rowsPerSlice = destination->h;
    }

    bytesPerSlice = rowsPerSlice * rowPitch;

    alignedRowPitch = D3D12_INTERNAL_Align(rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    needsRealignment = rowsPerSlice != destination->h || rowPitch != alignedRowPitch;
    needsPlacementCopy = source->offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0;

    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    sourceLocation.PlacedFootprint.Footprint.Format = SDLToD3D12_TextureFormat[textureContainer->header.info.format];
    sourceLocation.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

    destinationLocation.pResource = textureContainer->activeTexture->resource;
    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = textureSubresource->index;

    if (needsRealignment) {
        temporaryBuffer = D3D12_INTERNAL_CreateBuffer(
            d3d12CommandBuffer->renderer,
            0,
            alignedRowPitch * destination->h * destination->d,
            D3D12_BUFFER_TYPE_UPLOAD);

        if (!temporaryBuffer) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create temporary upload buffer.");
            return;
        }

        sourceLocation.pResource = temporaryBuffer->handle;

        for (int sliceIndex = 0; sliceIndex < destination->d; sliceIndex += 1) {
            for (Uint32 rowIndex = 0; rowIndex < rowsPerSlice; rowIndex += 1) {
                SDL_memcpy(
                    temporaryBuffer->mapPointer + (sliceIndex * rowsPerSlice) + (rowIndex * alignedRowPitch),
                    transferBufferContainer->activeBuffer->mapPointer + source->offset + (sliceIndex * bytesPerSlice) + (rowIndex * rowPitch),
                    alignedRowPitch);
            }

            sourceLocation.PlacedFootprint.Footprint.Width = destination->w;
            sourceLocation.PlacedFootprint.Footprint.Height = rowsPerSlice;
            sourceLocation.PlacedFootprint.Footprint.Depth = 1;
            sourceLocation.PlacedFootprint.Offset = (sliceIndex * bytesPerSlice);

            ID3D12GraphicsCommandList_CopyTextureRegion(
                d3d12CommandBuffer->graphicsCommandList,
                &destinationLocation,
                destination->x,
                destination->y,
                sliceIndex,
                &sourceLocation,
                NULL);
        }

        D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, temporaryBuffer);
        D3D12_INTERNAL_ReleaseBuffer(
            d3d12CommandBuffer->renderer,
            temporaryBuffer);
    } else if (needsPlacementCopy) {
        temporaryBuffer = D3D12_INTERNAL_CreateBuffer(
            d3d12CommandBuffer->renderer,
            0,
            alignedRowPitch * destination->h * destination->d,
            D3D12_BUFFER_TYPE_UPLOAD);

        if (!temporaryBuffer) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create temporary upload buffer.");
            return;
        }

        SDL_memcpy(
            temporaryBuffer->mapPointer,
            transferBufferContainer->activeBuffer->mapPointer + source->offset,
            alignedRowPitch * destination->h * destination->d);

        sourceLocation.pResource = temporaryBuffer->handle;
        sourceLocation.PlacedFootprint.Offset = 0;
        sourceLocation.PlacedFootprint.Footprint.Width = destination->w;
        sourceLocation.PlacedFootprint.Footprint.Height = destination->h;
        sourceLocation.PlacedFootprint.Footprint.Depth = 1;

        ID3D12GraphicsCommandList_CopyTextureRegion(
            d3d12CommandBuffer->graphicsCommandList,
            &destinationLocation,
            destination->x,
            destination->y,
            destination->z,
            &sourceLocation,
            NULL);

        D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, temporaryBuffer);
        D3D12_INTERNAL_ReleaseBuffer(
            d3d12CommandBuffer->renderer,
            temporaryBuffer);

        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Texture upload offset not aligned to 512 bytes! This is suboptimal on D3D12!");
    } else {
        sourceLocation.pResource = transferBufferContainer->activeBuffer->handle;
        sourceLocation.PlacedFootprint.Offset = source->offset;
        sourceLocation.PlacedFootprint.Footprint.Width = destination->w;
        sourceLocation.PlacedFootprint.Footprint.Height = destination->h;
        sourceLocation.PlacedFootprint.Footprint.Depth = destination->d;

        ID3D12GraphicsCommandList_CopyTextureRegion(
            d3d12CommandBuffer->graphicsCommandList,
            &destinationLocation,
            destination->x,
            destination->y,
            destination->z,
            &sourceLocation,
            NULL);
    }

    D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        textureSubresource);

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, transferBufferContainer->activeBuffer);
    D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, textureSubresource);
}

static void D3D12_UploadToBuffer(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTransferBufferLocation *source,
    SDL_GpuBufferRegion *destination,
    SDL_bool cycle)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12BufferContainer *transferBufferContainer = (D3D12BufferContainer *)source->transferBuffer;
    D3D12BufferContainer *bufferContainer = (D3D12BufferContainer *)destination->buffer;

    /* The transfer buffer does not need a barrier, it is synced by the client. */

    D3D12Buffer *buffer = D3D12_INTERNAL_PrepareBufferForWrite(
        d3d12CommandBuffer,
        bufferContainer,
        cycle,
        D3D12_RESOURCE_STATE_COPY_DEST);

    ID3D12GraphicsCommandList_CopyBufferRegion(
        d3d12CommandBuffer->graphicsCommandList,
        buffer->handle,
        destination->offset,
        transferBufferContainer->activeBuffer->handle,
        source->offset,
        destination->size);

    D3D12_INTERNAL_BufferTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        buffer);

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, transferBufferContainer->activeBuffer);
    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, buffer);
}

static void D3D12_CopyTextureToTexture(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTextureLocation *source,
    SDL_GpuTextureLocation *destination,
    Uint32 w,
    Uint32 h,
    Uint32 d,
    SDL_bool cycle)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12TextureContainer *sourceContainer = (D3D12TextureContainer *)source->textureSlice.texture;
    D3D12TextureContainer *destinationContainer = (D3D12TextureContainer *)destination->textureSlice.texture;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation;
    D3D12_TEXTURE_COPY_LOCATION destinationLocation;

    D3D12TextureSubresource *sourceSubresource = D3D12_INTERNAL_FetchTextureSubresource(
        sourceContainer,
        source->textureSlice.layer,
        source->textureSlice.mipLevel);

    D3D12TextureSubresource *destinationSubresource = D3D12_INTERNAL_PrepareTextureSubresourceForWrite(
        d3d12CommandBuffer,
        destinationContainer,
        destination->textureSlice.layer,
        destination->textureSlice.mipLevel,
        cycle,
        D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceSubresource);

    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sourceLocation.SubresourceIndex = sourceSubresource->index;
    sourceLocation.pResource = sourceSubresource->parent->resource;

    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destinationLocation.SubresourceIndex = destinationSubresource->index;
    destinationLocation.pResource = destinationSubresource->parent->resource;

    D3D12_BOX sourceBox = { source->x, source->y, source->z, source->x + w, source->y + h, source->z + d };

    ID3D12GraphicsCommandList_CopyTextureRegion(
        d3d12CommandBuffer->graphicsCommandList,
        &destinationLocation,
        destination->x,
        destination->y,
        destination->z,
        &sourceLocation,
        &sourceBox);

    D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceSubresource);

    D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        destinationSubresource);

    D3D12_INTERNAL_TrackTextureSubresource(
        d3d12CommandBuffer,
        sourceSubresource);

    D3D12_INTERNAL_TrackTextureSubresource(
        d3d12CommandBuffer,
        destinationSubresource);
}

static void D3D12_CopyBufferToBuffer(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBufferLocation *source,
    SDL_GpuBufferLocation *destination,
    Uint32 size,
    SDL_bool cycle)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12BufferContainer *sourceContainer = (D3D12BufferContainer *)source->buffer;
    D3D12BufferContainer *destinationContainer = (D3D12BufferContainer *)destination->buffer;

    D3D12Buffer *sourceBuffer = sourceContainer->activeBuffer;
    D3D12Buffer *destinationBuffer = D3D12_INTERNAL_PrepareBufferForWrite(
        d3d12CommandBuffer,
        destinationContainer,
        cycle,
        D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_INTERNAL_BufferTransitionFromDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceBuffer);

    ID3D12GraphicsCommandList_CopyBufferRegion(
        d3d12CommandBuffer->graphicsCommandList,
        destinationBuffer->handle,
        destination->offset,
        sourceBuffer->handle,
        source->offset,
        size);

    D3D12_INTERNAL_BufferTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceBuffer);

    D3D12_INTERNAL_BufferTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_DEST,
        destinationBuffer);

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, sourceBuffer);
    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, destinationBuffer);
}

static void D3D12_DownloadFromTexture(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTextureRegion *source,
    SDL_GpuTextureTransferInfo *destination)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation;
    D3D12_TEXTURE_COPY_LOCATION destinationLocation;
    Uint32 pixelsPerRow = destination->imagePitch;
    Uint32 rowPitch;
    Uint32 alignedRowPitch;
    Uint32 rowsPerSlice = destination->imageHeight;
    SDL_bool needsRealignment;
    SDL_bool needsPlacementCopy;
    D3D12TextureDownload *textureDownload = NULL;
    D3D12TextureContainer *sourceContainer = (D3D12TextureContainer *)source->textureSlice.texture;
    D3D12TextureSubresource *sourceSubresource = D3D12_INTERNAL_FetchTextureSubresource(
        sourceContainer,
        source->textureSlice.layer,
        source->textureSlice.mipLevel);
    D3D12BufferContainer *destinationContainer = (D3D12BufferContainer *)destination->transferBuffer;
    D3D12Buffer *destinationBuffer = destinationContainer->activeBuffer;

    /* D3D12 requires texture data row pitch to be 256 byte aligned, which is obviously insane.
     * Instead of exposing that restriction to the client, which is a huge rake to step on,
     * and a restriction that no other backend requires, we're going to copy data to a temporary buffer,
     * copy THAT data to the texture, and then get rid of the temporary buffer ASAP.
     * If we're lucky and the row pitch and depth pitch are already aligned, we can skip all of that.
     *
     * D3D12 also requires offsets to be 512 byte aligned. We'll fix that for the client and warn them as well.
     *
     * And just for some extra fun, D3D12 doesn't actually support depth pitch, so we have to realign that too!
     *
     * Since this is an async download we have to do all these fixups after the command is finished,
     * so we'll cache the metadata similar to D3D11 and map and copy it when the command buffer is cleaned.
     */

    if (pixelsPerRow == 0) {
        pixelsPerRow = source->w;
    }

    rowPitch = BytesPerRow(pixelsPerRow, sourceContainer->header.info.format);

    if (rowsPerSlice == 0) {
        rowsPerSlice = source->h;
    }

    alignedRowPitch = D3D12_INTERNAL_Align(rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    needsRealignment = rowsPerSlice != source->h || rowPitch != alignedRowPitch;
    needsPlacementCopy = destination->offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0;

    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sourceLocation.SubresourceIndex = sourceSubresource->index;
    sourceLocation.pResource = sourceSubresource->parent->resource;

    D3D12_BOX sourceBox = { source->x, source->y, source->z, source->x + source->w, source->y + rowsPerSlice, source->z + source->d };

    destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destinationLocation.PlacedFootprint.Footprint.Format = SDLToD3D12_TextureFormat[sourceContainer->header.info.format];
    destinationLocation.PlacedFootprint.Footprint.Width = source->w;
    destinationLocation.PlacedFootprint.Footprint.Height = rowsPerSlice;
    destinationLocation.PlacedFootprint.Footprint.Depth = source->d;
    destinationLocation.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

    if (needsRealignment || needsPlacementCopy) {
        textureDownload = SDL_malloc(sizeof(D3D12TextureDownload));

        if (!textureDownload) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create texture download structure!");
            return;
        }

        textureDownload->temporaryBuffer = D3D12_INTERNAL_CreateBuffer(
            d3d12CommandBuffer->renderer,
            0,
            alignedRowPitch * rowsPerSlice * source->d,
            D3D12_BUFFER_TYPE_DOWNLOAD);

        if (!textureDownload->temporaryBuffer) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create temporary download buffer!");
            SDL_free(textureDownload);
            return;
        }

        textureDownload->destinationBuffer = destinationBuffer;
        textureDownload->bufferOffset = destination->offset;
        textureDownload->width = source->w;
        textureDownload->height = rowsPerSlice;
        textureDownload->depth = source->d;
        textureDownload->bytesPerRow = rowPitch;
        textureDownload->bytesPerDepthSlice = rowPitch * rowsPerSlice;
        textureDownload->alignedBytesPerRow = alignedRowPitch;

        destinationLocation.pResource = textureDownload->temporaryBuffer->handle;
        destinationLocation.PlacedFootprint.Offset = 0;
    } else {
        destinationLocation.pResource = destinationBuffer->handle;
        destinationLocation.PlacedFootprint.Offset = destination->offset;
    }

    D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceSubresource);

    ID3D12GraphicsCommandList_CopyTextureRegion(
        d3d12CommandBuffer->graphicsCommandList,
        &destinationLocation,
        0,
        0,
        0,
        &sourceLocation,
        &sourceBox);

    D3D12_INTERNAL_TextureSubresourceTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceSubresource);

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, destinationBuffer);
    D3D12_INTERNAL_TrackTextureSubresource(d3d12CommandBuffer, sourceSubresource);

    if (textureDownload != NULL) {
        D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, textureDownload->temporaryBuffer);

        if (d3d12CommandBuffer->textureDownloadCount >= d3d12CommandBuffer->textureDownloadCapacity) {
            d3d12CommandBuffer->textureDownloadCapacity *= 2;
            d3d12CommandBuffer->textureDownloads = SDL_realloc(
                d3d12CommandBuffer->textureDownloads,
                d3d12CommandBuffer->textureDownloadCapacity * sizeof(D3D12TextureDownload *));
        }

        d3d12CommandBuffer->textureDownloads[d3d12CommandBuffer->textureDownloadCount] = textureDownload;
        d3d12CommandBuffer->textureDownloadCount += 1;

        D3D12_INTERNAL_ReleaseBuffer(d3d12CommandBuffer->renderer, textureDownload->temporaryBuffer);
    }
}

static void D3D12_DownloadFromBuffer(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuBufferRegion *source,
    SDL_GpuTransferBufferLocation *destination)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12BufferContainer *sourceContainer = (D3D12BufferContainer *)source->buffer;
    D3D12BufferContainer *destinationContainer = (D3D12BufferContainer *)destination->transferBuffer;

    D3D12Buffer *sourceBuffer = sourceContainer->activeBuffer;
    D3D12_INTERNAL_BufferTransitionFromDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceBuffer);

    D3D12Buffer *destinationBuffer = destinationContainer->activeBuffer;

    ID3D12GraphicsCommandList_CopyBufferRegion(
        d3d12CommandBuffer->graphicsCommandList,
        destinationBuffer->handle,
        destination->offset,
        sourceBuffer->handle,
        source->offset,
        source->size);

    D3D12_INTERNAL_BufferTransitionToDefaultUsage(
        d3d12CommandBuffer,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        sourceBuffer);

    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, sourceBuffer);
    D3D12_INTERNAL_TrackBuffer(d3d12CommandBuffer, destinationBuffer);
}

static void D3D12_EndCopyPass(
    SDL_GpuCommandBuffer *commandBuffer)
{
    /* no-op */
    (void)commandBuffer;
}

static void D3D12_GenerateMipmaps(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTexture *texture) { SDL_assert(SDL_FALSE); }

static void D3D12_Blit(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_GpuTextureRegion *source,
    SDL_GpuTextureRegion *destination,
    SDL_GpuFilter filterMode,
    SDL_bool cycle)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Renderer *renderer = d3d12CommandBuffer->renderer;
    D3D12TextureContainer *sourceTextureContainer = (D3D12TextureContainer *)source->textureSlice.texture;
    D3D12TextureContainer *destinationTextureContainer = (D3D12TextureContainer *)destination->textureSlice.texture;
    SDL_GpuColorAttachmentInfo colorAttachmentInfo;
    SDL_GpuViewport viewport;
    SDL_GpuTextureSamplerBinding textureSamplerBinding;
    BlitFragmentUniforms blitFragmentUniforms;
    SDL_GpuTextureCreateInfo *sourceTextureCreateInfo = &sourceTextureContainer->header.info;
    SDL_GpuTextureCreateInfo *destinationTextureCreateInfo = &destinationTextureContainer->header.info;

    /* Unused */
    colorAttachmentInfo.clearColor.r = 0;
    colorAttachmentInfo.clearColor.g = 0;
    colorAttachmentInfo.clearColor.b = 0;
    colorAttachmentInfo.clearColor.a = 0;

    /* If the entire destination is blitted, we don't have to load */
    if (
        destinationTextureCreateInfo->layerCount == 1 &&
        destinationTextureCreateInfo->levelCount == 1 &&
        destination->w == destinationTextureCreateInfo->width &&
        destination->h == destinationTextureCreateInfo->height &&
        destination->d == destinationTextureCreateInfo->depth) {
        colorAttachmentInfo.loadOp = SDL_GPU_LOADOP_DONT_CARE;
    } else {
        colorAttachmentInfo.loadOp = SDL_GPU_LOADOP_LOAD;
    }

    colorAttachmentInfo.storeOp = SDL_GPU_STOREOP_STORE;

    colorAttachmentInfo.textureSlice = destination->textureSlice;
    colorAttachmentInfo.cycle = cycle;

    D3D12_BeginRenderPass(
        commandBuffer,
        &colorAttachmentInfo,
        1,
        NULL);

    viewport.x = (float)destination->x;
    viewport.y = (float)destination->y;
    viewport.w = (float)destination->w;
    viewport.h = (float)destination->h;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    D3D12_SetViewport(
        commandBuffer,
        &viewport);

    if (
        sourceTextureCreateInfo->layerCount == 1 &&
        sourceTextureCreateInfo->depth == 1) {
        /* 2D source */
        D3D12_BindGraphicsPipeline(
            commandBuffer,
            renderer->blitFrom2DPipeline);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "3D blit source not implemented!");
        return;
    }

    textureSamplerBinding.texture = source->textureSlice.texture;
    textureSamplerBinding.sampler =
        filterMode == SDL_GPU_FILTER_NEAREST ? renderer->blitNearestSampler : renderer->blitLinearSampler;

    D3D12_BindFragmentSamplers(
        commandBuffer,
        0,
        &textureSamplerBinding,
        1);

    blitFragmentUniforms.left = (float)source->x / sourceTextureCreateInfo->width;
    blitFragmentUniforms.top = (float)source->y / sourceTextureCreateInfo->height;
    blitFragmentUniforms.width = (float)source->w / sourceTextureCreateInfo->width;
    blitFragmentUniforms.height = (float)source->h / sourceTextureCreateInfo->height;

    D3D12_PushFragmentUniformData(
        commandBuffer,
        0,
        &blitFragmentUniforms,
        sizeof(BlitFragmentUniforms));

    D3D12_DrawPrimitives(commandBuffer, 0, 3);
    D3D12_EndRenderPass(commandBuffer);
}

/* Submission/Presentation */

static SDL_bool D3D12_SupportsSwapchainComposition(
    SDL_GpuRenderer *driverData,
    SDL_Window *window,
    SDL_GpuSwapchainComposition swapchainComposition)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    DXGI_FORMAT format;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport;
    HRESULT res;

    format = SwapchainCompositionToTextureFormat[swapchainComposition];

    formatSupport.Format = format;
    res = ID3D12Device_CheckFeatureSupport(
        renderer->device,
        D3D12_FEATURE_FORMAT_SUPPORT,
        &formatSupport,
        sizeof(formatSupport));
    if (FAILED(res)) {
        /* Format is apparently unknown */
        return SDL_FALSE;
    }

    return (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_DISPLAY) != 0;
}

static SDL_bool D3D12_SupportsPresentMode(
    SDL_GpuRenderer *driverData,
    SDL_Window *window,
    SDL_GpuPresentMode presentMode)
{
    (void)driverData;
    (void)window;

    SDL_bool result = SDL_FALSE;
    switch (presentMode) {
    case SDL_GPU_PRESENTMODE_IMMEDIATE:
    case SDL_GPU_PRESENTMODE_VSYNC:
    case SDL_GPU_PRESENTMODE_MAILBOX:
        result = SDL_TRUE;
        break;
    default:
        SDL_assert(!"Unrecognized present mode");
        result = SDL_FALSE;
        break;
    }
    return result;
}

static D3D12WindowData *D3D12_INTERNAL_FetchWindowData(
    SDL_Window *window)
{
    SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    return (D3D12WindowData *)SDL_GetPointerProperty(properties, WINDOW_PROPERTY_DATA, NULL);
}

static SDL_bool D3D12_INTERNAL_InitializeSwapchainTexture(
    D3D12Renderer *renderer,
    IDXGISwapChain3 *swapchain,
    DXGI_FORMAT swapchainFormat,
    DXGI_FORMAT rtvFormat,
    Uint32 index,
    D3D12TextureContainer *pTextureContainer)
{
    D3D12Texture *pTexture;
    ID3D12Resource *swapchainTexture;
    D3D12_RESOURCE_DESC textureDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
    HRESULT res;

    res = IDXGISwapChain_GetBuffer(
        swapchain,
        index,
        D3D_GUID(D3D_IID_ID3D12Resource),
        (void **)&swapchainTexture);
    ERROR_CHECK_RETURN("Could not get buffer from swapchain!", 0);

    pTexture = (D3D12Texture *)SDL_calloc(1, sizeof(D3D12Texture));
    if (!pTexture) {
        ID3D12Resource_Release(swapchainTexture);
        return SDL_FALSE;
    }
    pTexture->resource = NULL; /* This will be set in AcquireSwapchainTexture */
    pTexture->subresourceCount = 1;
    pTexture->subresources = (D3D12TextureSubresource *)SDL_calloc(1, sizeof(D3D12TextureSubresource));
    if (!pTexture->subresources) {
        SDL_free(pTexture);
        ID3D12Resource_Release(swapchainTexture);
        return SDL_FALSE;
    }
    pTexture->subresources[0].srvHandle.heap = NULL;
    pTexture->subresources[0].dsvHandle.heap = NULL;
    pTexture->subresources[0].rtvHandle.heap = NULL;
    pTexture->subresources[0].uavHandle.heap = NULL;
    pTexture->subresources[0].parent = pTexture;
    pTexture->subresources[0].index = 0;
    pTexture->subresources[0].layer = 0;
    pTexture->subresources[0].level = 0;
    SDL_AtomicSet(&pTexture->subresources[0].referenceCount, 0);

    ID3D12Resource_GetDesc(swapchainTexture, &textureDesc);
    pTextureContainer->header.info.width = (Uint32)textureDesc.Width;
    pTextureContainer->header.info.height = (Uint32)textureDesc.Height;
    pTextureContainer->header.info.depth = 1;
    pTextureContainer->header.info.layerCount = 1;
    pTextureContainer->header.info.levelCount = 1;
    pTextureContainer->header.info.isCube = 0;
    pTextureContainer->header.info.usageFlags =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT |
        SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT;
    pTextureContainer->header.info.sampleCount = SDL_GPU_SAMPLECOUNT_1;
    pTextureContainer->header.info.format = SDL_GPU_TEXTUREFORMAT_INVALID; /* FIXME: Set this to the actual format! */

    pTextureContainer->debugName = NULL;
    pTextureContainer->textures = (D3D12Texture **)SDL_calloc(1, sizeof(D3D12Texture *));
    if (!pTextureContainer->textures) {
        SDL_free(pTexture->subresources);
        SDL_free(pTexture);
        ID3D12Resource_Release(swapchainTexture);
        return SDL_FALSE;
    }

    pTextureContainer->textureCapacity = 1;
    pTextureContainer->textureCount = 1;
    pTextureContainer->textures[0] = pTexture;
    pTextureContainer->activeTexture = pTexture;
    pTextureContainer->canBeCycled = SDL_FALSE;

    pTexture->container = pTextureContainer;
    pTexture->containerIndex = 0;

    /* Create the SRV for the swapchain */
    D3D12_INTERNAL_AssignCpuDescriptorHandle(
        renderer,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        &pTexture->srvHandle);

    srvDesc.Format = swapchainFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;
    srvDesc.Texture2D.PlaneSlice = 0;

    ID3D12Device_CreateShaderResourceView(
        renderer->device,
        swapchainTexture,
        &srvDesc,
        pTexture->srvHandle.cpuHandle);

    /* Create the RTV for the swapchain */
    D3D12_INTERNAL_AssignCpuDescriptorHandle(
        renderer,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        &pTexture->subresources[0].rtvHandle);

    rtvDesc.Format = rtvFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    ID3D12Device_CreateRenderTargetView(
        renderer->device,
        swapchainTexture,
        &rtvDesc,
        pTexture->subresources[0].rtvHandle.cpuHandle);

    ID3D12Resource_Release(swapchainTexture);

    return SDL_TRUE;
}

static SDL_bool D3D12_INTERNAL_ResizeSwapchain(
    D3D12Renderer *renderer,
    D3D12WindowData *windowData,
    Sint32 width,
    Sint32 height)
{
    /* Wait so we don't release in-flight views */
    D3D12_Wait((SDL_GpuRenderer *)renderer);

    /* Release views and clean up */
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &windowData->textureContainers[i].activeTexture->srvHandle);
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &windowData->textureContainers[i].activeTexture->subresources[0].rtvHandle);

        SDL_free(windowData->textureContainers[i].activeTexture->subresources);
        SDL_free(windowData->textureContainers[i].activeTexture);
        SDL_free(windowData->textureContainers[i].textures);
    }

    /* Resize the swapchain */
    HRESULT res = IDXGISwapChain_ResizeBuffers(
        windowData->swapchain,
        0, /* Keep buffer count the same */
        width,
        height,
        DXGI_FORMAT_UNKNOWN, /* Keep the old format */
        renderer->supportsTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    ERROR_CHECK_RETURN("Could not resize swapchain buffers", 0)

    /* Create texture object for the swapchain */
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        if (!D3D12_INTERNAL_InitializeSwapchainTexture(
                renderer,
                windowData->swapchain,
                windowData->swapchainFormat,
                (windowData->swapchainComposition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR) ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : windowData->swapchainFormat,
                i,
                &windowData->textureContainers[i])) {
            return SDL_FALSE;
        }
    }

    return SDL_TRUE;
}

static void D3D12_INTERNAL_DestroySwapchain(
    D3D12Renderer *renderer,
    D3D12WindowData *windowData)
{
    /* Release views and clean up */
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &windowData->textureContainers[i].activeTexture->srvHandle);
        D3D12_INTERNAL_ReleaseCpuDescriptorHandle(
            renderer,
            &windowData->textureContainers[i].activeTexture->subresources[0].rtvHandle);

        SDL_free(windowData->textureContainers[i].activeTexture->subresources);
        SDL_free(windowData->textureContainers[i].activeTexture);
        SDL_free(windowData->textureContainers[i].textures);
    }

    IDXGISwapChain_Release(windowData->swapchain);
    windowData->swapchain = NULL;
}

static SDL_bool D3D12_INTERNAL_CreateSwapchain(
    D3D12Renderer *renderer,
    D3D12WindowData *windowData,
    SDL_GpuSwapchainComposition swapchainComposition,
    SDL_GpuPresentMode presentMode)
{
    HWND dxgiHandle;
    int width, height;
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreenDesc;
    DXGI_FORMAT swapchainFormat;
    IDXGIFactory1 *pParent;
    IDXGISwapChain1 *swapchain;
    IDXGISwapChain3 *swapchain3;
    Uint32 colorSpaceSupport;
    HRESULT res;

    /* Get the DXGI handle */
#ifdef _WIN32
    dxgiHandle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(windowData->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
    dxgiHandle = (HWND)windowData->window;
#endif

    /* Get the window size */
    SDL_GetWindowSize(windowData->window, &width, &height);

    swapchainFormat = SwapchainCompositionToTextureFormat[swapchainComposition];

    // Initialize the swapchain buffer descriptor
    swapchainDesc.Width = 0;
    swapchainDesc.Height = 0;
    swapchainDesc.Format = swapchainFormat;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // | DXGI_USAGE_UNORDERED_ACCESS | DXGI_USAGE_SHADER_INPUT;
    swapchainDesc.BufferCount = MAX_FRAMES_IN_FLIGHT;
    swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapchainDesc.Flags = 0;
    swapchainDesc.Stereo = 0;

    // Initialize the fullscreen descriptor (if needed)
    fullscreenDesc.RefreshRate.Numerator = 0;
    fullscreenDesc.RefreshRate.Denominator = 0;
    fullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    fullscreenDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    fullscreenDesc.Windowed = SDL_TRUE;

    if (renderer->supportsTearing) {
        swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    } else {
        swapchainDesc.Flags = 0;
    }

#ifndef SDL_PLATFORM_WINRT
    if (!IsWindow(dxgiHandle)) {
        return SDL_FALSE;
    }
#endif

    /* Create the swapchain! */
    res = IDXGIFactory4_CreateSwapChainForHwnd(
        renderer->factory,
        (IUnknown *)renderer->commandQueue,
        dxgiHandle,
        &swapchainDesc,
        &fullscreenDesc,
        NULL,
        &swapchain);
    ERROR_CHECK_RETURN("Could not create swapchain", 0);

    res = IDXGISwapChain1_QueryInterface(
        swapchain,
        D3D_GUID(D3D_IID_IDXGISwapChain3),
        (void **)&swapchain3);
    IDXGISwapChain1_Release(swapchain);
    ERROR_CHECK_RETURN("Could not create IDXGISwapChain3", 0);

    IDXGISwapChain3_CheckColorSpaceSupport(
        swapchain3,
        windowData->swapchainColorSpace,
        &colorSpaceSupport);

    if (!(colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Requested colorspace is unsupported!");
        return SDL_FALSE;
    }

    IDXGISwapChain3_SetColorSpace1(
        swapchain3,
        windowData->swapchainColorSpace);

    /*
     * The swapchain's parent is a separate factory from the factory that
     * we used to create the swapchain, and only that parent can be used to
     * set the window association. Trying to set an association on our factory
     * will silently fail and doesn't even verify arguments or return errors.
     * See https://gamedev.net/forums/topic/634235-dxgidisabling-altenter/4999955/
     */
    res = IDXGISwapChain3_GetParent(
        swapchain3,
        D3D_GUID(D3D_IID_IDXGIFactory1),
        (void **)&pParent);
    if (FAILED(res)) {
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "Could not get swapchain parent! Error Code: " HRESULT_FMT,
            res);
    } else {
        /* Disable DXGI window crap */
        res = IDXGIFactory1_MakeWindowAssociation(
            pParent,
            dxgiHandle,
            DXGI_MWA_NO_WINDOW_CHANGES);
        if (FAILED(res)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "MakeWindowAssociation failed! Error Code: " HRESULT_FMT,
                res);
        }

        /* We're done with the parent now */
        IDXGIFactory1_Release(pParent);
    }
    /* Initialize the swapchain data */
    windowData->swapchain = swapchain3;
    windowData->presentMode = presentMode;
    windowData->swapchainComposition = swapchainComposition;
    windowData->swapchainFormat = swapchainFormat;
    windowData->swapchainColorSpace = SwapchainCompositionToColorSpace[swapchainComposition];
    windowData->frameCounter = 0;

    /* If a you are using a FLIP model format you can't create the swapchain as DXGI_FORMAT_B8G8R8A8_UNORM_SRGB.
     * You have to create the swapchain as DXGI_FORMAT_B8G8R8A8_UNORM and then set the render target view's format to DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
     */
    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        if (!D3D12_INTERNAL_InitializeSwapchainTexture(
                renderer,
                swapchain3,
                swapchainFormat,
                (swapchainComposition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR) ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : windowData->swapchainFormat,
                i,
                &windowData->textureContainers[i])) {
            IDXGISwapChain3_Release(swapchain3);
            return SDL_FALSE;
        }
    }

    return SDL_TRUE;
}

static SDL_bool D3D12_ClaimWindow(
    SDL_GpuRenderer *driverData,
    SDL_Window *window,
    SDL_GpuSwapchainComposition swapchainComposition,
    SDL_GpuPresentMode presentMode)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12WindowData *windowData = D3D12_INTERNAL_FetchWindowData(window);

    if (windowData == NULL) {
        windowData = (D3D12WindowData *)SDL_calloc(1, sizeof(D3D12WindowData));
        if (!windowData) {
            return SDL_FALSE;
        }
        windowData->window = window;

        if (D3D12_INTERNAL_CreateSwapchain(renderer, windowData, swapchainComposition, presentMode)) {
            SDL_SetPointerProperty(SDL_GetWindowProperties(window), WINDOW_PROPERTY_DATA, windowData);

            SDL_LockMutex(renderer->windowLock);

            if (renderer->claimedWindowCount >= renderer->claimedWindowCapacity) {
                renderer->claimedWindowCapacity *= 2;
                renderer->claimedWindows = (D3D12WindowData **)SDL_realloc(
                    renderer->claimedWindows,
                    renderer->claimedWindowCapacity * sizeof(D3D12WindowData *));
            }
            renderer->claimedWindows[renderer->claimedWindowCount] = windowData;
            renderer->claimedWindowCount += 1;

            SDL_UnlockMutex(renderer->windowLock);

            return SDL_TRUE;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create swapchain, failed to claim window!");
            SDL_free(windowData);
            return SDL_FALSE;
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Window already claimed!");
        return SDL_FALSE;
    }
}

static void D3D12_UnclaimWindow(
    SDL_GpuRenderer *driverData,
    SDL_Window *window)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12WindowData *windowData = D3D12_INTERNAL_FetchWindowData(window);

    if (windowData == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Window already unclaimed!");
        return;
    }

    D3D12_Wait(driverData);

    for (Uint32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i += 1) {
        if (windowData->inFlightFences[i] != NULL) {
            D3D12_ReleaseFence(
                driverData,
                (SDL_GpuFence *)windowData->inFlightFences[i]);
            windowData->inFlightFences[i] = NULL;
        }
    }

    D3D12_INTERNAL_DestroySwapchain(renderer, windowData);

    SDL_LockMutex(renderer->windowLock);
    for (Uint32 i = 0; i < renderer->claimedWindowCount; i += 1) {
        if (renderer->claimedWindows[i]->window == window) {
            renderer->claimedWindows[i] = renderer->claimedWindows[renderer->claimedWindowCount - 1];
            renderer->claimedWindowCount -= 1;
            break;
        }
    }
    SDL_UnlockMutex(renderer->windowLock);

    SDL_free(windowData);
    SDL_ClearProperty(SDL_GetWindowProperties(window), WINDOW_PROPERTY_DATA);
}

static SDL_bool D3D12_SetSwapchainParameters(
    SDL_GpuRenderer *driverData,
    SDL_Window *window,
    SDL_GpuSwapchainComposition swapchainComposition,
    SDL_GpuPresentMode presentMode)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12WindowData *windowData = D3D12_INTERNAL_FetchWindowData(window);

    if (windowData == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Cannot set swapchain parameters on unclaimed window!");
        return SDL_FALSE;
    }

    if (!D3D12_SupportsSwapchainComposition(driverData, window, swapchainComposition)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Swapchain composition not supported!");
        return SDL_FALSE;
    }

    if (!D3D12_SupportsPresentMode(driverData, window, presentMode)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Present mode not supported!");
        return SDL_FALSE;
    }

    if (
        swapchainComposition != windowData->swapchainComposition ||
        presentMode != windowData->presentMode) {
        D3D12_Wait(driverData);

        /* Recreate the swapchain */
        D3D12_INTERNAL_DestroySwapchain(
            renderer,
            windowData);

        return D3D12_INTERNAL_CreateSwapchain(
            renderer,
            windowData,
            swapchainComposition,
            presentMode);
    }

    return SDL_TRUE;
}

static SDL_GpuTextureFormat D3D12_GetSwapchainTextureFormat(
    SDL_GpuRenderer *driverData,
    SDL_Window *window)
{
    D3D12WindowData *windowData = D3D12_INTERNAL_FetchWindowData(window);

    if (windowData == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Cannot get swapchain format, window has not been claimed!");
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }

    switch (windowData->swapchainFormat) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8;

    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_SRGB;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SFLOAT;

    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return SDL_GPU_TEXTUREFORMAT_R10G10B10A2;

    default:
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Unrecognized swapchain format!");
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

static D3D12Fence *D3D12_INTERNAL_AcquireFence(
    D3D12Renderer *renderer)
{
    D3D12Fence *fence;
    ID3D12Fence *handle;
    HRESULT res;

    SDL_LockMutex(renderer->fenceLock);

    if (renderer->availableFenceCount == 0) {
        res = ID3D12Device_CreateFence(
            renderer->device,
            D3D12_FENCE_UNSIGNALED_VALUE,
            D3D12_FENCE_FLAG_NONE,
            D3D_GUID(D3D_IID_ID3D12Fence),
            (void **)&handle);
        if (FAILED(res)) {
            D3D12_INTERNAL_LogError(renderer->device, "Failed to create fence!", res);
            SDL_UnlockMutex(renderer->fenceLock);
            return NULL;
        }

        fence = (D3D12Fence *)SDL_calloc(1, sizeof(D3D12Fence));
        if (!fence) {
            ID3D12Fence_Release(handle);
            SDL_UnlockMutex(renderer->fenceLock);
            return NULL;
        }
        fence->handle = handle;
        fence->event = CreateEventEx(NULL, 0, 0, EVENT_ALL_ACCESS);
        SDL_AtomicSet(&fence->referenceCount, 0);
    } else {
        fence = renderer->availableFences[renderer->availableFenceCount - 1];
        renderer->availableFenceCount -= 1;
        ID3D12Fence_Signal(fence->handle, D3D12_FENCE_UNSIGNALED_VALUE);
    }

    SDL_UnlockMutex(renderer->fenceLock);
    return fence;
}

static void D3D12_INTERNAL_AllocateCommandBuffer(
    D3D12Renderer *renderer)
{
    D3D12CommandBuffer *commandBuffer;
    HRESULT res;
    ID3D12CommandAllocator *commandAllocator;
    ID3D12GraphicsCommandList *commandList;

    commandBuffer = (D3D12CommandBuffer *)SDL_calloc(1, sizeof(D3D12CommandBuffer));
    if (!commandBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create ID3D12CommandList. Out of Memory");
        return;
    }

    res = ID3D12Device_CreateCommandAllocator(
        renderer->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        D3D_GUID(D3D_IID_ID3D12CommandAllocator),
        (void **)&commandAllocator);
    if (FAILED(res)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create ID3D12CommandAllocator");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return;
    }
    commandBuffer->commandAllocator = commandAllocator;

    res = ID3D12Device_CreateCommandList(
        renderer->device,
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator,
        NULL,
        D3D_GUID(D3D_IID_ID3D12GraphicsCommandList),
        (void **)&commandList);

    if (FAILED(res)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create ID3D12CommandList");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return;
    }
    commandBuffer->graphicsCommandList = commandList;

    commandBuffer->renderer = renderer;
    commandBuffer->inFlightFence = NULL;

    /* Window handling */
    commandBuffer->presentDataCapacity = 1;
    commandBuffer->presentDataCount = 0;
    commandBuffer->presentDatas = (D3D12PresentData *)SDL_calloc(
        commandBuffer->presentDataCapacity, sizeof(D3D12PresentData));

    /* Resource tracking */
    commandBuffer->usedTextureSubresourceCapacity = 4;
    commandBuffer->usedTextureSubresourceCount = 0;
    commandBuffer->usedTextureSubresources = (D3D12TextureSubresource **)SDL_calloc(
        commandBuffer->usedTextureSubresourceCapacity, sizeof(D3D12TextureSubresource *));

    commandBuffer->usedBufferCapacity = 4;
    commandBuffer->usedBufferCount = 0;
    commandBuffer->usedBuffers = (D3D12Buffer **)SDL_calloc(
        commandBuffer->usedBufferCapacity, sizeof(D3D12Buffer *));

    commandBuffer->usedSamplerCapacity = 4;
    commandBuffer->usedSamplerCount = 0;
    commandBuffer->usedSamplers = (D3D12Sampler **)SDL_calloc(
        commandBuffer->usedSamplerCapacity, sizeof(D3D12Sampler *));

    commandBuffer->usedGraphicsPipelineCapacity = 4;
    commandBuffer->usedGraphicsPipelineCount = 0;
    commandBuffer->usedGraphicsPipelines = (D3D12GraphicsPipeline **)SDL_calloc(
        commandBuffer->usedGraphicsPipelineCapacity, sizeof(D3D12GraphicsPipeline *));

    commandBuffer->usedComputePipelineCapacity = 4;
    commandBuffer->usedComputePipelineCount = 0;
    commandBuffer->usedComputePipelines = (D3D12ComputePipeline **)SDL_calloc(
        commandBuffer->usedComputePipelineCapacity, sizeof(D3D12ComputePipeline *));

    commandBuffer->usedUniformBufferCapacity = 4;
    commandBuffer->usedUniformBufferCount = 0;
    commandBuffer->usedUniformBuffers = (D3D12UniformBuffer **)SDL_calloc(
        commandBuffer->usedUniformBufferCapacity, sizeof(D3D12UniformBuffer *));

    commandBuffer->textureDownloadCapacity = 4;
    commandBuffer->textureDownloadCount = 0;
    commandBuffer->textureDownloads = SDL_calloc(
        commandBuffer->textureDownloadCapacity, sizeof(D3D12TextureDownload *));

    if (
        (!commandBuffer->presentDatas) ||
        (!commandBuffer->usedTextureSubresources) ||
        (!commandBuffer->usedBuffers) ||
        (!commandBuffer->usedSamplers) ||
        (!commandBuffer->usedGraphicsPipelines) ||
        (!commandBuffer->usedComputePipelines) ||
        (!commandBuffer->usedUniformBuffers) ||
        (!commandBuffer->textureDownloads)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create ID3D12CommandList. Out of Memory");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return;
    }

    D3D12CommandBuffer **resizedAvailableCommandBuffers = (D3D12CommandBuffer **)SDL_realloc(
        renderer->availableCommandBuffers,
        sizeof(D3D12CommandBuffer *) * (renderer->availableCommandBufferCapacity + 1));

    if (!resizedAvailableCommandBuffers) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create ID3D12CommandList. Out of Memory");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return;
    }
    /* Add to inactive command buffer array */
    renderer->availableCommandBufferCapacity += 1;
    renderer->availableCommandBuffers = resizedAvailableCommandBuffers;

    renderer->availableCommandBuffers[renderer->availableCommandBufferCount] = commandBuffer;
    renderer->availableCommandBufferCount += 1;
}

static D3D12CommandBuffer *D3D12_INTERNAL_AcquireCommandBufferFromPool(
    D3D12Renderer *renderer)
{
    D3D12CommandBuffer *commandBuffer;

    if (renderer->availableCommandBufferCount == 0) {
        D3D12_INTERNAL_AllocateCommandBuffer(renderer);
    }

    commandBuffer = renderer->availableCommandBuffers[renderer->availableCommandBufferCount - 1];
    renderer->availableCommandBufferCount -= 1;

    return commandBuffer;
}

static SDL_GpuCommandBuffer *D3D12_AcquireCommandBuffer(
    SDL_GpuRenderer *driverData)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12CommandBuffer *commandBuffer;
    ID3D12DescriptorHeap *heaps[2];
    SDL_zeroa(heaps);

    SDL_LockMutex(renderer->acquireCommandBufferLock);
    commandBuffer = D3D12_INTERNAL_AcquireCommandBufferFromPool(renderer);
    SDL_UnlockMutex(renderer->acquireCommandBufferLock);

    if (commandBuffer == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire command buffer!");
        return NULL;
    }

    /* Set the descriptor heaps! */
    commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] =
        D3D12_INTERNAL_AcquireDescriptorHeapFromPool(commandBuffer, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire descriptor heap!");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return NULL;
    }

    commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] =
        D3D12_INTERNAL_AcquireDescriptorHeapFromPool(commandBuffer, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    if (!commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire descriptor heap!");
        D3D12_INTERNAL_DestroyCommandBuffer(commandBuffer);
        return NULL;
    }

    heaps[0] = commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]->handle;
    heaps[1] = commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]->handle;

    ID3D12GraphicsCommandList_SetDescriptorHeaps(
        commandBuffer->graphicsCommandList,
        2,
        heaps);

    /* Set the bind state */
    commandBuffer->currentGraphicsPipeline = NULL;

    SDL_zeroa(commandBuffer->colorAttachmentTextureSubresources);
    commandBuffer->colorAttachmentCount = 0;
    commandBuffer->depthStencilTextureSubresource = NULL;

    SDL_zeroa(commandBuffer->vertexSamplerTextures);
    SDL_zeroa(commandBuffer->vertexSamplers);
    SDL_zeroa(commandBuffer->vertexStorageTextureSubresources);
    SDL_zeroa(commandBuffer->vertexStorageBuffers);
    SDL_zeroa(commandBuffer->vertexUniformBuffers);

    SDL_zeroa(commandBuffer->fragmentSamplerTextures);
    SDL_zeroa(commandBuffer->fragmentSamplers);
    SDL_zeroa(commandBuffer->fragmentStorageTextureSlices);
    SDL_zeroa(commandBuffer->fragmentStorageBuffers);
    SDL_zeroa(commandBuffer->fragmentUniformBuffers);

    SDL_zeroa(commandBuffer->computeReadOnlyStorageTextures);
    SDL_zeroa(commandBuffer->computeReadOnlyStorageBuffers);
    SDL_zeroa(commandBuffer->computeReadWriteStorageTextures);
    SDL_zeroa(commandBuffer->computeReadWriteStorageBuffers);
    SDL_zeroa(commandBuffer->computeUniformBuffers);

    commandBuffer->autoReleaseFence = SDL_TRUE;

    return (SDL_GpuCommandBuffer *)commandBuffer;
}

static SDL_GpuTexture *D3D12_AcquireSwapchainTexture(
    SDL_GpuCommandBuffer *commandBuffer,
    SDL_Window *window,
    Uint32 *pWidth,
    Uint32 *pHeight)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Renderer *renderer = d3d12CommandBuffer->renderer;
    D3D12WindowData *windowData;
    DXGI_SWAP_CHAIN_DESC swapchainDesc;
    int w, h;
    HRESULT res;

    windowData = D3D12_INTERNAL_FetchWindowData(window);
    if (windowData == NULL) {
        return NULL;
    }

    /* Check for window size changes and resize the swapchain if needed. */
    IDXGISwapChain_GetDesc(windowData->swapchain, &swapchainDesc);
    SDL_GetWindowSize(window, &w, &h);

    if (w != swapchainDesc.BufferDesc.Width || h != swapchainDesc.BufferDesc.Height) {
        res = D3D12_INTERNAL_ResizeSwapchain(
            renderer,
            windowData,
            w,
            h);
        ERROR_CHECK_RETURN("Could not resize swapchain", NULL);
    }

    if (windowData->inFlightFences[windowData->frameCounter] != NULL) {
        if (windowData->presentMode == SDL_GPU_PRESENTMODE_VSYNC) {
            /* In VSYNC mode, block until the least recent presented frame is done */
            D3D12_WaitForFences(
                (SDL_GpuRenderer *)renderer,
                SDL_TRUE,
                (SDL_GpuFence **)&windowData->inFlightFences[windowData->frameCounter],
                1);
        } else {
            if (!D3D12_QueryFence(
                    (SDL_GpuRenderer *)renderer,
                    (SDL_GpuFence *)windowData->inFlightFences[windowData->frameCounter])) {
                /*
                 * In MAILBOX or IMMEDIATE mode, if the least recent fence is not signaled,
                 * return NULL to indicate that rendering should be skipped
                 */
                return NULL;
            }
        }

        D3D12_ReleaseFence(
            (SDL_GpuRenderer *)renderer,
            (SDL_GpuFence *)windowData->inFlightFences[windowData->frameCounter]);

        windowData->inFlightFences[windowData->frameCounter] = NULL;
    }

    Uint32 swapchainIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(windowData->swapchain);

    /* Set the handle on the windowData texture data. */
    res = IDXGISwapChain_GetBuffer(
        windowData->swapchain,
        swapchainIndex,
        D3D_GUID(D3D_IID_ID3D12Resource),
        (void **)&windowData->textureContainers[swapchainIndex].activeTexture->resource);
    ERROR_CHECK_RETURN("Could not acquire swapchain!", NULL);

    /* Send the dimensions to the out parameters. */
    *pWidth = windowData->textureContainers[swapchainIndex].header.info.width;
    *pHeight = windowData->textureContainers[swapchainIndex].header.info.height;

    /* TODO: Set up the texture container */

    /* Set up presentation */
    if (d3d12CommandBuffer->presentDataCount == d3d12CommandBuffer->presentDataCapacity) {
        d3d12CommandBuffer->presentDataCapacity += 1;
        d3d12CommandBuffer->presentDatas = (D3D12PresentData *)SDL_realloc(
            d3d12CommandBuffer->presentDatas,
            d3d12CommandBuffer->presentDataCapacity * sizeof(D3D12PresentData));
    }
    d3d12CommandBuffer->presentDatas[d3d12CommandBuffer->presentDataCount].windowData = windowData;
    d3d12CommandBuffer->presentDatas[d3d12CommandBuffer->presentDataCount].swapchainImageIndex = swapchainIndex;
    d3d12CommandBuffer->presentDataCount += 1;

    return (SDL_GpuTexture *)&windowData->textureContainers[swapchainIndex];
}

static void D3D12_INTERNAL_PerformPendingDestroys(D3D12Renderer *renderer)
{
    SDL_LockMutex(renderer->disposeLock);

    for (Sint32 i = renderer->buffersToDestroyCount - 1; i >= 0; i -= 1) {
        if (SDL_AtomicGet(&renderer->buffersToDestroy[i]->referenceCount) == 0) {
            D3D12_INTERNAL_DestroyBuffer(
                renderer,
                renderer->buffersToDestroy[i]);

            renderer->buffersToDestroy[i] = renderer->buffersToDestroy[renderer->buffersToDestroyCount - 1];
            renderer->buffersToDestroyCount -= 1;
        }
    }

    for (Sint32 i = renderer->texturesToDestroyCount - 1; i >= 0; i -= 1) {
        Sint32 refCountTotal = 0;
        for (Uint32 subresourceIndex = 0; subresourceIndex < renderer->texturesToDestroy[i]->subresourceCount; subresourceIndex += 1) {
            refCountTotal += SDL_AtomicGet(&renderer->texturesToDestroy[i]->subresources[subresourceIndex].referenceCount);
        }

        if (refCountTotal == 0) {
            D3D12_INTERNAL_DestroyTexture(
                renderer,
                renderer->texturesToDestroy[i]);

            renderer->texturesToDestroy[i] = renderer->texturesToDestroy[renderer->texturesToDestroyCount - 1];
            renderer->texturesToDestroyCount -= 1;
        }
    }

    for (Sint32 i = renderer->samplersToDestroyCount - 1; i >= 0; i -= 1) {
        if (SDL_AtomicGet(&renderer->samplersToDestroy[i]->referenceCount) == 0) {
            D3D12_INTERNAL_DestroySampler(
                renderer,
                renderer->samplersToDestroy[i]);

            renderer->samplersToDestroy[i] = renderer->samplersToDestroy[renderer->samplersToDestroyCount - 1];
            renderer->samplersToDestroyCount -= 1;
        }
    }

    for (Sint32 i = renderer->graphicsPipelinesToDestroyCount - 1; i >= 0; i -= 1) {
        if (SDL_AtomicGet(&renderer->graphicsPipelinesToDestroy[i]->referenceCount) == 0) {
            D3D12_INTERNAL_DestroyGraphicsPipeline(
                renderer->graphicsPipelinesToDestroy[i]);

            renderer->graphicsPipelinesToDestroy[i] = renderer->graphicsPipelinesToDestroy[renderer->graphicsPipelinesToDestroyCount - 1];
            renderer->graphicsPipelinesToDestroyCount -= 1;
        }
    }

    for (Sint32 i = renderer->computePipelinesToDestroyCount - 1; i >= 0; i -= 1) {
        if (SDL_AtomicGet(&renderer->computePipelinesToDestroy[i]->referenceCount) == 0) {
            D3D12_INTERNAL_DestroyComputePipeline(
                renderer->computePipelinesToDestroy[i]);

            renderer->computePipelinesToDestroy[i] = renderer->computePipelinesToDestroy[renderer->computePipelinesToDestroyCount - 1];
            renderer->computePipelinesToDestroyCount -= 1;
        }
    }

    SDL_UnlockMutex(renderer->disposeLock);
}

static void D3D12_INTERNAL_CopyTextureDownload(
    D3D12CommandBuffer *commandBuffer,
    D3D12TextureDownload *download)
{
    Uint8 *sourcePtr;
    Uint8 *destPtr;
    HRESULT res;

    res = ID3D12Resource_Map(
        download->temporaryBuffer->handle,
        0,
        NULL,
        (void **)&sourcePtr);

    if (FAILED(res)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to map temporary buffer!");
        return;
    }

    res = ID3D12Resource_Map(
        download->destinationBuffer->handle,
        0,
        NULL,
        (void **)&destPtr);

    if (FAILED(res)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to map destination buffer!");
        return;
    }

    for (Uint32 sliceIndex = 0; sliceIndex < download->depth; sliceIndex += 1) {
        for (Uint32 rowIndex = 0; rowIndex < download->height; rowIndex += 1) {
            SDL_memcpy(
                destPtr + download->bufferOffset + (sliceIndex * download->bytesPerDepthSlice) + (rowIndex * download->bytesPerRow),
                sourcePtr + (sliceIndex * download->height) + (rowIndex * download->alignedBytesPerRow),
                download->bytesPerRow);
        }
    }

    ID3D12Resource_Unmap(
        download->temporaryBuffer->handle,
        0,
        NULL);

    ID3D12Resource_Unmap(
        download->destinationBuffer->handle,
        0,
        NULL);
}

static void D3D12_INTERNAL_CleanCommandBuffer(
    D3D12Renderer *renderer,
    D3D12CommandBuffer *commandBuffer)
{
    Uint32 i;
    HRESULT res;

    /* Perform deferred texture data copies */

    for (i = 0; i < commandBuffer->textureDownloadCount; i += 1) {
        D3D12_INTERNAL_CopyTextureDownload(
            commandBuffer,
            commandBuffer->textureDownloads[i]);
        SDL_free(commandBuffer->textureDownloads[i]);
    }
    commandBuffer->textureDownloadCount = 0;

    res = ID3D12CommandAllocator_Reset(commandBuffer->commandAllocator);
    ERROR_CHECK("Could not reset command allocator")

    res = ID3D12GraphicsCommandList_Reset(
        commandBuffer->graphicsCommandList,
        commandBuffer->commandAllocator,
        NULL);
    ERROR_CHECK("Could not reset graphicsCommandList")

    /* Return descriptor heaps to pool */
    D3D12_INTERNAL_ReturnDescriptorHeapToPool(
        renderer,
        commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV]);
    D3D12_INTERNAL_ReturnDescriptorHeapToPool(
        renderer,
        commandBuffer->gpuDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER]);

    /* Uniform buffers are now available */
    SDL_LockMutex(renderer->acquireUniformBufferLock);

    for (i = 0; i < commandBuffer->usedUniformBufferCount; i += 1) {
        D3D12_INTERNAL_ReturnUniformBufferToPool(
            renderer,
            commandBuffer->usedUniformBuffers[i]);
    }
    commandBuffer->usedUniformBufferCount = 0;

    SDL_UnlockMutex(renderer->acquireUniformBufferLock);

    /* TODO: More reference counting */

    for (i = 0; i < commandBuffer->usedTextureSubresourceCount; i += 1) {
        (void)SDL_AtomicDecRef(&commandBuffer->usedTextureSubresources[i]->referenceCount);
    }
    commandBuffer->usedTextureSubresourceCount = 0;

    for (i = 0; i < commandBuffer->usedBufferCount; i += 1) {
        (void)SDL_AtomicDecRef(&commandBuffer->usedBuffers[i]->referenceCount);
    }
    commandBuffer->usedBufferCount = 0;

    for (i = 0; i < commandBuffer->usedSamplerCount; i += 1) {
        (void)SDL_AtomicDecRef(&commandBuffer->usedSamplers[i]->referenceCount);
    }
    commandBuffer->usedSamplerCount = 0;

    for (i = 0; i < commandBuffer->usedGraphicsPipelineCount; i += 1) {
        (void)SDL_AtomicDecRef(&commandBuffer->usedGraphicsPipelines[i]->referenceCount);
    }
    commandBuffer->usedGraphicsPipelineCount = 0;

    for (i = 0; i < commandBuffer->usedComputePipelineCount; i += 1) {
        (void)SDL_AtomicDecRef(&commandBuffer->usedComputePipelines[i]->referenceCount);
    }
    commandBuffer->usedComputePipelineCount = 0;

    /* Reset presentation */
    commandBuffer->presentDataCount = 0;

    /* The fence is now available (unless SubmitAndAcquireFence was called) */
    if (commandBuffer->autoReleaseFence) {
        D3D12_ReleaseFence(
            (SDL_GpuRenderer *)renderer,
            (SDL_GpuFence *)commandBuffer->inFlightFence);

        commandBuffer->inFlightFence = NULL;
    }

    /* Return command buffer to pool */
    SDL_LockMutex(renderer->acquireCommandBufferLock);

    if (renderer->availableCommandBufferCount == renderer->availableCommandBufferCapacity) {
        renderer->availableCommandBufferCapacity += 1;
        renderer->availableCommandBuffers = (D3D12CommandBuffer **)SDL_realloc(
            renderer->availableCommandBuffers,
            renderer->availableCommandBufferCapacity * sizeof(D3D12CommandBuffer *));
    }

    renderer->availableCommandBuffers[renderer->availableCommandBufferCount] = commandBuffer;
    renderer->availableCommandBufferCount += 1;

    SDL_UnlockMutex(renderer->acquireCommandBufferLock);

    /* Remove this command buffer from the submitted list */
    for (i = 0; i < renderer->submittedCommandBufferCount; i += 1) {
        if (renderer->submittedCommandBuffers[i] == commandBuffer) {
            renderer->submittedCommandBuffers[i] = renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount - 1];
            renderer->submittedCommandBufferCount -= 1;
        }
    }
}

static void D3D12_Submit(
    SDL_GpuCommandBuffer *commandBuffer)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    D3D12Renderer *renderer = d3d12CommandBuffer->renderer;
    ID3D12CommandList *commandLists[1];
    HRESULT res;

    SDL_LockMutex(renderer->submitLock);

    /* Unmap uniform buffers */
    for (Uint32 i = 0; i < MAX_UNIFORM_BUFFERS_PER_STAGE; i += 1) {
        if (d3d12CommandBuffer->vertexUniformBuffers[i] != NULL) {
            ID3D12Resource_Unmap(
                d3d12CommandBuffer->vertexUniformBuffers[i]->buffer->handle,
                0,
                NULL);
            d3d12CommandBuffer->vertexUniformBuffers[i]->buffer->mapPointer = NULL;
        }

        if (d3d12CommandBuffer->fragmentUniformBuffers[i] != NULL) {
            ID3D12Resource_Unmap(
                d3d12CommandBuffer->fragmentUniformBuffers[i]->buffer->handle,
                0,
                NULL);
            d3d12CommandBuffer->fragmentUniformBuffers[i]->buffer->mapPointer = NULL;
        }

        /* TODO: compute uniforms */
    }

    /* Transition present textures to present mode */
    for (Uint32 i = 0; i < d3d12CommandBuffer->presentDataCount; i += 1) {
        Uint32 swapchainIndex = d3d12CommandBuffer->presentDatas[i].swapchainImageIndex;
        D3D12TextureContainer *container = &d3d12CommandBuffer->presentDatas[i].windowData->textureContainers[swapchainIndex];
        D3D12TextureSubresource *subresource = D3D12_INTERNAL_FetchTextureSubresource(container, 0, 0);
        D3D12_INTERNAL_TextureSubresourceTransitionFromDefaultUsage(
            d3d12CommandBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            subresource);
    }

    /* Notify the command buffer that we have completed recording */
    res = ID3D12GraphicsCommandList_Close(d3d12CommandBuffer->graphicsCommandList);
    ERROR_CHECK("Failed to close command list!");

    res = ID3D12GraphicsCommandList_QueryInterface(
        d3d12CommandBuffer->graphicsCommandList,
        D3D_GUID(D3D_IID_ID3D12CommandList),
        (void **)&commandLists[0]);
    ERROR_CHECK("Failed to convert command list!")

    /* Submit the command list to the queue */
    ID3D12CommandQueue_ExecuteCommandLists(
        renderer->commandQueue,
        1,
        commandLists);

    ID3D12CommandList_Release(commandLists[0]);

    /* Acquire a fence and set it to the in-flight fence */
    d3d12CommandBuffer->inFlightFence = D3D12_INTERNAL_AcquireFence(renderer);
    if (!d3d12CommandBuffer->inFlightFence) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire fence.");
    }
    /* Command buffer has a reference to the in-flight fence */
    (void)SDL_AtomicIncRef(&d3d12CommandBuffer->inFlightFence->referenceCount);

    /* Mark that a fence should be signaled after command list execution */
    res = ID3D12CommandQueue_Signal(
        renderer->commandQueue,
        d3d12CommandBuffer->inFlightFence->handle,
        D3D12_FENCE_SIGNAL_VALUE);
    ERROR_CHECK("Failed to enqueue fence signal!");

    /* Mark the command buffer as submitted */
    if (renderer->submittedCommandBufferCount + 1 >= renderer->submittedCommandBufferCapacity) {
        renderer->submittedCommandBufferCapacity = renderer->submittedCommandBufferCount + 1;

        renderer->submittedCommandBuffers = (D3D12CommandBuffer **)SDL_realloc(
            renderer->submittedCommandBuffers,
            sizeof(D3D12CommandBuffer *) * renderer->submittedCommandBufferCapacity);
    }

    renderer->submittedCommandBuffers[renderer->submittedCommandBufferCount] = d3d12CommandBuffer;
    renderer->submittedCommandBufferCount += 1;

    /* Present, if applicable */
    for (Uint32 i = 0; i < d3d12CommandBuffer->presentDataCount; i += 1) {
        D3D12PresentData *presentData = &d3d12CommandBuffer->presentDatas[i];
        D3D12WindowData *windowData = presentData->windowData;

        /* NOTE: flip discard always supported since DXGI 1.4 is required */
        Uint32 syncInterval = 1;
        if (windowData->presentMode == SDL_GPU_PRESENTMODE_IMMEDIATE ||
            windowData->presentMode == SDL_GPU_PRESENTMODE_MAILBOX) {
            syncInterval = 0;
        }

        Uint32 presentFlags = 0;
        if (renderer->supportsTearing &&
            windowData->presentMode == SDL_GPU_PRESENTMODE_IMMEDIATE) {
            presentFlags = DXGI_PRESENT_ALLOW_TEARING;
        }

        IDXGISwapChain_Present(
            windowData->swapchain,
            syncInterval,
            presentFlags);

        ID3D12Resource_Release(windowData->textureContainers[presentData->swapchainImageIndex].activeTexture->resource);

        windowData->inFlightFences[windowData->frameCounter] = d3d12CommandBuffer->inFlightFence;
        (void)SDL_AtomicIncRef(&d3d12CommandBuffer->inFlightFence->referenceCount);
        windowData->frameCounter = (windowData->frameCounter + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    /* Check for cleanups */
    for (Sint32 i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1) {
        Uint64 fenceValue = ID3D12Fence_GetCompletedValue(
            renderer->submittedCommandBuffers[i]->inFlightFence->handle);

        if (fenceValue == D3D12_FENCE_SIGNAL_VALUE) {
            D3D12_INTERNAL_CleanCommandBuffer(
                renderer,
                renderer->submittedCommandBuffers[i]);
        }
    }

    D3D12_INTERNAL_PerformPendingDestroys(renderer);

    SDL_UnlockMutex(renderer->submitLock);
}

static SDL_GpuFence *D3D12_SubmitAndAcquireFence(
    SDL_GpuCommandBuffer *commandBuffer)
{
    D3D12CommandBuffer *d3d12CommandBuffer = (D3D12CommandBuffer *)commandBuffer;
    d3d12CommandBuffer->autoReleaseFence = SDL_FALSE;
    D3D12_Submit(commandBuffer);
    return (SDL_GpuFence *)d3d12CommandBuffer->inFlightFence;
}

static void D3D12_Wait(
    SDL_GpuRenderer *driverData)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12Fence *fence = D3D12_INTERNAL_AcquireFence(renderer);
    if (!fence) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to acquire fence.");
        return;
    }
    HRESULT res;

    SDL_LockMutex(renderer->submitLock);

    if (renderer->commandQueue) {
        /* Insert a signal into the end of the command queue... */
        ID3D12CommandQueue_Signal(
            renderer->commandQueue,
            fence->handle,
            D3D12_FENCE_SIGNAL_VALUE);

        /* ...and then block on it. */
        if (ID3D12Fence_GetCompletedValue(fence->handle) != D3D12_FENCE_SIGNAL_VALUE) {
            res = ID3D12Fence_SetEventOnCompletion(
                fence->handle,
                D3D12_FENCE_SIGNAL_VALUE,
                fence->event);
            ERROR_CHECK_RETURN("Setting fence event failed", )

            WaitForSingleObject(fence->event, INFINITE);
        }
    }

    D3D12_ReleaseFence(
        (SDL_GpuRenderer *)renderer,
        (SDL_GpuFence *)fence);

    /* Clean up */
    for (Sint32 i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1) {
        D3D12_INTERNAL_CleanCommandBuffer(renderer, renderer->submittedCommandBuffers[i]);
    }

    D3D12_INTERNAL_PerformPendingDestroys(renderer);

    SDL_UnlockMutex(renderer->submitLock);
}

static void D3D12_WaitForFences(
    SDL_GpuRenderer *driverData,
    SDL_bool waitAll,
    SDL_GpuFence **pFences,
    Uint32 fenceCount)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    D3D12Fence *fence;
    HANDLE *events = SDL_stack_alloc(HANDLE, fenceCount);
    HRESULT res;

    SDL_LockMutex(renderer->submitLock);

    for (Uint32 i = 0; i < fenceCount; i += 1) {
        fence = (D3D12Fence *)pFences[i];

        res = ID3D12Fence_SetEventOnCompletion(
            fence->handle,
            D3D12_FENCE_SIGNAL_VALUE,
            fence->event);
        ERROR_CHECK_RETURN("Setting fence event failed", )

        events[i] = fence->event;
    }

    WaitForMultipleObjects(
        fenceCount,
        events,
        waitAll,
        INFINITE);

    /* Check for cleanups */
    for (Sint32 i = renderer->submittedCommandBufferCount - 1; i >= 0; i -= 1) {
        Uint64 fenceValue = ID3D12Fence_GetCompletedValue(
            renderer->submittedCommandBuffers[i]->inFlightFence->handle);

        if (fenceValue == D3D12_FENCE_SIGNAL_VALUE) {
            D3D12_INTERNAL_CleanCommandBuffer(
                renderer,
                renderer->submittedCommandBuffers[i]);
        }
    }

    D3D12_INTERNAL_PerformPendingDestroys(renderer);

    SDL_stack_free(events);

    SDL_UnlockMutex(renderer->submitLock);
}

/* Feature Queries */

static SDL_bool D3D12_SupportsTextureFormat(
    SDL_GpuRenderer *driverData,
    SDL_GpuTextureFormat format,
    SDL_GpuTextureType type,
    SDL_GpuTextureUsageFlags usage)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    DXGI_FORMAT dxgiFormat = SDLToD3D12_TextureFormat[format];
    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport = { dxgiFormat, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
    HRESULT res;

    res = ID3D12Device_CheckFeatureSupport(
        renderer->device,
        D3D12_FEATURE_FORMAT_SUPPORT,
        &formatSupport,
        sizeof(formatSupport));
    if (FAILED(res)) {
        /* Format is apparently unknown */
        return SDL_FALSE;
    }

    /* Is the texture type supported? */
    if (type == SDL_GPU_TEXTURETYPE_2D && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)) {
        return SDL_FALSE;
    }
    if (type == SDL_GPU_TEXTURETYPE_3D && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE3D)) {
        return SDL_FALSE;
    }
    if (type == SDL_GPU_TEXTURETYPE_CUBE && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURECUBE)) {
        return SDL_FALSE;
    }

    /* Are the usage flags supported? */
    if ((usage & SDL_GPU_TEXTUREUSAGE_SAMPLER_BIT) && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)) {
        return SDL_FALSE;
    }
    if ((usage & (SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ_BIT | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ_BIT | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE_BIT)) && !(formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) {
        return SDL_FALSE;
    }
    if ((usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET_BIT) && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)) {
        return SDL_FALSE;
    }
    if ((usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET_BIT) && !(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL)) {
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

static SDL_GpuSampleCount D3D12_GetBestSampleCount(
    SDL_GpuRenderer *driverData,
    SDL_GpuTextureFormat format,
    SDL_GpuSampleCount desiredSampleCount)
{
    D3D12Renderer *renderer = (D3D12Renderer *)driverData;
    SDL_GpuSampleCount maxSupported = SDL_GPU_SAMPLECOUNT_8;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS featureData;
    HRESULT res;

    featureData.Flags = (D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS)0;
    while (maxSupported >= SDL_GPU_SAMPLECOUNT_1) {
        featureData.Format = SDLToD3D12_TextureFormat[format];
        featureData.SampleCount = (UINT)maxSupported;
        res = ID3D12Device_CheckFeatureSupport(
            renderer->device,
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &featureData,
            sizeof(featureData));

        if (SUCCEEDED(res) && featureData.NumQualityLevels > 0) {
            break;
        }
        maxSupported = (SDL_GpuSampleCount)((int)maxSupported - 1);
    }

    return (SDL_GpuSampleCount)SDL_min(maxSupported, desiredSampleCount);
}

static void D3D12_INTERNAL_InitBlitPipelines(
    D3D12Renderer *renderer)
{
    SDL_GpuShaderCreateInfo shaderCreateInfo;
    SDL_GpuShader *fullscreenVertexShader;
    SDL_GpuShader *blitFrom2DPixelShader;
    SDL_GpuGraphicsPipelineCreateInfo blitPipelineCreateInfo;
    SDL_GpuSamplerCreateInfo samplerCreateInfo;
    SDL_GpuVertexBinding binding;
    SDL_GpuVertexAttribute attribute;
    SDL_GpuColorAttachmentDescription colorAttachmentDesc;

    /* Fullscreen vertex shader */
    SDL_zero(shaderCreateInfo);
    shaderCreateInfo.code = (Uint8 *)D3D12_FullscreenVert;
    shaderCreateInfo.codeSize = sizeof(D3D12_FullscreenVert);
    shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    shaderCreateInfo.format = SDL_GPU_SHADERFORMAT_DXBC;
    shaderCreateInfo.entryPointName = "main";

    fullscreenVertexShader = D3D12_CreateShader(
        (SDL_GpuRenderer *)renderer,
        &shaderCreateInfo);

    if (fullscreenVertexShader == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to compile fullscreen vertex shader for blit!");
    }

    /* Blit from 2D pixel shader */
    shaderCreateInfo.code = (Uint8 *)D3D12_BlitFrom2D;
    shaderCreateInfo.codeSize = sizeof(D3D12_BlitFrom2D);
    shaderCreateInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shaderCreateInfo.samplerCount = 1;
    shaderCreateInfo.uniformBufferCount = 1;

    blitFrom2DPixelShader = D3D12_CreateShader(
        (SDL_GpuRenderer *)renderer,
        &shaderCreateInfo);

    if (blitFrom2DPixelShader == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to compile blit from 2D pixel shader!");
    }

    /* Blit from 2D pipeline */
    SDL_zero(blitPipelineCreateInfo);

    SDL_zero(colorAttachmentDesc);
    colorAttachmentDesc.blendState.colorWriteMask = 0xF;
    colorAttachmentDesc.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8; /* FIXME: we are gonna need a pipeline per output format */

    blitPipelineCreateInfo.attachmentInfo.colorAttachmentDescriptions = &colorAttachmentDesc;
    blitPipelineCreateInfo.attachmentInfo.colorAttachmentCount = 1;
    blitPipelineCreateInfo.attachmentInfo.depthStencilFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM; /* arbitrary */
    blitPipelineCreateInfo.attachmentInfo.hasDepthStencilAttachment = 0;

    binding.binding = 0;
    binding.inputRate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    binding.stepRate = 0;
    binding.stride = 64;

    attribute.binding = 0;
    attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_VECTOR2;
    attribute.location = 0;
    attribute.offset = 0;

    blitPipelineCreateInfo.vertexInputState.vertexAttributeCount = 1;
    blitPipelineCreateInfo.vertexInputState.vertexAttributes = &attribute;
    blitPipelineCreateInfo.vertexInputState.vertexBindingCount = 1;
    blitPipelineCreateInfo.vertexInputState.vertexBindings = &binding;

    blitPipelineCreateInfo.vertexShader = fullscreenVertexShader;
    blitPipelineCreateInfo.fragmentShader = blitFrom2DPixelShader;

    blitPipelineCreateInfo.multisampleState.sampleCount = SDL_GPU_SAMPLECOUNT_1;
    blitPipelineCreateInfo.multisampleState.sampleMask = 0xFFFFFFFF;

    blitPipelineCreateInfo.primitiveType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

    blitPipelineCreateInfo.blendConstants[0] = 1.0f;
    blitPipelineCreateInfo.blendConstants[1] = 1.0f;
    blitPipelineCreateInfo.blendConstants[2] = 1.0f;
    blitPipelineCreateInfo.blendConstants[3] = 1.0f;

    renderer->blitFrom2DPipeline = D3D12_CreateGraphicsPipeline(
        (SDL_GpuRenderer *)renderer,
        &blitPipelineCreateInfo);

    if (renderer->blitFrom2DPipeline == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create blit pipeline!");
    }

    /* Create samplers */
    samplerCreateInfo.addressModeU = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeV = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerCreateInfo.addressModeW = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerCreateInfo.anisotropyEnable = 0;
    samplerCreateInfo.compareEnable = 0;
    samplerCreateInfo.magFilter = SDL_GPU_FILTER_NEAREST;
    samplerCreateInfo.minFilter = SDL_GPU_FILTER_NEAREST;
    samplerCreateInfo.mipmapMode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerCreateInfo.mipLodBias = 0.0f;
    samplerCreateInfo.minLod = 0;
    samplerCreateInfo.maxLod = 1000;
    samplerCreateInfo.maxAnisotropy = 1.0f;
    samplerCreateInfo.compareOp = SDL_GPU_COMPAREOP_ALWAYS;

    renderer->blitNearestSampler = D3D12_CreateSampler(
        (SDL_GpuRenderer *)renderer,
        &samplerCreateInfo);

    if (renderer->blitNearestSampler == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create blit nearest sampler!");
    }

    samplerCreateInfo.magFilter = SDL_GPU_FILTER_LINEAR;
    samplerCreateInfo.minFilter = SDL_GPU_FILTER_LINEAR;
    samplerCreateInfo.mipmapMode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;

    renderer->blitLinearSampler = D3D12_CreateSampler(
        (SDL_GpuRenderer *)renderer,
        &samplerCreateInfo);

    if (renderer->blitLinearSampler == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Failed to create blit linear sampler!");
    }

    /* Clean up */
    D3D12_ReleaseShader((SDL_GpuRenderer *)renderer, fullscreenVertexShader);
    D3D12_ReleaseShader((SDL_GpuRenderer *)renderer, blitFrom2DPixelShader);
}

static SDL_bool D3D12_PrepareDriver(SDL_VideoDevice *_this)
{
    void *d3d12_dll;
    void *dxgi_dll;
    PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceFunc;
    PFN_CREATE_DXGI_FACTORY1 CreateDXGIFactoryFunc;
    HRESULT res;
    ID3D12Device *device;
    IDXGIFactory1 *factory;
    IDXGIFactory4 *factory4;
    IDXGIFactory6 *factory6;
    IDXGIAdapter1 *adapter;

    /* Can we load D3D12? */

    d3d12_dll = SDL_LoadObject(D3D12_DLL);
    if (d3d12_dll == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not find " D3D12_DLL);
        return SDL_FALSE;
    }

    D3D12CreateDeviceFunc = (PFN_D3D12_CREATE_DEVICE)SDL_LoadFunction(
        d3d12_dll,
        D3D12_CREATE_DEVICE_FUNC);
    if (D3D12CreateDeviceFunc == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not find function " D3D12_CREATE_DEVICE_FUNC " in " D3D12_DLL);
        SDL_UnloadObject(d3d12_dll);
        return SDL_FALSE;
    }

    /* Can we load DXGI? */

    dxgi_dll = SDL_LoadObject(DXGI_DLL);
    if (dxgi_dll == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not find " DXGI_DLL);
        return SDL_FALSE;
    }

    CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY1)SDL_LoadFunction(
        dxgi_dll,
        CREATE_DXGI_FACTORY1_FUNC);
    if (CreateDXGIFactoryFunc == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not find function " CREATE_DXGI_FACTORY1_FUNC " in " DXGI_DLL);
        SDL_UnloadObject(dxgi_dll);
        return SDL_FALSE;
    }

    /* Can we create a device? */

    /* Create the DXGI factory */
    res = CreateDXGIFactoryFunc(
        &D3D_IID_IDXGIFactory1,
        (void **)&factory);
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not create DXGIFactory");
        SDL_UnloadObject(d3d12_dll);
        SDL_UnloadObject(dxgi_dll);
        return SDL_FALSE;
    }

    /* Check for DXGI 1.4 support */
    res = IDXGIFactory1_QueryInterface(
        factory,
        D3D_GUID(D3D_IID_IDXGIFactory4),
        (void **)&factory4);
    if (FAILED(res)) {
        IDXGIFactory1_Release(factory);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Failed to find DXGI1.4 support, required for DX12");
        SDL_UnloadObject(d3d12_dll);
        SDL_UnloadObject(dxgi_dll);
        return SDL_FALSE;
    }
    IDXGIFactory4_Release(factory4);

    res = IDXGIFactory1_QueryInterface(
        factory,
        D3D_GUID(D3D_IID_IDXGIFactory6),
        (void **)&factory6);
    if (SUCCEEDED(res)) {
        res = IDXGIFactory6_EnumAdapterByGpuPreference(
            factory6,
            0,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            D3D_GUID(D3D_IID_IDXGIAdapter1),
            (void **)&adapter);
        IDXGIFactory6_Release(factory6);
    } else {
        res = IDXGIFactory1_EnumAdapters1(
            factory,
            0,
            &adapter);
    }
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Failed to find adapter for D3D12Device");
        IDXGIFactory1_Release(factory);
        SDL_UnloadObject(d3d12_dll);
        SDL_UnloadObject(dxgi_dll);
        return SDL_FALSE;
    }

    res = D3D12CreateDeviceFunc(
        (IUnknown *)adapter,
        D3D_FEATURE_LEVEL_CHOICE,
        D3D_GUID(D3D_IID_ID3D12Device),
        (void **)&device);

    if (SUCCEEDED(res)) {
        ID3D12Device_Release(device);
    }
    IDXGIAdapter1_Release(adapter);
    IDXGIFactory1_Release(factory);

    SDL_UnloadObject(d3d12_dll);
    SDL_UnloadObject(dxgi_dll);

    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "D3D12: Could not create D3D12Device with feature level " D3D_FEATURE_LEVEL_CHOICE_STR);
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

static void D3D12_INTERNAL_TryInitializeDXGIDebug(D3D12Renderer *renderer)
{
    PFN_DXGI_GET_DEBUG_INTERFACE DXGIGetDebugInterfaceFunc;
    HRESULT res;

    renderer->dxgidebug_dll = SDL_LoadObject(DXGIDEBUG_DLL);
    if (renderer->dxgidebug_dll == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not find " DXGIDEBUG_DLL);
        return;
    }

    DXGIGetDebugInterfaceFunc = (PFN_DXGI_GET_DEBUG_INTERFACE)SDL_LoadFunction(
        renderer->dxgidebug_dll,
        DXGI_GET_DEBUG_INTERFACE_FUNC);
    if (DXGIGetDebugInterfaceFunc == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load function: " DXGI_GET_DEBUG_INTERFACE_FUNC);
        return;
    }

    res = DXGIGetDebugInterfaceFunc(&D3D_IID_IDXGIDebug, (void **)&renderer->dxgiDebug);
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not get IDXGIDebug interface");
    }

    res = DXGIGetDebugInterfaceFunc(&D3D_IID_IDXGIInfoQueue, (void **)&renderer->dxgiInfoQueue);
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not get IDXGIInfoQueue interface");
    }
}

static void D3D12_INTERNAL_TryInitializeD3D12Debug(D3D12Renderer *renderer)
{
    PFN_D3D12_GET_DEBUG_INTERFACE D3D12GetDebugInterfaceFunc;
    HRESULT res;

    D3D12GetDebugInterfaceFunc = (PFN_D3D12_GET_DEBUG_INTERFACE)SDL_LoadFunction(
        renderer->d3d12_dll,
        D3D12_GET_DEBUG_INTERFACE_FUNC);
    if (D3D12GetDebugInterfaceFunc == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Could not load function: " D3D12_GET_DEBUG_INTERFACE_FUNC);
        return;
    }

    res = D3D12GetDebugInterfaceFunc(D3D_GUID(D3D_IID_ID3D12Debug), (void **)&renderer->d3d12Debug);
    if (FAILED(res)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not get ID3D12Debug interface");
        return;
    }

    ID3D12Debug_EnableDebugLayer(renderer->d3d12Debug);
}

static void D3D12_INTERNAL_TryInitializeD3D12DebugInfoQueue(D3D12Renderer *renderer)
{
    ID3D12InfoQueue *infoQueue = NULL;
    D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
    D3D12_INFO_QUEUE_FILTER filter;
    HRESULT res;

    res = ID3D12Device_QueryInterface(
        renderer->device,
        D3D_GUID(D3D_IID_ID3D12InfoQueue),
        (void **)&infoQueue);
    if (FAILED(res)) {
        ERROR_CHECK_RETURN("Failed to convert ID3D12Device to ID3D12InfoQueue", );
    }

    SDL_zero(filter);
    filter.DenyList.NumSeverities = 1;
    filter.DenyList.pSeverityList = severities;
    ID3D12InfoQueue_PushStorageFilter(
        infoQueue,
        &filter);

    ID3D12InfoQueue_SetBreakOnSeverity(
        infoQueue,
        D3D12_MESSAGE_SEVERITY_ERROR,
        SDL_TRUE);

    ID3D12InfoQueue_SetBreakOnSeverity(
        infoQueue,
        D3D12_MESSAGE_SEVERITY_CORRUPTION,
        SDL_TRUE);

    ID3D12InfoQueue_Release(infoQueue);
}

static SDL_GpuDevice *D3D12_CreateDevice(SDL_bool debugMode, SDL_bool preferLowPower, SDL_PropertiesID props)
{
    SDL_GpuDevice *result;
    D3D12Renderer *renderer;
    PFN_CREATE_DXGI_FACTORY1 CreateDXGIFactoryFunc;
    HRESULT res;
    IDXGIFactory1 *factory1;
    IDXGIFactory5 *factory5;
    IDXGIFactory6 *factory6;
    DXGI_ADAPTER_DESC1 adapterDesc;
    PFN_D3D12_CREATE_DEVICE D3D12CreateDeviceFunc;
    D3D12_FEATURE_DATA_ARCHITECTURE architecture;
    D3D12_COMMAND_QUEUE_DESC queueDesc;

    renderer = (D3D12Renderer *)SDL_calloc(1, sizeof(D3D12Renderer));

    /* Load the DXGI library */
    renderer->dxgi_dll = SDL_LoadObject(DXGI_DLL);
    if (renderer->dxgi_dll == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not find " DXGI_DLL);
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Initialize the DXGI debug layer, if applicable */
    if (debugMode) {
        D3D12_INTERNAL_TryInitializeDXGIDebug(renderer);
    }

    /* Load the CreateDXGIFactory1 function */
    CreateDXGIFactoryFunc = (PFN_CREATE_DXGI_FACTORY1)SDL_LoadFunction(
        renderer->dxgi_dll,
        CREATE_DXGI_FACTORY1_FUNC);
    if (CreateDXGIFactoryFunc == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load function: " CREATE_DXGI_FACTORY1_FUNC);
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Create the DXGI factory */
    res = CreateDXGIFactoryFunc(
        &D3D_IID_IDXGIFactory1,
        (void **)&factory1);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create DXGIFactory", NULL);
    }

    /* Check for DXGI 1.4 support */
    res = IDXGIFactory1_QueryInterface(
        factory1,
        D3D_GUID(D3D_IID_IDXGIFactory4),
        (void **)&renderer->factory);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("DXGI1.4 support not found, required for DX12", NULL);
    }
    IDXGIFactory1_Release(factory1);

    /* Check for explicit tearing support */
    res = IDXGIFactory4_QueryInterface(
        renderer->factory,
        D3D_GUID(D3D_IID_IDXGIFactory5),
        (void **)&factory5);
    if (SUCCEEDED(res)) {
        res = IDXGIFactory5_CheckFeatureSupport(
            factory5,
            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
            &renderer->supportsTearing,
            sizeof(renderer->supportsTearing));
        if (FAILED(res)) {
            renderer->supportsTearing = SDL_FALSE;
        }
        IDXGIFactory5_Release(factory5);
    }

    /* Select the appropriate device for rendering */
    res = IDXGIFactory4_QueryInterface(
        renderer->factory,
        D3D_GUID(D3D_IID_IDXGIFactory6),
        (void **)&factory6);
    if (SUCCEEDED(res)) {
        res = IDXGIFactory6_EnumAdapterByGpuPreference(
            factory6,
            0,
            preferLowPower ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            D3D_GUID(D3D_IID_IDXGIAdapter1),
            (void **)&renderer->adapter);
        IDXGIFactory6_Release(factory6);
    } else {
        res = IDXGIFactory4_EnumAdapters1(
            renderer->factory,
            0,
            &renderer->adapter);
    }

    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not find adapter for D3D12Device", NULL);
    }

    /* Get information about the selected adapter. Used for logging info. */
    res = IDXGIAdapter1_GetDesc1(renderer->adapter, &adapterDesc);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not get adapter description", NULL);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "SDL_Gpu Driver: D3D12");
    SDL_LogInfo(SDL_LOG_CATEGORY_GPU, "D3D12 Adapter: %S", adapterDesc.Description);

    /* Load the D3D library */
    renderer->d3d12_dll = SDL_LoadObject(D3D12_DLL);
    if (renderer->d3d12_dll == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not find " D3D12_DLL);
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Load the CreateDevice function */
    D3D12CreateDeviceFunc = (PFN_D3D12_CREATE_DEVICE)SDL_LoadFunction(
        renderer->d3d12_dll,
        D3D12_CREATE_DEVICE_FUNC);
    if (D3D12CreateDeviceFunc == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load function: " D3D12_CREATE_DEVICE_FUNC);
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->D3D12SerializeRootSignature_func = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)SDL_LoadFunction(
        renderer->d3d12_dll,
        D3D12_SERIALIZE_ROOT_SIGNATURE_FUNC);
    if (renderer->D3D12SerializeRootSignature_func == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load function: " D3D12_SERIALIZE_ROOT_SIGNATURE_FUNC);
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Initialize the D3D12 debug layer, if applicable */
    if (debugMode) {
        D3D12_INTERNAL_TryInitializeD3D12Debug(renderer);
    }

    /* Create the D3D12Device */
    res = D3D12CreateDeviceFunc(
        (IUnknown *)renderer->adapter,
        D3D_FEATURE_LEVEL_CHOICE,
        D3D_GUID(D3D_IID_ID3D12Device),
        (void **)&renderer->device);

    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create D3D12Device", NULL);
    }

    /* Initialize the D3D12 debug info queue, if applicable */
    if (debugMode) {
        D3D12_INTERNAL_TryInitializeD3D12DebugInfoQueue(renderer);
    }

    /* Check UMA */
    architecture.NodeIndex = 0;
    res = ID3D12Device_CheckFeatureSupport(
        renderer->device,
        D3D12_FEATURE_ARCHITECTURE,
        &architecture,
        sizeof(D3D12_FEATURE_DATA_ARCHITECTURE));
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not get device architecture", NULL);
    }

    renderer->UMA = (SDL_bool)architecture.UMA;
    renderer->UMACacheCoherent = (SDL_bool)architecture.CacheCoherentUMA;

    /* Check "GPU Upload Heap" support (for fast uniform buffers) */
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16; /* 15 wasn't enough, huh? */
    renderer->GPUUploadHeapSupported = SDL_FALSE;
    res = ID3D12Device_CheckFeatureSupport(
        renderer->device,
        D3D12_FEATURE_D3D12_OPTIONS16,
        &options16,
        sizeof(options16));

    if (SUCCEEDED(res)) {
        renderer->GPUUploadHeapSupported = options16.GPUUploadHeapSupported;
    }

    /* Create command queue */
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.NodeMask = 0;
    queueDesc.Priority = 0;

    res = ID3D12Device_CreateCommandQueue(
        renderer->device,
        &queueDesc,
        D3D_GUID(D3D_IID_ID3D12CommandQueue),
        (void **)&renderer->commandQueue);

    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create D3D12CommandQueue", NULL);
    }

    /* Create indirect command signatures */

    D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc;
    D3D12_INDIRECT_ARGUMENT_DESC indirectArgumentDesc;
    SDL_zero(indirectArgumentDesc);

    indirectArgumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    commandSignatureDesc.NodeMask = 0;
    commandSignatureDesc.ByteStride = 16;
    commandSignatureDesc.NumArgumentDescs = 1;
    commandSignatureDesc.pArgumentDescs = &indirectArgumentDesc;

    res = ID3D12Device_CreateCommandSignature(
        renderer->device,
        &commandSignatureDesc,
        NULL,
        D3D_GUID(D3D_IID_ID3D12CommandSignature),
        (void **)&renderer->indirectDrawCommandSignature);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create indirect draw command signature", NULL)
    }

    indirectArgumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    commandSignatureDesc.ByteStride = 20;
    commandSignatureDesc.pArgumentDescs = &indirectArgumentDesc;

    res = ID3D12Device_CreateCommandSignature(
        renderer->device,
        &commandSignatureDesc,
        NULL,
        D3D_GUID(D3D_IID_ID3D12CommandSignature),
        (void **)&renderer->indirectIndexedDrawCommandSignature);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create indirect indexed draw command signature", NULL)
    }

    indirectArgumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    commandSignatureDesc.ByteStride = 12;
    commandSignatureDesc.pArgumentDescs = &indirectArgumentDesc;

    res = ID3D12Device_CreateCommandSignature(
        renderer->device,
        &commandSignatureDesc,
        NULL,
        D3D_GUID(D3D_IID_ID3D12CommandSignature),
        (void **)&renderer->indirectDispatchCommandSignature);
    if (FAILED(res)) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        ERROR_CHECK_RETURN("Could not create indirect dispatch command signature", NULL)
    }

    /* Initialize pools */

    renderer->submittedCommandBufferCapacity = 4;
    renderer->submittedCommandBufferCount = 0;
    renderer->submittedCommandBuffers = (D3D12CommandBuffer **)SDL_calloc(
        renderer->submittedCommandBufferCapacity, sizeof(D3D12CommandBuffer *));
    if (!renderer->submittedCommandBuffers) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->uniformBufferPoolCapacity = 4;
    renderer->uniformBufferPoolCount = 0;
    renderer->uniformBufferPool = (D3D12UniformBuffer **)SDL_calloc(
        renderer->uniformBufferPoolCapacity, sizeof(D3D12UniformBuffer *));
    if (!renderer->uniformBufferPool) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->claimedWindowCapacity = 4;
    renderer->claimedWindowCount = 0;
    renderer->claimedWindows = (D3D12WindowData **)SDL_calloc(
        renderer->claimedWindowCapacity, sizeof(D3D12WindowData *));
    if (!renderer->claimedWindows) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->availableFenceCapacity = 4;
    renderer->availableFenceCount = 0;
    renderer->availableFences = (D3D12Fence **)SDL_calloc(
        renderer->availableFenceCapacity, sizeof(D3D12Fence *));
    if (!renderer->availableFences) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Initialize CPU descriptor heaps */
    for (Uint32 i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; i += 1) {
        renderer->stagingDescriptorHeaps[i] = D3D12_INTERNAL_CreateDescriptorHeap(
            renderer,
            (D3D12_DESCRIPTOR_HEAP_TYPE)i,
            (i <= D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) ? VIEW_SAMPLER_STAGING_DESCRIPTOR_COUNT : TARGET_STAGING_DESCRIPTOR_COUNT,
            SDL_TRUE);
    }

    /* Initialize GPU descriptor heaps */
    for (Uint32 i = 0; i < 2; i += 1) {
        renderer->descriptorHeapPools[i].lock = SDL_CreateMutex();
        renderer->descriptorHeapPools[i].capacity = 4;
        renderer->descriptorHeapPools[i].count = 4;
        renderer->descriptorHeapPools[i].heaps = (D3D12DescriptorHeap **)SDL_calloc(
            renderer->descriptorHeapPools[i].capacity, sizeof(D3D12DescriptorHeap *));

        for (Uint32 j = 0; j < renderer->descriptorHeapPools[i].capacity; j += 1) {
            renderer->descriptorHeapPools[i].heaps[j] = D3D12_INTERNAL_CreateDescriptorHeap(
                renderer,
                (D3D12_DESCRIPTOR_HEAP_TYPE)i,
                i == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ? VIEW_GPU_DESCRIPTOR_COUNT : SAMPLER_GPU_DESCRIPTOR_COUNT,
                SDL_FALSE);
        }
    }

    /* Deferred resource releasing */

    renderer->buffersToDestroyCapacity = 4;
    renderer->buffersToDestroyCount = 0;
    renderer->buffersToDestroy = (D3D12Buffer **)SDL_calloc(
        renderer->buffersToDestroyCapacity, sizeof(D3D12Buffer *));
    if (!renderer->buffersToDestroy) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->texturesToDestroyCapacity = 4;
    renderer->texturesToDestroyCount = 0;
    renderer->texturesToDestroy = (D3D12Texture **)SDL_calloc(
        renderer->texturesToDestroyCapacity, sizeof(D3D12Texture *));
    if (!renderer->texturesToDestroy) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->samplersToDestroyCapacity = 4;
    renderer->samplersToDestroyCount = 0;
    renderer->samplersToDestroy = (D3D12Sampler **)SDL_calloc(
        renderer->samplersToDestroyCapacity, sizeof(D3D12Sampler *));
    if (!renderer->samplersToDestroy) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->graphicsPipelinesToDestroyCapacity = 4;
    renderer->graphicsPipelinesToDestroyCount = 0;
    renderer->graphicsPipelinesToDestroy = (D3D12GraphicsPipeline **)SDL_calloc(
        renderer->graphicsPipelinesToDestroyCapacity, sizeof(D3D12GraphicsPipeline *));
    if (!renderer->graphicsPipelinesToDestroy) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    renderer->computePipelinesToDestroyCapacity = 4;
    renderer->computePipelinesToDestroyCount = 0;
    renderer->computePipelinesToDestroy = (D3D12ComputePipeline **)SDL_calloc(
        renderer->computePipelinesToDestroyCapacity, sizeof(D3D12ComputePipeline *));
    if (!renderer->computePipelinesToDestroy) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    /* Locks */
    renderer->stagingDescriptorHeapLock = SDL_CreateMutex();
    renderer->acquireCommandBufferLock = SDL_CreateMutex();
    renderer->acquireUniformBufferLock = SDL_CreateMutex();
    renderer->submitLock = SDL_CreateMutex();
    renderer->windowLock = SDL_CreateMutex();
    renderer->fenceLock = SDL_CreateMutex();
    renderer->disposeLock = SDL_CreateMutex();

    renderer->debugMode = debugMode;

    renderer->semantic = SDL_GetStringProperty(props, SDL_PROP_GPU_CREATEDEVICE_D3D12_SEMANTIC_NAME_STRING, "TEXCOORD");

    /* Blit pipelines */
    D3D12_INTERNAL_InitBlitPipelines(renderer);

    /* Create the SDL_Gpu Device */
    result = (SDL_GpuDevice *)SDL_calloc(1, sizeof(SDL_GpuDevice));

    if (!result) {
        D3D12_INTERNAL_DestroyRenderer(renderer);
        return NULL;
    }

    ASSIGN_DRIVER(D3D12)
    result->driverData = (SDL_GpuRenderer *)renderer;
    result->debugMode = debugMode;

    return result;
}

SDL_GpuBootstrap D3D12Driver = {
    "D3D12",
    SDL_GPU_DRIVER_D3D12,
    D3D12_PrepareDriver,
    D3D12_CreateDevice
};

#endif /* SDL_GPU_D3D12 */