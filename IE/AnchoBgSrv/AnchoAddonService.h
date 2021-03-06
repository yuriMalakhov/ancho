/****************************************************************************
 * AnchoAddonService.h : Declaration of CAnchoAddonService
 * Copyright 2012 Salsita software (http://www.salsitasoft.com).
 * Author: Arne Seib <kontakt@seiberspace.de>
 ****************************************************************************/

#pragma once
#include "resource.h"       // main symbols

#include "AnchoBgSrv_i.h"
#include "AnchoBackground.h"
#include "AnchoBgSrvModule.h"
#include "IECookieManager.h"
#include "CommandQueue.h"
#include "XmlHttpRequest.h"

#if defined(_WIN32_WCE) && !defined(_CE_DCOM) && !defined(_CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA)
#error "Single-threaded COM objects are not properly supported on Windows CE platform, such as the Windows Mobile platforms that do not include full DCOM support. Define _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA to force ATL to support creating single-thread COM object's and allow use of it's single-threaded COM object implementations. The threading model in your rgs file was set to 'Free' as that is the only threading model supported in non DCOM Windows CE platforms."
#endif

class CAnchoAddonService;
/*============================================================================
 * class CAnchoAddonServiceCallback
 */
struct CAnchoAddonServiceCallback
{
  virtual void OnAddonFinalRelease(BSTR bsID) = 0;
};

/*============================================================================
 * class ServiceTimer
 */
struct ServiceTimer
{
  typedef void (CAnchoAddonService::*ServiceMethod)();

  static void CALLBACK callback(void *aParameter, BOOLEAN aTimerOrWaitFired)
  {
    ServiceTimer *timer = reinterpret_cast<ServiceTimer*>(aParameter);
    if (timer) {
      ATLASSERT(timer->mServiceInstance);
      ATLASSERT(timer->mMethod);

      (timer->mServiceInstance->*(timer->mMethod))();
    }
  }

  ServiceTimer()
    : mTimerHandle(NULL), mTimerQueue(NULL), mPeriod(0), mMethod(NULL), mServiceInstance(NULL)
  {}

  ~ServiceTimer()
  {
    finalize();
  }

  void initialize(ServiceMethod aMethod, CAnchoAddonService *aInstance, unsigned long aPeriodMS, HANDLE aTimerQueue = NULL)
  {
    finalize();

    if (!aMethod || !aInstance) {
      ATLASSERT(false);
      //Ancho_THROW();
    }

    mMethod = aMethod;
    mServiceInstance = aInstance;

    mTimerQueue = aTimerQueue;
    mPeriod = aPeriodMS;

    if (!CreateTimerQueueTimer(
      &mTimerHandle,
      mTimerQueue,
      (WAITORTIMERCALLBACK) &ServiceTimer::callback,
      (void*) this,
      aPeriodMS,
      aPeriodMS,
      WT_EXECUTEDEFAULT
      ))
    {
      ATLASSERT(false);
      //ANCHO_THROW();
    }
  }

  bool isRunning() const
  {
    return mTimerHandle != NULL;
  }

  void finalize()
  {
    if (isRunning()) {
      DeleteTimerQueueTimer(mTimerQueue, mTimerHandle, NULL);
    }
  }

  HANDLE mTimerHandle;
  HANDLE mTimerQueue;
  unsigned long mPeriod;

  ServiceMethod mMethod;
  CAnchoAddonService *mServiceInstance;
};


/*============================================================================
 * class CAnchoAddonService
 */
