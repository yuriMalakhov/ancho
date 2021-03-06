/****************************************************************************
 * AnchoRuntime.cpp : Implementation of CAnchoRuntime
 * Copyright 2012 Salsita software (http://www.salsitasoft.com).
 * Author: Arne Seib <kontakt@seiberspace.de>
 ****************************************************************************/

#include "stdafx.h"
#include <map>

#include "AnchoRuntime.h"
#include "AnchoAddon.h"
#include "AnchoBrowserEvents.h"
#include "AnchoPassthruAPP.h"
#include "dllmain.h"

#include <string>

#include <Iepmapi.h>
#pragma comment(lib, "Iepmapi.lib")

/*============================================================================
 * class CAnchoRuntime
 */

//----------------------------------------------------------------------------
//  InitAddons
HRESULT CAnchoRuntime::InitAddons()
{
  // create all addons
  // open the registry key where all extensions are registered,
  // iterate subkeys and load each extension

  CRegKey regKey;
  LONG res = regKey.Open(HKEY_CURRENT_USER, s_AnchoExtensionsRegistryKey, KEY_READ);
  if (ERROR_SUCCESS != res)
  {
    return HRESULT_FROM_WIN32(res);
  }
  DWORD iIndex = 0;
  CString sKeyName;
  DWORD dwLen = 4096;
  HRESULT hr = S_OK;

  while(ERROR_SUCCESS == regKey.EnumKey(iIndex++, sKeyName.GetBuffer(dwLen), &dwLen, NULL))
  {
    sKeyName.ReleaseBuffer();
    CAnchoAddonComObject * pNewObject = NULL;
    hr = CAnchoAddonComObject::CreateInstance(&pNewObject);
    if (SUCCEEDED(hr))
    {
      CComPtr<IAnchoAddon> addon(pNewObject);
      hr = addon->Init(sKeyName, m_pAnchoService, m_pWebBrowser);
      if (SUCCEEDED(hr))
      {
        m_Addons[std::wstring(sKeyName)] = addon;
      }
    }
    dwLen = 4096;
  }
  return S_OK;
}

//----------------------------------------------------------------------------
//  DestroyAddons
void CAnchoRuntime::DestroyAddons()
{
  AddonMap::iterator it = m_Addons.begin();
  while(it != m_Addons.end()) {
    it->second->Shutdown();
    ++it;
  }
  m_Addons.clear();

  if(m_pAnchoService) {
    m_pAnchoService->unregisterRuntime(m_TabID);
  }
  m_pAnchoService.Release();
  if (m_pWebBrowser)
  {
    m_pWebBrowser.Release();
  }
  ATLTRACE(L"ANCHO: all addons destroyed for runtime %d\n", m_TabID);
}

//----------------------------------------------------------------------------
//  Cleanup
HRESULT CAnchoRuntime::Cleanup()
{
  AtlUnadvise(m_pWebBrowser, DIID_DWebBrowserEvents2, m_WebBrowserEventsCookie);
  AtlUnadvise(m_pBrowserEventSource, IID_DAnchoBrowserEvents, m_AnchoBrowserEventsCookie);

  return S_OK;
}

