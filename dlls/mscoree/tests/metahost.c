/*
 * Copyright 2010 Vincent Povirk
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "ole2.h"

#include "corerror.h"
#include "ocidl.h"
#include "initguid.h"
#include "metahost.h"
#include "wine/test.h"

static HMODULE hmscoree;

static HRESULT (WINAPI *pCLRCreateInstance)(REFCLSID clsid, REFIID riid, LPVOID *ppInterface);

static ICLRMetaHost *metahost;

static const WCHAR v4_0[] = {'v','4','.','0','.','3','0','3','1','9',0};

static DWORD expect_runtime_tid;

static BOOL init_pointers(void)
{
    HRESULT hr = E_FAIL;

    hmscoree = LoadLibraryA("mscoree.dll");

    if (hmscoree)
        pCLRCreateInstance = (void *)GetProcAddress(hmscoree, "CLRCreateInstance");

    if (pCLRCreateInstance)
        hr = pCLRCreateInstance(&CLSID_CLRMetaHost, &IID_ICLRMetaHost, (void**)&metahost);

    if (FAILED(hr))
    {
        win_skip(".NET 4 is not installed\n");
        FreeLibrary(hmscoree);
        return FALSE;
    }

    return TRUE;
}

static void cleanup(void)
{
    ICLRMetaHost_Release(metahost);

    FreeLibrary(hmscoree);
}

static WCHAR *strrchrW( WCHAR *str, WCHAR ch )
{
    WCHAR *ret = NULL;
    do { if (*str == ch) ret = str; } while (*str++);
    return ret;
}

static void test_getruntime(WCHAR *version)
{
    static const WCHAR dotzero[] = {'.','0',0};
    WCHAR *dot;
    HRESULT hr;
    ICLRRuntimeInfo *info;
    DWORD count;
    WCHAR buf[MAX_PATH];

    hr = ICLRMetaHost_GetRuntime(metahost, NULL, &IID_ICLRRuntimeInfo, (void**)&info);
    ok(hr == E_POINTER, "GetVersion failed, hr=%x\n", hr);

    hr = ICLRMetaHost_GetRuntime(metahost, version, &IID_ICLRRuntimeInfo, (void**)&info);
    ok(hr == S_OK, "GetVersion failed, hr=%x\n", hr);
    if (hr != S_OK) return;

    count = MAX_PATH;
    hr = ICLRRuntimeInfo_GetVersionString(info, buf, &count);
    ok(hr == S_OK, "GetVersionString returned %x\n", hr);
    ok(count == lstrlenW(buf)+1, "GetVersionString returned count %u but string of length %u\n", count, lstrlenW(buf)+1);
    ok(lstrcmpW(buf, version) == 0, "got unexpected version %s\n", wine_dbgstr_w(buf));

    ICLRRuntimeInfo_Release(info);

    /* Versions must match exactly. */
    dot = strrchrW(version, '.');
    lstrcpyW(dot, dotzero);
    hr = ICLRMetaHost_GetRuntime(metahost, version, &IID_ICLRRuntimeInfo, (void**)&info);
    ok(hr == CLR_E_SHIM_RUNTIME, "GetVersion failed, hr=%x\n", hr);
}

static void test_enumruntimes(void)
{
    IEnumUnknown *runtime_enum;
    IUnknown *unk;
    DWORD count;
    ICLRRuntimeInfo *runtime_info;
    HRESULT hr;
    WCHAR buf[MAX_PATH];

    hr = ICLRMetaHost_EnumerateInstalledRuntimes(metahost, &runtime_enum);
    ok(hr == S_OK, "EnumerateInstalledRuntimes returned %x\n", hr);
    if (FAILED(hr)) return;

    while ((hr = IEnumUnknown_Next(runtime_enum, 1, &unk, &count)) == S_OK)
    {
        hr = IUnknown_QueryInterface(unk, &IID_ICLRRuntimeInfo, (void**)&runtime_info);
        ok(hr == S_OK, "QueryInterface returned %x\n", hr);

        count = 1;
        hr = ICLRRuntimeInfo_GetVersionString(runtime_info, buf, &count);
        ok(hr == E_NOT_SUFFICIENT_BUFFER, "GetVersionString returned %x\n", hr);
        ok(count > 1, "GetVersionString returned count %u\n", count);

        count = 0xdeadbeef;
        hr = ICLRRuntimeInfo_GetVersionString(runtime_info, NULL, &count);
        ok(hr == S_OK, "GetVersionString returned %x\n", hr);
        ok(count > 1 && count != 0xdeadbeef, "GetVersionString returned count %u\n", count);

        count = MAX_PATH;
        hr = ICLRRuntimeInfo_GetVersionString(runtime_info, buf, &count);
        ok(hr == S_OK, "GetVersionString returned %x\n", hr);
        ok(count > 1, "GetVersionString returned count %u\n", count);

        trace("runtime found: %s\n", wine_dbgstr_w(buf));

        ICLRRuntimeInfo_Release(runtime_info);
        IUnknown_Release(unk);

        test_getruntime(buf);
    }

    ok(hr == S_FALSE, "IEnumUnknown_Next returned %x\n", hr);

    IEnumUnknown_Release(runtime_enum);
}