class ATL_NO_VTABLE CAnchoAddonService :
  public CAnchoAddonServiceCallback,
  public CComObjectRootEx<CComSingleThreadModel>,
  public CComCoClass<CAnchoAddonService, &CLSID_AnchoAddonService>,
  public IAnchoAddonService,
  public IDispatchImpl<IAnchoServiceApi, &IID_IAnchoServiceApi, &LIBID_AnchoBgSrvLib, /*wMajor =*/ 0xffff, /*wMinor =*/ 0xffff>
{
public:
  // -------------------------------------------------------------------------
  // ctor
  CAnchoAddonService(): m_NextTabID(1), m_NextRequestID(1), mShouldCheckBHOConnections(false)
  {
  }

public:
  // -------------------------------------------------------------------------
  // COM standard stuff
  DECLARE_REGISTRY_RESOURCEID(IDR_SCRIPTSERVICE)
  DECLARE_CLASSFACTORY_SINGLETON(CAnchoAddonService)
  DECLARE_NOT_AGGREGATABLE(CAnchoAddonService)
  DECLARE_PROTECT_FINAL_CONSTRUCT()

public:
  // -------------------------------------------------------------------------
  // COM interface map
  BEGIN_COM_MAP(CAnchoAddonService)
    COM_INTERFACE_ENTRY(IAnchoAddonService)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY(IAnchoServiceApi)
  END_COM_MAP()

public:
  // -------------------------------------------------------------------------
  // COM standard methods
  HRESULT FinalConstruct();
  void FinalRelease();

public:
  // -------------------------------------------------------------------------
  // Methods
  inline LPCTSTR GetThisPath() {return m_sThisPath;}

  // CAnchoAddonServiceCallback implementation
  virtual void OnAddonFinalRelease(BSTR bsID);

  HRESULT navigateBrowser(LPUNKNOWN aWebBrowserWin, const std::wstring &url, INT32 aNavigateOptions);
  HRESULT getActiveWebBrowser(LPUNKNOWN* pUnkWebBrowser);
public:
  // -------------------------------------------------------------------------
  // IAnchoServiceApi methods. See .idl for description.
  STDMETHOD(get_cookieManager)(LPDISPATCH* ppRet);
  STDMETHOD(invokeExternalEventObject)(BSTR aExtensionId, BSTR aEventName, LPDISPATCH aArgs, VARIANT* aRet);
  STDMETHOD(createTab)(LPDISPATCH aProperties, LPDISPATCH aCreator, LPDISPATCH aCallback);
  STDMETHOD(reloadTab)(INT aTabId);
  STDMETHOD(updateTab)(INT aTabId, LPDISPATCH aProperties);
  STDMETHOD(getTabInfo)(INT aTabId, LPDISPATCH aCreator, VARIANT* aRet);
  STDMETHOD(removeTabs)(LPDISPATCH aTabs, LPDISPATCH aCallback);
  STDMETHOD(queryTabs)(LPDISPATCH aQueryInfo, LPDISPATCH aCreator, VARIANT* aRet);

  STDMETHOD(getWindow)(INT aWindowId, LPDISPATCH aCreator, BOOL aPopulate, VARIANT* aRet);
  STDMETHOD(getAllWindows)(LPDISPATCH aCreator, BOOL aPopulate, VARIANT* aRet);
  STDMETHOD(updateWindow)(INT aWindowId, LPDISPATCH aProperties);
  STDMETHOD(createWindow)(LPDISPATCH aProperties, LPDISPATCH aCreator, LPDISPATCH aCallback);
  STDMETHOD(closeWindow)(INT aWindowId);
  STDMETHOD(createPopupWindow)(BSTR aUrl, INT aX, INT aY, LPDISPATCH aInjectedData, LPDISPATCH aCloseCallback);
  STDMETHOD(getCurrentWindowId)(INT *aWinId);


  STDMETHOD(executeScript)(BSTR aExtensionID, INT aTabID, BSTR aCode, BOOL aFileSpecified, BOOL aInAllFrames);

  //STDMETHOD(get_browserActionInfos)(VARIANT* aBrowserActionInfos);
  STDMETHOD(getBrowserActions)(VARIANT* aBrowserActionsArray);
  STDMETHOD(addBrowserActionInfo)(LPDISPATCH aBrowserActionInfo);
  STDMETHOD(setBrowserActionUpdateCallback)(INT aTabId, LPDISPATCH aBrowserActionUpdateCallback);
  STDMETHOD(browserActionNotification)();

  STDMETHOD(testFunction(IDispatch** ppVal))
  {
    //ATLTRACE(L"TEST FUNCTION -----------------\n");

    return S_OK;
  }
  // -------------------------------------------------------------------------
  // IAnchoAddonService methods. See .idl for description.
  STDMETHOD(GetAddonBackground)(BSTR bsID, IAnchoAddonBackground ** ppRet);
  STDMETHOD(GetModulePath)(BSTR * pbsPath);
  STDMETHOD(registerRuntime)(INT aFrameTab, IAnchoRuntime * aRuntime, ULONG aHeartBeat, INT *aTabID);
  STDMETHOD(unregisterRuntime)(INT aTabID);
  STDMETHOD(createTabNotification)(INT aTabID, INT aRequestID);
  STDMETHOD(invokeEventObjectInAllExtensions)(BSTR aEventName, LPDISPATCH aArgs, VARIANT* aRet);
  STDMETHOD(invokeEventObjectInAllExtensionsWithIDispatchArgument)(BSTR aEventName, LPDISPATCH aArg);

  STDMETHOD(webBrowserReady)();

  STDMETHOD(registerBrowserActionToolbar)(INT aFrameTab, BSTR * aUrl, INT*aTabId);
  STDMETHOD(unregisterBrowserActionToolbar)(INT aTabId);
  STDMETHOD(getDispatchObject)(IDispatch **aRet);
private:
  int getFrameTabID(int aFrameTab);

  void fillWindowInfo(HWND aWndHandle, CIDispatchHelper &aInfo);
  HWND getCurrentWindowHWND();
  bool isIEWindow(HWND);

  INT winHWNDToId(HWND aHwnd)
  { return reinterpret_cast<INT>(aHwnd); }

  HWND winIdToHWND(INT aWinId)
  { return reinterpret_cast<HWND>(aWinId); }


  class ATabCreatedCallback: public ACommand
  {
  public:
#if _HAS_CPP0X
  typedef std::shared_ptr<ATabCreatedCallback> Ptr;
#else
  typedef std::tr1::shared_ptr<ATabCreatedCallback> Ptr;
#endif

    void operator()(INT aTabID)
    { execute(aTabID); }
    virtual void execute(INT aTabID) = 0;
  };
  class TabCreatedCallback;
  class WindowCreatedCallback;

  typedef std::map<int, ATabCreatedCallback::Ptr> CreateTabCallbackMap;


  HRESULT createTabImpl(CIDispatchHelper &aProperties, ATabCreatedCallback::Ptr aCallback, bool aInNewWindow);
  HRESULT createWindowImpl(CIDispatchHelper &aProperties, ATabCreatedCallback::Ptr aCallback);

  HRESULT removeTab(INT aTabId, LPDISPATCH aCallback);
  HRESULT executeScriptInTab(BSTR aExtensionID, INT aTabID, BSTR aCode, BOOL aFileSpecified);

  HRESULT FindActiveBrowser(IWebBrowser2** webBrowser);
private:
    //Private type declarations
  struct RuntimeRecord {
    RuntimeRecord(IAnchoRuntime *aRuntime = NULL, unsigned long aHeartbeatId = 0)
      : runtime(aRuntime), hearbeatMaster(aHeartbeatId) {}

    CComPtr<IAnchoRuntime> runtime;
    CIDispatchHelper callback;
    HeartbeatMaster hearbeatMaster;
  };
  typedef std::map<int, RuntimeRecord> RuntimeMap;

  class CreateTabCommand;
  class CreateWindowCommand;

  typedef std::map<int, int> FrameTabToTabIDMap;

  // a map containing all addon background objects - one per addon
  typedef std::map<std::wstring, CAnchoAddonBackgroundComObject*> BackgroundObjectsMap;
  typedef std::map<int, CIDispatchHelper> BrowserActionCallbackMap;

  typedef CComCritSecLock<CComAutoCriticalSection> CSLock;
private:
  //private methods
  void checkBHOConnections();

  IAnchoRuntime &getRuntime(int aTabId)
  {
    RuntimeMap::iterator it = m_Runtimes.find(aTabId);
    if (it == m_Runtimes.end()) {
      throw std::runtime_error("Runtime not found");
    }
    return *(it->second.runtime);
  }


private:
  // -------------------------------------------------------------------------
  // Private members.


  BackgroundObjectsMap          m_BackgroundObjects;
  RuntimeMap                    m_Runtimes;
  CComAutoCriticalSection       mRuntimesCriticalSection;
  volatile bool                 mShouldCheckBHOConnections;

  CreateTabCallbackMap          m_CreateTabCallbacks;
  CComPtr<ComSimpleJSArray>     m_BrowserActionInfos;
  BrowserActionCallbackMap      m_BrowserActionCallbacks;
  FrameTabToTabIDMap            m_FrameTabIds;
  CommandQueue                  m_WebBrowserPostInitTasks;
  ServiceTimer                  mBHOHeartbeatTimer;

  // Path to this exe and also to magpie.
  CString                       m_sThisPath;

  CComPtr<IIECookieManager>     m_Cookies;

  int                           m_NextTabID;
  int                           m_NextRequestID;
};

