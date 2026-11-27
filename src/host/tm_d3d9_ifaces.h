/*
 * tm_d3d9_ifaces.h - the Direct3D 9 interfaces a guest can hold, with each
 * vtable in order and each method's argument count after `this` (read from
 * d3d9.h). The DXVK backend (tm_d3d9_dxvk.cpp) gives every guest object of
 * these interfaces a vtable of shims made from this table.
 */
#ifndef TM_D3D9_IFACES_H
#define TM_D3D9_IFACES_H
#include <cstdint>

namespace tmd3d {

struct Method { const char *name; int nargs; };
struct Guid { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; };

enum Kind {
    DEVICE, SWAPCHAIN, SURFACE, VOLUME, TEXTURE, CUBETEXTURE, VOLUMETEXTURE, VERTEXBUFFER, INDEXBUFFER,
    VERTEXDECL, VERTEXSHADER, PIXELSHADER, STATEBLOCK, QUERY, NKIND
};

#define TM_IUNKNOWN { "QueryInterface", 2 }, { "AddRef", 0 }, { "Release", 0 }
#define TM_RESOURCE TM_IUNKNOWN, { "GetDevice", 1 }, { "SetPrivateData", 4 }, { "GetPrivateData", 3 }, \
    { "FreePrivateData", 1 }, { "SetPriority", 1 }, { "GetPriority", 0 }, { "PreLoad", 0 }, { "GetType", 0 }
#define TM_BASETEX TM_RESOURCE, { "SetLOD", 1 }, { "GetLOD", 0 }, { "GetLevelCount", 0 }, \
    { "SetAutoGenFilterType", 1 }, { "GetAutoGenFilterType", 0 }, { "GenerateMipSubLevels", 0 }

constexpr Method k_device[] = {
    TM_IUNKNOWN, { "TestCooperativeLevel", 0 },
    { "GetAvailableTextureMem", 0 }, { "EvictManagedResources", 0 }, { "GetDirect3D", 1 }, { "GetDeviceCaps", 1 },
    { "GetDisplayMode", 2 }, { "GetCreationParameters", 1 }, { "SetCursorProperties", 3 }, { "SetCursorPosition", 3 },
    { "ShowCursor", 1 }, { "CreateAdditionalSwapChain", 2 }, { "GetSwapChain", 2 }, { "GetNumberOfSwapChains", 0 },
    { "Reset", 1 }, { "Present", 4 }, { "GetBackBuffer", 4 }, { "GetRasterStatus", 2 }, { "SetDialogBoxMode", 1 },
    { "SetGammaRamp", 3 }, { "GetGammaRamp", 2 }, { "CreateTexture", 8 }, { "CreateVolumeTexture", 9 },
    { "CreateCubeTexture", 7 }, { "CreateVertexBuffer", 6 }, { "CreateIndexBuffer", 6 }, { "CreateRenderTarget", 8 },
    { "CreateDepthStencilSurface", 8 }, { "UpdateSurface", 4 }, { "UpdateTexture", 2 }, { "GetRenderTargetData", 2 },
    { "GetFrontBufferData", 2 }, { "StretchRect", 5 }, { "ColorFill", 3 }, { "CreateOffscreenPlainSurface", 6 },
    { "SetRenderTarget", 2 }, { "GetRenderTarget", 2 }, { "SetDepthStencilSurface", 1 }, { "GetDepthStencilSurface", 1 },
    { "BeginScene", 0 }, { "EndScene", 0 }, { "Clear", 6 }, { "SetTransform", 2 }, { "GetTransform", 2 },
    { "MultiplyTransform", 2 }, { "SetViewport", 1 }, { "GetViewport", 1 }, { "SetMaterial", 1 }, { "GetMaterial", 1 },
    { "SetLight", 2 }, { "GetLight", 2 }, { "LightEnable", 2 }, { "GetLightEnable", 2 }, { "SetClipPlane", 2 },
    { "GetClipPlane", 2 }, { "SetRenderState", 2 }, { "GetRenderState", 2 }, { "CreateStateBlock", 2 },
    { "BeginStateBlock", 0 }, { "EndStateBlock", 1 }, { "SetClipStatus", 1 }, { "GetClipStatus", 1 },
    { "GetTexture", 2 }, { "SetTexture", 2 }, { "GetTextureStageState", 3 }, { "SetTextureStageState", 3 },
    { "GetSamplerState", 3 }, { "SetSamplerState", 3 }, { "ValidateDevice", 1 }, { "SetPaletteEntries", 2 },
    { "GetPaletteEntries", 2 }, { "SetCurrentTexturePalette", 1 }, { "GetCurrentTexturePalette", 1 },
    { "SetScissorRect", 1 }, { "GetScissorRect", 1 }, { "SetSoftwareVertexProcessing", 1 },
    { "GetSoftwareVertexProcessing", 0 }, { "SetNPatchMode", 1 }, { "GetNPatchMode", 0 }, { "DrawPrimitive", 3 },
    { "DrawIndexedPrimitive", 6 }, { "DrawPrimitiveUP", 4 }, { "DrawIndexedPrimitiveUP", 8 }, { "ProcessVertices", 6 },
    { "CreateVertexDeclaration", 2 }, { "SetVertexDeclaration", 1 }, { "GetVertexDeclaration", 1 }, { "SetFVF", 1 },
    { "GetFVF", 1 }, { "CreateVertexShader", 2 }, { "SetVertexShader", 1 }, { "GetVertexShader", 1 },
    { "SetVertexShaderConstantF", 3 }, { "GetVertexShaderConstantF", 3 }, { "SetVertexShaderConstantI", 3 },
    { "GetVertexShaderConstantI", 3 }, { "SetVertexShaderConstantB", 3 }, { "GetVertexShaderConstantB", 3 },
    { "SetStreamSource", 4 }, { "GetStreamSource", 4 }, { "SetStreamSourceFreq", 2 }, { "GetStreamSourceFreq", 2 },
    { "SetIndices", 1 }, { "GetIndices", 1 }, { "CreatePixelShader", 2 }, { "SetPixelShader", 1 },
    { "GetPixelShader", 1 }, { "SetPixelShaderConstantF", 3 }, { "GetPixelShaderConstantF", 3 },
    { "SetPixelShaderConstantI", 3 }, { "GetPixelShaderConstantI", 3 }, { "SetPixelShaderConstantB", 3 },
    { "GetPixelShaderConstantB", 3 }, { "DrawRectPatch", 3 }, { "DrawTriPatch", 3 }, { "DeletePatch", 1 },
    { "CreateQuery", 2 },
};
constexpr Method k_swapchain[] = {
    TM_IUNKNOWN, { "Present", 5 }, { "GetFrontBufferData", 1 }, { "GetBackBuffer", 3 }, { "GetRasterStatus", 1 },
    { "GetDisplayMode", 1 }, { "GetDevice", 1 }, { "GetPresentParameters", 1 },
};
constexpr Method k_surface[] = {
    TM_RESOURCE, { "GetContainer", 2 }, { "GetDesc", 1 }, { "LockRect", 3 }, { "UnlockRect", 0 }, { "GetDC", 1 },
    { "ReleaseDC", 1 },
};
constexpr Method k_volume[] = {
    TM_IUNKNOWN, { "GetDevice", 1 }, { "SetPrivateData", 4 }, { "GetPrivateData", 3 }, { "FreePrivateData", 1 },
    { "GetContainer", 2 }, { "GetDesc", 1 }, { "LockBox", 3 }, { "UnlockBox", 0 },
};
constexpr Method k_texture[] = {
    TM_BASETEX, { "GetLevelDesc", 2 }, { "GetSurfaceLevel", 2 }, { "LockRect", 4 }, { "UnlockRect", 1 },
    { "AddDirtyRect", 1 },
};
constexpr Method k_cubetexture[] = {
    TM_BASETEX, { "GetLevelDesc", 2 }, { "GetCubeMapSurface", 3 }, { "LockRect", 5 }, { "UnlockRect", 2 },
    { "AddDirtyRect", 2 },
};
constexpr Method k_volumetexture[] = {
    TM_BASETEX, { "GetLevelDesc", 2 }, { "GetVolumeLevel", 2 }, { "LockBox", 4 }, { "UnlockBox", 1 },
    { "AddDirtyBox", 1 },
};
constexpr Method k_vertexbuffer[] = { TM_RESOURCE, { "Lock", 4 }, { "Unlock", 0 }, { "GetDesc", 1 } };
constexpr Method k_indexbuffer[] = { TM_RESOURCE, { "Lock", 4 }, { "Unlock", 0 }, { "GetDesc", 1 } };
constexpr Method k_vertexdecl[] = { TM_IUNKNOWN, { "GetDevice", 1 }, { "GetDeclaration", 2 } };
constexpr Method k_vertexshader[] = { TM_IUNKNOWN, { "GetDevice", 1 }, { "GetFunction", 2 } };
constexpr Method k_pixelshader[] = { TM_IUNKNOWN, { "GetDevice", 1 }, { "GetFunction", 2 } };
constexpr Method k_stateblock[] = { TM_IUNKNOWN, { "GetDevice", 1 }, { "Capture", 0 }, { "Apply", 0 } };
constexpr Method k_query[] = {
    TM_IUNKNOWN, { "GetDevice", 1 }, { "GetType", 0 }, { "GetDataSize", 0 }, { "Issue", 1 }, { "GetData", 3 },
};
#undef TM_IUNKNOWN
#undef TM_RESOURCE
#undef TM_BASETEX

/* the interface's own IID, and the base interfaces it also answers for
 * (IDirect3DResource9, IDirect3DBaseTexture9) */
enum Base { B_NONE = 0, B_RESOURCE = 1, B_BASETEX = 2 };
struct Iface { const char *name; const Method *m; int n; Guid iid; int bases; };
#define TM_N(a) (int)(sizeof a / sizeof a[0])
constexpr Iface k_if[NKIND] = {
    { "IDirect3DDevice9", k_device, TM_N(k_device),
      { 0xD0223B96, 0xBF7A, 0x43FD, { 0x92, 0xBD, 0xA4, 0x3B, 0x0D, 0x82, 0xB9, 0xEB } }, B_NONE },
    { "IDirect3DSwapChain9", k_swapchain, TM_N(k_swapchain),
      { 0x794950F2, 0xADFC, 0x458A, { 0x90, 0x5E, 0x10, 0xA1, 0x0B, 0x0B, 0x50, 0x3B } }, B_NONE },
    { "IDirect3DSurface9", k_surface, TM_N(k_surface),
      { 0x0CFBAF3A, 0x9FF6, 0x429A, { 0x99, 0xB3, 0xA2, 0x79, 0x6A, 0xF8, 0xB8, 0x9B } }, B_RESOURCE },
    { "IDirect3DVolume9", k_volume, TM_N(k_volume),
      { 0x24F416E6, 0x1F67, 0x4AA7, { 0xB8, 0x8E, 0xD3, 0x3F, 0x6F, 0x31, 0x28, 0xA1 } }, B_NONE },
    { "IDirect3DTexture9", k_texture, TM_N(k_texture),
      { 0x85C31227, 0x3DE5, 0x4F00, { 0x9B, 0x3A, 0xF1, 0x1A, 0xC3, 0x8C, 0x18, 0xB5 } }, B_RESOURCE | B_BASETEX },
    { "IDirect3DCubeTexture9", k_cubetexture, TM_N(k_cubetexture),
      { 0xFFF32F81, 0xD953, 0x473A, { 0x92, 0x23, 0x93, 0xD6, 0x52, 0xAB, 0xA9, 0x3F } }, B_RESOURCE | B_BASETEX },
    { "IDirect3DVolumeTexture9", k_volumetexture, TM_N(k_volumetexture),
      { 0x2518526C, 0xE789, 0x4111, { 0xA7, 0xB9, 0x47, 0xEF, 0x32, 0x8D, 0x13, 0xE6 } }, B_RESOURCE | B_BASETEX },
    { "IDirect3DVertexBuffer9", k_vertexbuffer, TM_N(k_vertexbuffer),
      { 0xB64BB1B5, 0xFD70, 0x4DF6, { 0xBF, 0x91, 0x19, 0xD0, 0xA1, 0x24, 0x55, 0xE3 } }, B_RESOURCE },
    { "IDirect3DIndexBuffer9", k_indexbuffer, TM_N(k_indexbuffer),
      { 0x7C9DD65E, 0xD3F7, 0x4529, { 0xAC, 0xEE, 0x78, 0x58, 0x30, 0xAC, 0xDE, 0x35 } }, B_RESOURCE },
    { "IDirect3DVertexDeclaration9", k_vertexdecl, TM_N(k_vertexdecl),
      { 0xDD13C59C, 0x36FA, 0x4098, { 0xA8, 0xFB, 0xC7, 0xED, 0x39, 0xDC, 0x85, 0x46 } }, B_NONE },
    { "IDirect3DVertexShader9", k_vertexshader, TM_N(k_vertexshader),
      { 0xEFC5557E, 0x6265, 0x4613, { 0x8A, 0x94, 0x43, 0x85, 0x78, 0x89, 0xEB, 0x36 } }, B_NONE },
    { "IDirect3DPixelShader9", k_pixelshader, TM_N(k_pixelshader),
      { 0x6D3BDBDC, 0x5B02, 0x4415, { 0xB8, 0x52, 0xCE, 0x5E, 0x8B, 0xCC, 0xB2, 0x89 } }, B_NONE },
    { "IDirect3DStateBlock9", k_stateblock, TM_N(k_stateblock),
      { 0xB07C4FE5, 0x310D, 0x4BA8, { 0xA2, 0x3C, 0x4F, 0x0F, 0x20, 0x6F, 0x21, 0x8B } }, B_NONE },
    { "IDirect3DQuery9", k_query, TM_N(k_query),
      { 0xD9771460, 0xA695, 0x4F26, { 0xBB, 0xD3, 0x27, 0xB8, 0x40, 0xB5, 0x41, 0xCC } }, B_NONE },
};
#undef TM_N
constexpr Guid k_iid_unknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
constexpr Guid k_iid_resource = { 0x05EEC05D, 0x8F7D, 0x4362, { 0xB9, 0x99, 0xD1, 0xBA, 0xF3, 0x57, 0xC7, 0x04 } };
constexpr Guid k_iid_basetex = { 0x580CA87E, 0x1D3C, 0x4D54, { 0x99, 0x1D, 0xB7, 0xD3, 0xE3, 0xC2, 0x98, 0xCE } };

}  // namespace tmd3d
#endif
