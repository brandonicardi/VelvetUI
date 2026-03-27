// ============================================================
// VelvetUI - XAML Diagnostics COM Interface Definitions
//
// Extracted from xamlOM.h (MIDL-generated).
// Reference: Windows SDK xamlOM.h / m417z gist / ExplorerTAP
//
// Note: MIDL_INTERFACE("uuid") already registers the UUID via
// __declspec(uuid("...")) on MSVC. No __CRT_UUID_DECL needed
// (that macro is MinGW/Clang-only).
// ============================================================

#pragma once

#include <rpc.h>
#include <rpcndr.h>
#include <windows.h>
#include <ole2.h>
#include <oaidl.h>
#include <ocidl.h>
#include <inspectable.h>
#include <dxgi1_2.h>

// Prevent GetCurrentTime macro collision with WinRT headers
#undef GetCurrentTime

// ============================================================
// Win32 helpers
// ============================================================
#ifndef E_NOTFOUND
#define E_NOTFOUND    HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
#endif
#ifndef E_UNKNOWNTYPE
#define E_UNKNOWNTYPE MAKE_HRESULT(SEVERITY_ERROR, FACILITY_XAML, 40L)
#endif

// ============================================================
// C-linkage: function prototypes and POD types
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

_Check_return_ HRESULT InitializeXamlDiagnostic(
    _In_ LPCWSTR endPointName, _In_ DWORD pid,
    _In_ LPCWSTR wszDllXamlDiagnostics,
    _In_ LPCWSTR wszTAPDllName, _In_ CLSID tapClsid);

_Check_return_ HRESULT InitializeXamlDiagnosticsEx(
    _In_ LPCWSTR endPointName, _In_ DWORD pid,
    _In_ LPCWSTR wszDllXamlDiagnostics,
    _In_ LPCWSTR wszTAPDllName, _In_ CLSID tapClsid,
    _In_ LPCWSTR wszInitializationData);

typedef MIDL_uhyper InstanceHandle;

typedef enum VisualMutationType {
    Add    = 0,
    Remove = 1
} VisualMutationType;

typedef enum BaseValueSource {
    BaseValueSourceUnknown      = 0,
    BaseValueSourceDefault      = 1,
    BaseValueSourceBuiltInStyle = 2,
    BaseValueSourceStyle        = 3,
    BaseValueSourceLocal        = 4,
    Inherited                   = 5,
    DefaultStyleTrigger         = 6,
    TemplateTrigger             = 7,
    StyleTrigger                = 8,
    ImplicitStyleReference      = 9,
    ParentTemplate              = 10,
    ParentTemplateTrigger       = 11,
    Animation                   = 12,
    Coercion                    = 13,
    BaseValueSourceVisualState  = 14
} BaseValueSource;

typedef enum MetadataBit {
    None                            = 0,
    IsValueHandle                   = 0x1,
    IsPropertyReadOnly              = 0x2,
    IsValueCollection               = 0x4,
    IsValueCollectionReadOnly       = 0x8,
    IsValueBindingExpression        = 0x10,
    IsValueNull                     = 0x20,
    IsValueHandleAndEvaluatedValue  = 0x40
} MetadataBit;

typedef enum RenderTargetBitmapOptions {
    RenderTarget            = 0,
    RenderTargetAndChildren = 1
} RenderTargetBitmapOptions;

typedef enum ResourceType {
    ResourceTypeStatic = 0,
    ResourceTypeTheme  = 1
} ResourceType;

typedef enum VisualElementState {
    ErrorResolved         = 0,
    ErrorResourceNotFound = 1,
    ErrorInvalidResource  = 2
} VisualElementState;

typedef struct SourceInfo {
    BSTR         FileName;
    unsigned int LineNumber;
    unsigned int ColumnNumber;
    unsigned int CharPosition;
    BSTR         Hash;
} SourceInfo;

typedef struct ParentChildRelation {
    InstanceHandle Parent;
    InstanceHandle Child;
    unsigned int   ChildIndex;
} ParentChildRelation;

typedef struct VisualElement {
    InstanceHandle Handle;
    SourceInfo     SrcInfo;
    BSTR           Type;
    BSTR           Name;
    unsigned int   NumChildren;
} VisualElement;

typedef struct PropertyChainSource {
    InstanceHandle  Handle;
    BSTR            TargetType;
    BSTR            Name;
    BaseValueSource Source;
    SourceInfo      SrcInfo;
} PropertyChainSource;

typedef struct PropertyChainValue {
    unsigned int Index;
    BSTR         Type;
    BSTR         DeclaringType;
    BSTR         ValueType;
    BSTR         ItemType;
    BSTR         Value;
    BOOL         Overridden;
    hyper        MetadataBits;
    BSTR         PropertyName;
    unsigned int PropertyChainIndex;
} PropertyChainValue;

typedef struct EnumType {
    BSTR       Name;
    SAFEARRAY* ValueInts;
    SAFEARRAY* ValueStrings;
} EnumType;

typedef struct CollectionElementValue {
    unsigned int Index;
    BSTR         ValueType;
    BSTR         Value;
    hyper        MetadataBits;
} CollectionElementValue;

typedef struct BitmapDescription {
    unsigned int    Width;
    unsigned int    Height;
    DXGI_FORMAT     Format;
    DXGI_ALPHA_MODE AlphaMode;
} BitmapDescription;