//----------------------------------------------------------------------------
//  Init
HRESULT CAnchoRuntime::Init()
{
  ATLASSERT(m_spUnkSite);
  // get IServiceProvider to get IWebBrowser2 and IOleWindow
  CComQIPtr<IServiceProvider> pServiceProvider = m_spUnkSite;
  if (!pServiceProvider)
  {
    return E_FAIL;
  }

  // get IWebBrowser2
  pServiceProvider->QueryService(SID_SWebBrowserApp, IID_IWebBrowser2, (LPVOID*)&m_pWebBrowser.p);
  if (!m_pWebBrowser) {
    return E_FAIL;
  }

  AtlAdvise(m_pWebBrowser, (IUnknown *)(TWebBrowserEvents *) this, DIID_DWebBrowserEvents2, &m_WebBrowserEventsCookie);

  ATLTRACE(L"ANCHO: runtime initialization - CoCreateInstace(CLSID_AnchoAddonService)\n");
  // create addon service object
  IF_FAILED_RET(m_pAnchoService.CoCreateInstance(CLSID_AnchoAddonService));

  // Registering tab in service - obtains tab id and assigns it to the tab as property
  IF_FAILED_RET(m_pAnchoService->registerRuntime((INT)getFrameTabWindow(), this, m_HeartBeatSlave.id(), &m_TabID));
  HWND hwnd;
  m_pWebBrowser->get_HWND((long*)&hwnd);
  ::SetProp(hwnd, s_AnchoTabIDPropertyName, (HANDLE)m_TabID);

  CComObject<CAnchoBrowserEvents>* pBrowserEventSource;
  IF_FAILED_RET(CComObject<CAnchoBrowserEvents>::CreateInstance(&pBrowserEventSource));

  m_pBrowserEventSource = pBrowserEventSource;

  AtlAdvise(m_pBrowserEventSource, (IUnknown*)(TAnchoBrowserEvents*) this, IID_DAnchoBrowserEvents,
    &m_AnchoBrowserEventsCookie);

  // Set the sink as property of the browser so it can be retrieved if someone wants to send
  // us events.
  IF_FAILED_RET(m_pWebBrowser->PutProperty(L"_anchoBrowserEvents", CComVariant(m_pBrowserEventSource)));
  ATLTRACE(L"ANCHO: runtime %d initialized\n", m_TabID);
  return S_OK;
}

//----------------------------------------------------------------------------
//
STDMETHODIMP_(void) CAnchoRuntime::OnBrowserDownloadBegin()
{
  m_ExtensionPageAPIPrepared = false;
}

//----------------------------------------------------------------------------
//
STDMETHODIMP_(void) CAnchoRuntime::OnBrowserProgressChange(LONG Progress, LONG ProgressMax)
{
  if (m_IsExtensionPage && !m_ExtensionPageAPIPrepared && m_pWebBrowser) {
    READYSTATE readyState;
    m_pWebBrowser->get_ReadyState(&readyState);
    if (readyState == READYSTATE_INTERACTIVE) {
      CComBSTR url;
      m_pWebBrowser->get_LocationURL(&url);
      if (S_OK == InitializeExtensionScripting(url)) {
        m_ExtensionPageAPIPrepared = true;
      }
    }
  }
}

//----------------------------------------------------------------------------
//  OnNavigateComplete
STDMETHODIMP_(void) CAnchoRuntime::OnNavigateComplete(LPDISPATCH pDispatch, VARIANT *URL)
{
  CComBSTR url(URL->bstrVal);
  m_IsExtensionPage = isExtensionPage(std::wstring(url));
  /*if (m_IsExtensionPage) {
    // Too early for api injections
    if (S_OK == InitializeExtensionScripting(url)) {
      m_ExtensionPageAPIPrepared = true;
    }
  }*/
}

