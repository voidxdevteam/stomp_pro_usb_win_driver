// SPDX-FileCopyrightText: 2026 Sonulab
// SPDX-License-Identifier: GPL-3.0-only

#include "SonulabAsio.hpp"

#include "SonulabIds.hpp"

#include <atomic>
#include <new>
#include <string>

namespace {
HMODULE gModule = nullptr;
std::atomic<long> gObjects{0};
std::atomic<long> gLocks{0};

class SonulabClassFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(
        IUnknown* outer,
        REFIID riid,
        void** object) override {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        if (!object) {
            return E_POINTER;
        }
        auto* driver = new (std::nothrow) SonulabAsio();
        if (!driver) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = driver->QueryInterface(riid, object);
        driver->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) {
            gLocks.fetch_add(1, std::memory_order_relaxed);
        } else {
            gLocks.fetch_sub(1, std::memory_order_relaxed);
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
};

HRESULT registryResult(LSTATUS status) {
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(status);
}

LSTATUS setString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(
        key,
        name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

HRESULT registerDriver() {
    wchar_t modulePath[MAX_PATH]{};
    if (!GetModuleFileNameW(gModule, modulePath, MAX_PATH)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    wchar_t clsidText[64]{};
    if (!StringFromGUID2(CLSID_SonulabASIO, clsidText, static_cast<int>(std::size(clsidText)))) {
        return E_FAIL;
    }

    const std::wstring clsidPath = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsidText;
    HKEY clsidKey = nullptr;
    LSTATUS status = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, clsidPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &clsidKey, nullptr);
    if (status != ERROR_SUCCESS) {
        return registryResult(status);
    }
    status = setString(clsidKey, nullptr, kSonulabDriverDescription);
    HKEY serverKey = nullptr;
    if (status == ERROR_SUCCESS) {
        status = RegCreateKeyExW(
            clsidKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &serverKey, nullptr);
    }
    if (status == ERROR_SUCCESS) {
        status = setString(serverKey, nullptr, modulePath);
    }
    if (status == ERROR_SUCCESS) {
        status = setString(serverKey, L"ThreadingModel", L"Both");
    }
    if (serverKey) {
        RegCloseKey(serverKey);
    }
    RegCloseKey(clsidKey);
    if (status != ERROR_SUCCESS) {
        return registryResult(status);
    }

    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Sonulab ASIO");
    const std::wstring asioPath = std::wstring(L"SOFTWARE\\ASIO\\") + kSonulabDriverRegistryName;
    HKEY asioKey = nullptr;
    status = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE, asioPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &asioKey, nullptr);
    if (status == ERROR_SUCCESS) {
        status = setString(asioKey, L"CLSID", clsidText);
    }
    if (status == ERROR_SUCCESS) {
        status = setString(asioKey, L"Description", kSonulabDriverDescription);
    }
    if (asioKey) {
        RegCloseKey(asioKey);
    }
    return registryResult(status);
}

HRESULT unregisterDriver() {
    wchar_t clsidText[64]{};
    if (!StringFromGUID2(CLSID_SonulabASIO, clsidText, static_cast<int>(std::size(clsidText)))) {
        return E_FAIL;
    }
    const std::wstring clsidPath = std::wstring(L"SOFTWARE\\Classes\\CLSID\\") + clsidText;
    const std::wstring asioPath = std::wstring(L"SOFTWARE\\ASIO\\") + kSonulabDriverRegistryName;
    const LSTATUS clsidStatus = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidPath.c_str());
    const LSTATUS asioStatus = RegDeleteTreeW(HKEY_LOCAL_MACHINE, asioPath.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\Sonulab ASIO");
    if (clsidStatus != ERROR_SUCCESS && clsidStatus != ERROR_FILE_NOT_FOUND) {
        return registryResult(clsidStatus);
    }
    if (asioStatus != ERROR_SUCCESS && asioStatus != ERROR_FILE_NOT_FOUND) {
        return registryResult(asioStatus);
    }
    return S_OK;
}
}

void sonulabObjectCreated() noexcept {
    gObjects.fetch_add(1, std::memory_order_relaxed);
}

void sonulabObjectDestroyed() noexcept {
    gObjects.fetch_sub(1, std::memory_order_relaxed);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** object) {
    if (!IsEqualCLSID(clsid, CLSID_SonulabASIO)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    if (!object) {
        return E_POINTER;
    }
    auto* factory = new (std::nothrow) SonulabClassFactory();
    if (!factory) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return gObjects.load(std::memory_order_relaxed) == 0 &&
            gLocks.load(std::memory_order_relaxed) == 0
        ? S_OK
        : S_FALSE;
}

extern "C" HRESULT __stdcall DllRegisterServer() {
    return registerDriver();
}

extern "C" HRESULT __stdcall DllUnregisterServer() {
    return unregisterDriver();
}