static void WINAPI notification_dummy_callback(ICLRRuntimeInfo *pRuntimeInfo, CallbackThreadSetFnPtr pfnCallbackThreadSet,
    CallbackThreadUnsetFnPtr pfnCallbackThreadUnset)
{
    ok(0, "unexpected call\n");
}

static void WINAPI notification_callback(ICLRRuntimeInfo *pRuntimeInfo, CallbackThreadSetFnPtr pfnCallbackThreadSet,
    CallbackThreadUnsetFnPtr pfnCallbackThreadUnset)
{
    HRESULT hr;
    WCHAR buf[20];
    DWORD buf_size = 20;

    ok(expect_runtime_tid != 0, "unexpected call\n");

    if (expect_runtime_tid != 0)
    {
        ok(GetCurrentThreadId() == expect_runtime_tid,
            "expected call on thread %04x, got thread %04x\n", expect_runtime_tid, GetCurrentThreadId());
        expect_runtime_tid = 0;
    }

    hr = ICLRRuntimeInfo_GetVersionString(pRuntimeInfo, buf, &buf_size);
    ok(hr == S_OK, "GetVersion returned %x\n", hr);
    ok(lstrcmpW(buf, v4_0) == 0, "GetVersion returned %s\n", wine_dbgstr_w(buf));

    hr = pfnCallbackThreadSet();
    ok(hr == S_OK, "pfnCallbackThreadSet returned %x\n", hr);

    hr = pfnCallbackThreadUnset();
    ok(hr == S_OK, "pfnCallbackThreadUnset returned %x\n", hr);
}

static void test_notification(void)
{
    HRESULT hr;

    hr = ICLRMetaHost_RequestRuntimeLoadedNotification(metahost, NULL);
    ok(hr == E_POINTER, "RequestRuntimeLoadedNotification returned %x\n", hr);

    hr = ICLRMetaHost_RequestRuntimeLoadedNotification(metahost,notification_callback);
    ok(hr == S_OK, "RequestRuntimeLoadedNotification failed, hr=%x\n", hr);

    hr = ICLRMetaHost_RequestRuntimeLoadedNotification(metahost,notification_dummy_callback);
    ok(hr == HOST_E_INVALIDOPERATION, "RequestRuntimeLoadedNotification returned %x\n", hr);
}

static void test_notification_cb(void)
{
    HRESULT hr;
    ICLRRuntimeInfo *info;
    ICLRRuntimeHost *host;

    hr = ICLRMetaHost_GetRuntime(metahost, v4_0, &IID_ICLRRuntimeInfo, (void**)&info);
    ok(hr == S_OK, "GetRuntime returned %x\n", hr);

    expect_runtime_tid = GetCurrentThreadId();
    hr = ICLRRuntimeInfo_GetInterface(info, &CLSID_CLRRuntimeHost, &IID_ICLRRuntimeHost, (void**)&host);
    ok(hr == S_OK, "GetInterface returned %x\n", hr);
    ok(expect_runtime_tid == 0, "notification_callback was not called\n");

    ICLRRuntimeHost_Release(host);

    ICLRRuntimeInfo_Release(info);
}

START_TEST(metahost)
{
    if (!init_pointers())
        return;

    test_notification();
    test_enumruntimes();
    test_notification_cb();

    cleanup();
}
