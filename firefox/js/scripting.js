(function() {
  var Cc = Components.classes;
  var Ci = Components.interfaces;
  var Cu = Components.utils;
  var CC = Components.Constructor;

  Cu.import('resource://gre/modules/Services.jsm');
  Cu.import('resource://ancho/modules/Require.jsm');

  var ExtensionState = require('./state');
  var API = require('./api');
  var readStringFromUrl = require('./utils').readStringFromUrl;
  var contentScripts = require('./config').contentScripts;
  var Config = require('./config');

  function prepareWindow(window) {
    if (!('chrome' in window)) {
      var api = new API(window, ExtensionState);
      window.chrome = api.chrome;
      window.ancho = api.ancho;
      window.console = api.console;

      window.addEventListener('unload', function(event) {
        window.removeEventListener('unload', arguments.callee, false);
        delete window.chrome;
        delete window.ancho;
        delete window.console;
      });
    }
  }

  exports.applyContentScripts = function(win, spec) {
    var baseUrl = Services.io.newURI(spec, '', null);
    for (var i=0; i<contentScripts.length; i++) {
      var scriptInfo = contentScripts[i];
      var matches = scriptInfo.matches;
      var principal;
      // Preserving backwards compatibility with FF18 and older.
      if (Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULAppInfo).version.split('.')[0] >= 19) {
        principal = CC('@mozilla.org/systemprincipal;1', 'nsIPrincipal')();
      }
      else {
        principal = ExtensionState.backgroundWindow;
      }
      var sandbox = Cu.Sandbox(principal, { sandboxPrototype: win });
      var api = new API(win, ExtensionState);
      sandbox.chrome = api.chrome;
      sandbox.ancho = api.ancho;
      sandbox.console = api.console;
      sandbox.XMLHttpRequest = Require.XMLHttpRequest;
      for (var j=0; j<matches.length; j++) {
        if (spec.match(matches[j])) {
          for (var k=0; k<scriptInfo.js.length; k++) {
            if (sandbox.jQuery) {
              sandbox.jQuery.ajaxSettings.xhr = Require.createWrappedXMLHttpRequest;
            }
            var scriptUri = Services.io.newURI(Config.hostExtensionRoot
                  + scriptInfo.js[k], '', null);
            var script = readStringFromUrl(scriptUri);
            try {
              Cu.evalInSandbox(script, sandbox);
            } catch(err) {
              dump('Ancho: script "' + scriptInfo.js[k] + '" failed: "' + err.message + '"\n' + err.stack + '\n');
            }
          }
          break;
        }
      } // for
    } // for
  };

  // When we load a privileged HTML page we want all scripts to load as content
  // scripts so that they have access to the require function and chrome / ancho APIs.
  // So we strip the <script> tags out of the document and load them separately
  // as content scripts.
  exports.loadHtml = function(document, iframe, htmlSpec, callback) {
    var targetWindow = iframe.contentWindow;
    iframe.addEventListener('DOMWindowCreated', function(event) {
      var window = event.target.defaultView;
      // Check the wrappedJSObject for FF <= 18.
      if (window !== targetWindow && window.wrappedJSObject !== targetWindow) {
        return;
      }
      document.removeEventListener('DOMWindowCreated', arguments.callee, false);
      prepareWindow(targetWindow.wrappedJSObject);
    }, false);

    if (callback) {
      iframe.addEventListener('DOMContentLoaded', function(event) {
        iframe.removeEventListener('DOMContentLoaded', arguments.callee, false);
        callback(targetWindow.wrappedJSObject);
      }, false);
    }

    iframe.webNavigation.loadURI(htmlSpec,
        Ci.nsIWebNavigation.LOAD_FLAGS_NONE, null, null, null);
  };

}).call(this);
