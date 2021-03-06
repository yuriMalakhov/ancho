/****************************************************************************
 * AnchoRuntime.h : Declaration of the CAnchoRuntime
 * Copyright 2012 Salsita software (http://www.salsitasoft.com).
 * Author: Arne Seib <kontakt@seiberspace.de>
 ****************************************************************************/

#pragma once
#include "resource.h"       // main symbols

#include "ancho_i.h"

#if defined(_WIN32_WCE) && !defined(_CE_DCOM) && !defined(_CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA)
#error "Single-threaded COM objects are not properly supported on Windows CE platform, such as the Windows Mobile platforms that do not include full DCOM support. Define _CE_ALLOW_SINGLE_THREADED_OBJECTS_IN_MTA to force ATL to support creating single-thread COM object's and allow use of it's single-threaded COM object implementations. The threading model in your rgs file was set to 'Free' as that is the only threading model supported in non DCOM Windows CE platforms."
#endif

/*============================================================================
 * class CAnchoRuntime
 */
class CAnchoRuntime;
typedef IDispEventImpl<1, CAnchoRuntime, &DIID_DWebBrowserEvents2, &LIBID_SHDocVw, 1, 0> TWebBrowserEvents;
typedef IDispEventImpl<2, CAnchoRuntime, &IID_DAnchoBrowserEvents, &LIBID_anchoLib, 0xffff, 0xffff> TAnchoBrowserEvents;

class ATL_NO_VTABLE CAnchoRuntime :
  public CComObjectRootEx<CComSingleThreadModel>,
  public CComCoClass<CAnchoRuntime, &CLSID_AnchoRuntime>,
  public IObjectWithSiteImpl<CAnchoRuntime>,
  public TWebBrowserEvents,
  public TAnchoBrowserEvents,
  public IAnchoRuntime
{
  struct FrameRecord
  {
    FrameRecord(CComPtr<IWebBrowser2> aBrowser = CComPtr<IWebBrowser2>(), bool aIsTopLevel = false)
      : browser(aBrowser), isTopLevel(aIsTopLevel)
    { }

    CComPtr<IWebBrowser2> browser;
    bool isTopLevel;
  };

  typedef std::map<std::wstring, FrameRecord> FrameMap;
public:
  // -------------------------------------------------------------------------
  // ctor
  CAnchoRuntime() : m_WebBrowserEventsCookie(0), m_AnchoBrowserEventsCookie(0), m_ExtensionPageAPIPrepared(false), m_IsExtensionPage(false)
  {
  }

  // -------------------------------------------------------------------------
  // COM standard stuff
  DECLARE_REGISTRY_RESOURCEID(IDR_ANCHORUNTIME)
  DECLARE_NOT_AGGREGATABLE(CAnchoRuntime)
  DECLARE_PROTECT_FINAL_CONSTRUCT()

  // -------------------------------------------------------------------------
  // COM interface map
  BEGIN_COM_MAP(CAnchoRuntime)
    COM_INTERFACE_ENTRY(IAnchoRuntime)
    COM_INTERFACE_ENTRY(IObjectWithSite)
  END_COM_MAP()

  // -------------------------------------------------------------------------
  // COM sink map
  BEGIN_SINK_MAP(CAnchoRuntime)
    SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_BEFORENAVIGATE2, OnBrowserBeforeNavigate2)
    SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_NAVIGATECOMPLETE2, OnNavigateComplete)
    SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_PROGRESSCHANGE, OnBrowserProgressChange)
    SINK_ENTRY_EX(1, DIID_DWebBrowserEvents2, DISPID_DOWNLOADBEGIN, OnBrowserDownloadBegin)
    SINK_ENTRY_EX(2, IID_DAnchoBrowserEvents, 1, OnFrameStart)
    SINK_ENTRY_EX(2, IID_DAnchoBrowserEvents, 2, OnFrameEnd)
    SINK_ENTRY_EX(2, IID_DAnchoBrowserEvents, 3, OnFrameRedirect)

    SINK_ENTRY_EX(2, IID_DAnchoBrowserEvents, 4, OnBeforeRequest)
    SINK_ENTRY_EX(2, IID_DAnchoBrowserEvents, 5, OnBeforeSendHeaders)
  END_SINK_MAP()

  // -------------------------------------------------------------------------
  // COM standard methods
  HRESULT FinalConstruct()
  {
    return S_OK;
  }

  void FinalRelease()
  {
    DestroyAddons();
    Cleanup();
  }