//----------------------------------------------------------------------------
//  OnBrowserBeforeNavigate2
STDMETHODIMP_(void) CAnchoRuntime::OnBrowserBeforeNavigate2(LPDISPATCH pDisp, VARIANT *pURL, VARIANT *Flags,
  VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers, BOOL *Cancel)
{
  static bool bFirstRun = true;

  // Add the frame to the frames map so we can retrieve the IWebBrowser2 object using the URL.
  ATLASSERT(pURL->vt == VT_BSTR && pURL->bstrVal != NULL);
  CComQIPtr<IWebBrowser2> pWebBrowser(pDisp);
  ATLASSERT(pWebBrowser != NULL);

  // Workaround to ensure that first request goes through PAPP
  if (bFirstRun) {
    bFirstRun = false;
    *Cancel = TRUE;
    pWebBrowser->Stop();
    pWebBrowser->Navigate2(pURL, Flags, TargetFrameName, PostData, Headers);
    return;
  }

  VARIANT_BOOL isTop;
  if (SUCCEEDED(pWebBrowser->get_TopLevelContainer(&isTop))) {
    if (isTop) {
      // Loading the main frame so reset the frame list.
      m_Frames.clear();
    }
  }
  CComBSTR bstrUrl;
  removeUrlFragment(pURL->bstrVal, &bstrUrl);
  m_Frames[(BSTR) bstrUrl] = FrameRecord(pWebBrowser, isTop != VARIANT_FALSE);

  // Check if this is a new tab we are creating programmatically.
  // If so redirect it to the correct URL.
  std::wstring url(pURL->bstrVal, SysStringLen(pURL->bstrVal));

  //TODO - use headers instead of url
  size_t first = url.find_first_of(L'#');
  size_t last = url.find_last_of(L'#');
  if (first == std::wstring::npos || first == last) {
    return;
  }

  std::wstring requestIDStr = url.substr(first+1, last - first - 1);
  int requestID = _wtoi(requestIDStr.c_str());
  url.erase(0, last+1);

  CComVariant urlVar(url.c_str());
  CComVariant vtEmpty;

  *Cancel = TRUE;
  pWebBrowser->Stop();
  pWebBrowser->Navigate2(&urlVar, &vtEmpty, &vtEmpty, &vtEmpty, &vtEmpty);

  m_pAnchoService->createTabNotification(m_TabID, requestID);
}

//----------------------------------------------------------------------------
//  OnFrameStart
STDMETHODIMP CAnchoRuntime::OnFrameStart(BSTR bstrUrl, VARIANT_BOOL bIsMainFrame)
{
  //For extension pages we don't execute content scripts
  if (isExtensionPage(std::wstring(bstrUrl))) {
    return S_OK;
  }
  return InitializeContentScripting(bstrUrl, bIsMainFrame, documentLoadStart);
}

//----------------------------------------------------------------------------
//  OnFrameEnd
STDMETHODIMP CAnchoRuntime::OnFrameEnd(BSTR bstrUrl, VARIANT_BOOL bIsMainFrame)
{
  //For extension pages we don't execute content scripts
  if (isExtensionPage(std::wstring(bstrUrl))) {
    return S_OK;
  }
  return InitializeContentScripting(bstrUrl, bIsMainFrame, documentLoadEnd);
}