#ifdef __cplusplus
} // extern "C"
#endif

// ============================================================
// C++ scope: COM interfaces
// MIDL_INTERFACE("uuid") expands to:
//   struct __declspec(uuid("uuid")) __declspec(novtable) Name
// This registers the UUID so __uuidof(Name) works on MSVC.
// ============================================================
#ifdef __cplusplus

// ---- IVisualTreeServiceCallback ----

MIDL_INTERFACE("AA7A8931-80E4-4FEC-8F3B-553F87B4966E")
IVisualTreeServiceCallback : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation,
        VisualElement element,
        VisualMutationType mutationType) = 0;
};

// ---- IVisualTreeServiceCallback2 ----

MIDL_INTERFACE("BAD9EB88-AE77-4397-B948-5FA2DB0A19EA")
IVisualTreeServiceCallback2 : public IVisualTreeServiceCallback
{
public:
    virtual HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle element,
        VisualElementState elementState,
        LPCWSTR context) = 0;
};

// ---- IVisualTreeService ----

MIDL_INTERFACE("A593B11A-D17F-48BB-8F66-83910731C8A5")
IVisualTreeService : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE AdviseVisualTreeChange(
        IVisualTreeServiceCallback* pCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnadviseVisualTreeChange(
        IVisualTreeServiceCallback* pCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetEnums(
        unsigned int* pCount, EnumType** ppEnums) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(
        BSTR typeName, BSTR value, InstanceHandle* pInstanceHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValuesChain(
        InstanceHandle instanceHandle,
        unsigned int* pSourceCount, PropertyChainSource** ppPropertySources,
        unsigned int* pPropertyCount, PropertyChainValue** ppPropertyValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProperty(
        InstanceHandle instanceHandle, InstanceHandle value, unsigned int propertyIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearProperty(
        InstanceHandle instanceHandle, unsigned int propertyIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCollectionCount(
        InstanceHandle instanceHandle, unsigned int* pCollectionSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCollectionElements(
        InstanceHandle instanceHandle, unsigned int startIndex,
        unsigned int* pElementCount, CollectionElementValue** ppElementValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddChild(
        InstanceHandle parent, InstanceHandle child, unsigned int index) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveChild(
        InstanceHandle parent, unsigned int index) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearChildren(
        InstanceHandle parent) = 0;
};

// ---- IXamlDiagnostics ----

MIDL_INTERFACE("18C9E2B6-3F43-4116-9F2B-FF935D7770D2")
IXamlDiagnostics : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetDispatcher(IInspectable** ppDispatcher) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUiLayer(IInspectable** ppLayer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetApplication(IInspectable** ppApplication) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIInspectableFromHandle(
        InstanceHandle instanceHandle, IInspectable** ppInstance) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHandleFromIInspectable(
        IInspectable* pInstance, InstanceHandle* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE HitTest(
        RECT rect, unsigned int* pCount, InstanceHandle** ppInstanceHandles) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterInstance(
        IInspectable* pInstance, InstanceHandle* pInstanceHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInitializationData(BSTR* pInitializationData) = 0;
};

// ---- IBitmapData ----

MIDL_INTERFACE("d1a34ef2-cad8-4635-a3d2-fcda8d3f3caf")
IBitmapData : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CopyBytesTo(
        unsigned int sourceOffsetInBytes, unsigned int maxBytesToCopy,
        byte* pvBytes, unsigned int* numberOfBytesCopied) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStride(unsigned int* pStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBitmapDescription(BitmapDescription* pBitmapDescription) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSourceBitmapDescription(BitmapDescription* pBitmapDescription) = 0;
};

// ---- IVisualTreeService2 ----

MIDL_INTERFACE("130F5136-EC43-4F61-89C7-9801A36D2E95")
IVisualTreeService2 : public IVisualTreeService
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetPropertyIndex(
        InstanceHandle object, LPCWSTR propertyName, unsigned int* pPropertyIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(
        InstanceHandle object, unsigned int propertyIndex, InstanceHandle* pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReplaceResource(
        InstanceHandle resourceDictionary, InstanceHandle key, InstanceHandle newValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderTargetBitmap(
        InstanceHandle handle, RenderTargetBitmapOptions options,
        unsigned int maxPixelWidth, unsigned int maxPixelHeight,
        IBitmapData** ppBitmapData) = 0;
};

// ---- IVisualTreeService3 ----

MIDL_INTERFACE("0E79C6E0-85A0-4BE8-B41A-655CF1FD19BD")
IVisualTreeService3 : public IVisualTreeService2
{
public:
    virtual HRESULT STDMETHODCALLTYPE ResolveResource(
        InstanceHandle resourceContext, LPCWSTR resourceName,
        ResourceType resourceType, unsigned int propertyIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDictionaryItem(
        InstanceHandle dictionaryHandle, LPCWSTR resourceName,
        BOOL resourceIsImplicitStyle, InstanceHandle* resourceHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddDictionaryItem(
        InstanceHandle dictionaryHandle, InstanceHandle resourceKey,
        InstanceHandle resourceHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDictionaryItem(
        InstanceHandle dictionaryHandle, InstanceHandle resourceKey) = 0;
};

#endif // __cplusplus