public:
  // -------------------------------------------------------------------------
  // IObjectWithSiteImpl methods
  STDMETHOD(SetSite)(IUnknown *pUnkSite);

  // -------------------------------------------------------------------------
  // IAnchoRuntime methods
  STDMETHOD(reloadTab)();
  STDMETHOD(closeTab)();
  STDMETHOD(executeScript)(BSTR aExtensionId, BSTR aCode, INT aFileSpecified);
  STDMETHOD(updateTab)(LPDISPATCH aProperties);
  STDMETHOD(fillTabInfo)(VARIANT* aInfo);
  STDMETHOD(showBrowserActionBar)(INT aShow);

  // DWebBrowserEvents2 methods
  STDMETHOD_(void, OnNavigateComplete)(LPDISPATCH pDispatch, VARIANT *URL);
  STDMETHOD_(void, OnBrowserBeforeNavigate2)(LPDISPATCH pDisp, VARIANT *pURL, VARIANT *Flags,
    VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers, BOOL *Cancel);

  STDMETHOD_(void, OnBrowserDownloadBegin)();
  STDMETHOD_(void, OnBrowserProgressChange)(LONG Progress, LONG ProgressMax);

  // -------------------------------------------------------------------------
  // DAnchoBrowserEvents methods.
  STDMETHOD(OnFrameStart)(BSTR bstrUrl, VARIANT_BOOL bIsMainFrame);
  STDMETHOD(OnFrameEnd)(BSTR bstrUrl, VARIANT_BOOL bIsMainFrame);
  STDMETHOD(OnFrameRedirect)(BSTR bstrOldUrl, BSTR bstrNewUrl);

  STDMETHOD(OnBeforeRequest)(VARIANT aReporter);
  STDMETHOD(OnBeforeSendHeaders)(VARIANT aReporter);


private:
  // -------------------------------------------------------------------------
  // Methods
  HRESULT InitAddons();
  void DestroyAddons();
  HRESULT Init();
  HRESULT Cleanup();
  HRESULT InitializeContentScripting(BSTR bstrUrl, VARIANT_BOOL bIsRefreshingMainFrame, documentLoadPhase aPhase);
  HRESULT InitializeExtensionScripting(BSTR bstrUrl);

  struct BeforeRequestInfo
  {
    bool cancel;
    bool redirect;
    std::wstring newUrl;
  };
  HRESULT fireOnBeforeRequest(const std::wstring &aUrl, const std::wstring &aMethod, const std::wstring &aType, /*out*/ BeforeRequestInfo &aOutInfo);

  struct BeforeSendHeadersInfo
  {
    bool modifyHeaders;
    std::wstring headers;
  };
  HRESULT fireOnBeforeSendHeaders(const std::wstring &aUrl, const std::wstring &aMethod, const std::wstring &aType, /*out*/ BeforeSendHeadersInfo &aOutInfo);


  HWND getTabWindow();
  HWND getFrameTabWindow()
    {return findParentWindowByClass(L"Frame Tab");}
  HWND getMainWindow()
    {return findParentWindowByClass(L"IEFrame");}

  HWND findParentWindowByClass(std::wstring aClassName);
  bool isTabActive();
private:
  // -------------------------------------------------------------------------
  // Private members.
  typedef std::map<std::wstring, CComPtr<IAnchoAddon> > AddonMap;
  CComQIPtr<IWebBrowser2>                 m_pWebBrowser;
  CComPtr<IAnchoAddonService>             m_pAnchoService;
  AddonMap                                m_Addons;
  int                                     m_TabID;
  CComPtr<DAnchoBrowserEvents>            m_pBrowserEventSource;
  DWORD                                   m_WebBrowserEventsCookie;
  DWORD                                   m_AnchoBrowserEventsCookie;
  FrameMap                                m_Frames;

  bool                                    m_ExtensionPageAPIPrepared;
  bool                                    m_IsExtensionPage;

  HeartbeatSlave                          m_HeartBeatSlave;
};

OBJECT_ENTRY_AUTO(__uuidof(AnchoRuntime), CAnchoRuntime)