//----------------------------------------------------------------------------
//  OnFrameRedirect
STDMETHODIMP CAnchoRuntime::OnFrameRedirect(BSTR bstrOldUrl, BSTR bstrNewUrl)
{
  CComBSTR url;
  removeUrlFragment(bstrOldUrl, &url);
  FrameMap::iterator it = m_Frames.find(bstrOldUrl);
  if (it != m_Frames.end()) {
    removeUrlFragment(bstrNewUrl, &url);
    m_Frames[(BSTR) url] = it->second;
    m_Frames.erase(it);
  }
  return S_OK;
}
//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::OnBeforeRequest(VARIANT aReporter)
{
  ATLASSERT(aReporter.vt == VT_UNKNOWN);
  CComBSTR str;
  CComQIPtr<IWebRequestReporter> reporter(aReporter.punkVal);
  if (!reporter) {
    return E_INVALIDARG;
  }
  BeforeRequestInfo outInfo;
  CComBSTR url;
  CComBSTR method;
  reporter->getUrl(&url);
  reporter->getHTTPMethod(&method);

  std::wstring type = L"other";
  FrameMap::const_iterator it = m_Frames.find(url.m_str);
  if (it != m_Frames.end()) {
    type = it->second.isTopLevel ? L"main_frame" : L"sub_frame";
  }

  fireOnBeforeRequest(url.m_str, method.m_str, type, outInfo);
  if (outInfo.cancel) {
    reporter->cancelRequest();
  }
  if (outInfo.redirect) {
    reporter->redirectRequest(CComBSTR(outInfo.newUrl.c_str()));
  }
  return S_OK;
}
//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::OnBeforeSendHeaders(VARIANT aReporter)
{
  ATLASSERT(aReporter.vt == VT_UNKNOWN);
  CComBSTR str;
  CComQIPtr<IWebRequestReporter> reporter(aReporter.punkVal);
  if (!reporter) {
    return E_INVALIDARG;
  }
  BeforeSendHeadersInfo outInfo;
  CComBSTR url;
  CComBSTR method;
  reporter->getUrl(&url);
  reporter->getHTTPMethod(&method);

  std::wstring type = L"other";
  FrameMap::const_iterator it = m_Frames.find(url.m_str);
  if (it != m_Frames.end()) {
    type = it->second.isTopLevel ? L"main_frame" : L"sub_frame";
  }

  fireOnBeforeSendHeaders(url.m_str, method.m_str, type, outInfo);
  if (outInfo.modifyHeaders) {
    reporter->setNewHeaders(CComBSTR(outInfo.headers.c_str()).Detach());
  }
  return S_OK;
}
//----------------------------------------------------------------------------
//
HRESULT CAnchoRuntime::fireOnBeforeRequest(const std::wstring &aUrl, const std::wstring &aMethod, const std::wstring &aType, /*out*/ BeforeRequestInfo &aOutInfo)
{
  CComPtr<ComSimpleJSObject> info;
  IF_FAILED_RET(SimpleJSObject::createInstance(info));
  info->setProperty(L"url", CComVariant(aUrl.c_str()));
  info->setProperty(L"method", CComVariant(aMethod.c_str()));
  info->setProperty(L"type", CComVariant(aType.c_str()));
  info->setProperty(L"tabId", CComVariant(m_TabID));

  CComPtr<ComSimpleJSArray> argArray;
  IF_FAILED_RET(SimpleJSArray::createInstance(argArray));
  argArray->push_back(CComVariant(info.p));

  CComVariant result;
  m_pAnchoService->invokeEventObjectInAllExtensions(CComBSTR(L"webRequest.onBeforeRequest"), argArray.p, &result);
  if (result.vt & VT_ARRAY) {
    CComSafeArray<VARIANT> arr;
    arr.Attach(result.parray);
    //contained data already managed by CComSafeArray
    VARIANT tmp = {0}; HRESULT hr = result.Detach(&tmp);
    BEGIN_TRY_BLOCK
      aOutInfo.cancel = false;
      for (size_t i = 0; i < arr.GetCount(); ++i) {
        JSValue item(arr.GetAt(i));

        JSValue cancel = item[L"cancel"];
        if (!cancel.isNull()) {
          aOutInfo.cancel = aOutInfo.cancel || cancel.toBool();
        }

        JSValue redirectUrl = item[L"redirectUrl"];
        if (!redirectUrl.isNull()) {
          aOutInfo.redirect = true;
          aOutInfo.newUrl = redirectUrl.toString();
        }
      }
    END_TRY_BLOCK_CATCH_TO_HRESULT

  }
  return S_OK;
}
//----------------------------------------------------------------------------
//
HRESULT CAnchoRuntime::fireOnBeforeSendHeaders(const std::wstring &aUrl, const std::wstring &aMethod, const std::wstring &aType, /*out*/ BeforeSendHeadersInfo &aOutInfo)
{
  aOutInfo.modifyHeaders = false;
  CComPtr<ComSimpleJSObject> info;
  IF_FAILED_RET(SimpleJSObject::createInstance(info));
  info->setProperty(L"url", CComVariant(aUrl.c_str()));
  info->setProperty(L"method", CComVariant(aMethod.c_str()));
  info->setProperty(L"type", CComVariant(aType.c_str()));
  info->setProperty(L"tabId", CComVariant(m_TabID));
  info->setProperty(L"requestHeaders", CComVariant());

  CComPtr<ComSimpleJSArray> argArray;
  IF_FAILED_RET(SimpleJSArray::createInstance(argArray));
  argArray->push_back(CComVariant(info.p));

  CComVariant result;
  m_pAnchoService->invokeEventObjectInAllExtensions(CComBSTR(L"webRequest.onBeforeSendHeaders"), argArray.p, &result);
  if (result.vt & VT_ARRAY) {
    CComSafeArray<VARIANT> arr;
    arr.Attach(result.parray);
    //contained data already managed by CComSafeArray
    VARIANT tmp = {0}; HRESULT hr = result.Detach(&tmp);
    BEGIN_TRY_BLOCK
      for (size_t i = 0; i < arr.GetCount(); ++i) {
        JSValue item(arr.GetAt(i));
        JSValue requestHeaders = item[L"requestHeaders"];
        if (!requestHeaders.isNull()) {
          std::wostringstream oss;
          int headerCount = requestHeaders[L"length"].toInt();
          for (int i = 0; i < headerCount; ++i) {
            JSValue headerRecord = requestHeaders[i];
            //TODO handle headerRecord[L"binaryValue"]
            std::wstring headerText = headerRecord[L"name"].toString() + std::wstring(L": ") + headerRecord[L"value"].toString();
            oss << headerText << L"\r\n";
          }
          aOutInfo.modifyHeaders = true;
          aOutInfo.headers = oss.str();
        }
      }
    END_TRY_BLOCK_CATCH_TO_HRESULT

  }

  return S_OK;
}
//----------------------------------------------------------------------------
//  InitializeContentScripting
HRESULT CAnchoRuntime::InitializeContentScripting(BSTR bstrUrl, VARIANT_BOOL isRefreshingMainFrame, documentLoadPhase aPhase)
{
  CComPtr<IWebBrowser2> webBrowser;
  if (isRefreshingMainFrame) {
    webBrowser = m_pWebBrowser;
  }
  else {
    CComBSTR url;
    removeUrlFragment(bstrUrl, &url);
    FrameMap::iterator it = m_Frames.find((BSTR) url);
    if (it == m_Frames.end()) {
      // Either this frame has already been removed, or the request isn't for a frame after all (e.g. an htc).
      return S_FALSE;
    }
    webBrowser = it->second.browser;
  }
  // Normally the frame map is cleared in the BeforeNavigate2 handler, but it isn't triggered when the
  // page is refreshed, so we need this workaround as well.
  if (isRefreshingMainFrame && (documentLoadStart == aPhase)) {
    m_Frames.clear();
  }
  AddonMap::iterator it = m_Addons.begin();
  while(it != m_Addons.end()) {
    it->second->InitializeContentScripting(webBrowser, bstrUrl, aPhase);
    ++it;
  }

  return S_OK;
}
//----------------------------------------------------------------------------
//
HRESULT CAnchoRuntime::InitializeExtensionScripting(BSTR bstrUrl)
{
  std::wstring domain = getDomainName(bstrUrl);
  AddonMap::iterator it = m_Addons.find(domain);
  if (it != m_Addons.end()) {
    return it->second->InitializeExtensionScripting(bstrUrl);
  }
  return S_FALSE;
}

