// ============================================================
// VelvetUI - SimpleFactory
//
// Generic IClassFactory implementation used by DllGetClassObject
// to create VelvetTAP instances on demand.
// ============================================================

#pragma once

#include <Unknwn.h>
#include <winrt/base.h>

template<class T>
struct SimpleFactory : winrt::implements<SimpleFactory<T>, IClassFactory, winrt::non_agile>
{
    HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown* pUnkOuter,
        REFIID riid,
        void** ppvObject) override try
    {
        if (pUnkOuter) {
            return CLASS_E_NOAGGREGATION;
        }

        *ppvObject = nullptr;
        return winrt::make<T>().as(riid, ppvObject);
    }
    catch (...)
    {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override
    {
        return S_OK;
    }
};