OBJECT_ENTRY_AUTO(__uuidof(AnchoAddonService), CAnchoAddonService)

//--------------------------------------------------------------------

class CAnchoAddonService::TabCreatedCallback: public ATabCreatedCallback
{
public:
  TabCreatedCallback(CAnchoAddonService &aService, LPDISPATCH aCreator, LPDISPATCH aCallback)
    : mService(aService), mCreator(aCreator), mCallback(aCallback)
  {}
  void execute(INT aTabID)
  {
    CComVariant tabInfo;
    if (FAILED(mService.getTabInfo(aTabID, mCreator, &tabInfo))) {
      throw std::runtime_error("Failed to get tab info");
    }
    if (FAILED(mCallback.Invoke1((DISPID) 0, &tabInfo))) {
      throw std::runtime_error("Failed to invoke callback for on tab create");
    }
  }
protected:
  CAnchoAddonService &mService;
  CIDispatchHelper mCreator;
  CIDispatchHelper mCallback;
};

class CAnchoAddonService::WindowCreatedCallback: public ATabCreatedCallback
{
public:
  WindowCreatedCallback(CAnchoAddonService &aService, LPDISPATCH aCreator, LPDISPATCH aCallback)
    : mService(aService), mCreator(aCreator), mCallback(aCallback)
  {}
  void execute(INT aTabID)
  {
    CComVariant tabInfo;
    if (FAILED(mService.getTabInfo(aTabID, mCreator, &tabInfo)) || tabInfo.vt != VT_DISPATCH) {
      throw std::runtime_error("Failed to get tab info");
    }
    CIDispatchHelper tabInfoHelper = tabInfo.pdispVal;
    int winId;
    if (FAILED((tabInfoHelper.Get<int, VT_I4>(L"windowId", winId)))) {
      throw std::runtime_error("Failed to get windowID from tab info");
    }
    CComVariant windowInfo;
    if (FAILED(mService.getWindow(winId, mCreator, FALSE, &windowInfo))) {
      throw std::runtime_error("Failed to get window info");
    }
    if (FAILED(mCallback.Invoke1((DISPID) 0, &windowInfo))) {
      throw std::runtime_error("Failed to invoke callback for on tab create");
    }
  }
protected:
  CAnchoAddonService &mService;
  CIDispatchHelper mCreator;
  CIDispatchHelper mCallback;
};