//----------------------------------------------------------------------------
//  SetSite
STDMETHODIMP CAnchoRuntime::SetSite(IUnknown *pUnkSite)
{
  HRESULT hr = IObjectWithSiteImpl<CAnchoRuntime>::SetSite(pUnkSite);
  IF_FAILED_RET(hr);
  if (pUnkSite)
  {
    hr = Init();
    if (SUCCEEDED(hr)) {
      hr = InitAddons();
    }
  }
  else
  {
    DestroyAddons();
    Cleanup();
  }
  return hr;
}

//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::reloadTab()
{
  CComVariant var(REFRESH_COMPLETELY);
  m_pWebBrowser->Refresh2(&var);
  return S_OK;
}

//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::closeTab()
{
  return m_pWebBrowser->Quit();
}

//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::executeScript(BSTR aExtensionId, BSTR aCode, INT aFileSpecified)
{
  //TODO: check permissions from manifest
  return S_OK;
}
//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::showBrowserActionBar(INT aShow)
{
  wchar_t clsid[1024] = {0};
  IF_FAILED_RET(::StringFromGUID2( CLSID_IEToolbar, (OLECHAR*)clsid, sizeof(clsid)));
  CComVariant clsidVar(clsid);
  CComVariant show(aShow != FALSE);
  IF_FAILED_RET(m_pWebBrowser->ShowBrowserBar(&clsidVar, &show, NULL));
  return S_OK;
}
//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::updateTab(LPDISPATCH aProperties)
{
  CIDispatchHelper properties(aProperties);
  CComBSTR url;
  HRESULT hr = properties.Get<CComBSTR, VT_BSTR, BSTR>(L"url", url);
  if (hr == S_OK) {
    CComVariant vtUrl(url);
    CComVariant vtEmpty;
    m_pWebBrowser->Navigate2(&vtUrl, &vtEmpty, &vtEmpty, &vtEmpty, &vtEmpty);
  }
  INT active = 0;
  hr = properties.Get<INT, VT_BOOL, INT>(L"active", active);
  if (hr == S_OK) {
    HWND hwnd = getTabWindow();
    IAccessible *acc = NULL;
    //TODO - fix tab activation
    if (S_OK == AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, IID_IAccessible, (void**)&acc)) {
      CComVariant var(CHILDID_SELF, VT_I4);
      acc->accDoDefaultAction(var);
    }
  }
  return S_OK;
}