class CAnchoAddonService::CreateTabCommand: public AQueuedCommand
{
public:
  CreateTabCommand(CAnchoAddonService &aService, LPDISPATCH aProperties, LPDISPATCH aCreator, LPDISPATCH aCallback)
    : mService(aService), mProperties(aProperties), mCreator(aCreator), mCallback(aCallback)
  {}
  void execute()
  {
    mService.createTabImpl(mProperties, ATabCreatedCallback::Ptr(new TabCreatedCallback(mService, mCreator, mCallback)), false);
  }
protected:
  CAnchoAddonService &mService;
  CIDispatchHelper mProperties;
  CIDispatchHelper mCreator;
  CIDispatchHelper mCallback;
};

class CAnchoAddonService::CreateWindowCommand: public AQueuedCommand
{
public:
  CreateWindowCommand(CAnchoAddonService &aService, LPDISPATCH aProperties, LPDISPATCH aCreator, LPDISPATCH aCallback)
    : mService(aService), mProperties(aProperties), mCreator(aCreator), mCallback(aCallback)
  {}
  void execute()
  {
    mService.createWindowImpl(mProperties, ATabCreatedCallback::Ptr(new WindowCreatedCallback(mService, mCreator, mCallback)));
  }
protected:
  CAnchoAddonService &mService;
  CIDispatchHelper mProperties;
  CIDispatchHelper mCreator;
  CIDispatchHelper mCallback;
};