//----------------------------------------------------------------------------
//
STDMETHODIMP CAnchoRuntime::fillTabInfo(VARIANT* aInfo)
{
  ENSURE_RETVAL(aInfo);
  if(aInfo->vt != VT_DISPATCH) {
    return E_NOINTERFACE;
  }
  CIDispatchHelper obj(aInfo->pdispVal);

  CComBSTR locationUrl;
  CComBSTR name;
  m_pWebBrowser->get_LocationURL(&locationUrl);
  obj.SetProperty(L"url", CComVariant(locationUrl));

  m_pWebBrowser->get_Name(&name);
  obj.SetProperty(L"title", CComVariant(name));

  obj.SetProperty(L"id", CComVariant(m_TabID));

  obj.SetProperty(L"active", CComVariant(isTabActive()));

  obj.SetProperty(L"windowId", reinterpret_cast<INT>(getMainWindow()));
  return S_OK;
}

//----------------------------------------------------------------------------
//
HWND CAnchoRuntime::getTabWindow()
{
  HWND hwndBrowser = NULL;
  IServiceProvider* pServiceProvider = NULL;
  if (SUCCEEDED(m_pWebBrowser->QueryInterface(IID_IServiceProvider, (void**)&pServiceProvider))){
    IOleWindow* pWindow = NULL;
    if (SUCCEEDED(pServiceProvider->QueryService(SID_SShellBrowser, IID_IOleWindow,(void**)&pWindow))) {
      // hwndBrowser is the handle of TabWindowClass
      if (!SUCCEEDED(pWindow->GetWindow(&hwndBrowser))) {
        hwndBrowser = NULL;
      }
      pWindow->Release();
    }
    pServiceProvider->Release();
  }
  return hwndBrowser;
}

//----------------------------------------------------------------------------
//
HWND CAnchoRuntime::findParentWindowByClass(std::wstring aClassName)
{
  HWND window = getTabWindow();
  wchar_t className[256];
  while (window) {
    if (!GetClassName(window, className, 256)) {
      return NULL;
    }
    if (aClassName == className) {
      return window;
    }
    window = GetParent(window);
  }
  return NULL;
}
//----------------------------------------------------------------------------
//
bool CAnchoRuntime::isTabActive()
{
  HWND hwndBrowser = getTabWindow();
  return hwndBrowser && ::IsWindowVisible(hwndBrowser);
}
