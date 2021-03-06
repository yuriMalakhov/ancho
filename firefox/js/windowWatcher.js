(function() {

  var Cu = Components.utils;
  Cu.import('resource://gre/modules/Services.jsm');

  const BROWSER_WINDOW_TYPE = 'navigator:browser';

  var Event = function() {
    this.callbacks = [];
  };
  Event.prototype.addListener = function(callback) {
    this.callbacks.push(callback);
    return this;
  };

  Event.prototype.removeListener = function(callback) {

    var index = this.callbacks.indexOf(callback);
    if (index >= 0) {
      this.callbacks.splice(index, 1);
    }
    return this;
  };

  Event.prototype.clearListeners = function() {
    this.callbacks = [];
    return this;
  };

  Event.prototype.hasListeners = function() {
    return this.callbacks.length > 0;
  };

  Event.prototype.fire = function() {
    var args = arguments;
    var results = [];
    for ( var i = 0; i < this.callbacks.length; i++) {
      var callback = this.callbacks[i];
      if (callback) {
        var result = callback.apply(callback, args);
        results.push(result);
      }
    }
    return results;
  };

  function _isBrowserWindow(browserWindow) {
    return BROWSER_WINDOW_TYPE === browserWindow.document.documentElement.getAttribute('windowtype');
  }

  var WindowWatcherImpl = function() {
    this.loaders = new Event();
    this.unloaders = new Event();
    this.notificationListener = null;
  };

  WindowWatcherImpl.prototype.unload = function() {
    if (this.notificationListener != null) {
      Services.ww.unregisterNotification(this.notificationListener);
      this.notificationListener = null;
    }
    this.forAllWindows(function(browserWindow) {
      this.unloaders.fire(browserWindow);
    });
    this.loaders = new Event();
    this.unloaders = new Event();
  };

  WindowWatcherImpl.prototype.load = function() {
    if (this.notificationListener == null) {
      var self = this;
      this.notificationListener = function(browserWindow, topic) {
        if (topic === "domwindowopened") {
          if ('complete' === browserWindow.document.readyState && _isBrowserWindow(browserWindow)) {
            self.loaders.fire(browserWindow);
          } else {
            browserWindow.addEventListener('load', function() {
              browserWindow.removeEventListener('load', arguments.callee, false);
              if (_isBrowserWindow(browserWindow)) {
                self.loaders.fire(browserWindow);
              }
            });
          }
        }
        if (topic === "domwindowclosed") {
          if (_isBrowserWindow(browserWindow)) {
            self.unloaders.fire(browserWindow);
          }
        }
      };
      Services.ww.registerNotification(this.notificationListener);
    }
  };

  WindowWatcherImpl.prototype.register = function(loader, unloader) {
    this.loaders.addListener(loader);
    this.unloaders.addListener(unloader);

    // start listening of browser window open/close events
    this.load();

    // go through open windows and call loader there
    this.forAllWindows(function(browserWindow) {
      if ('complete' === browserWindow.document.readyState) {
        // Document is fully loaded so we can watch immediately.
        loader(browserWindow);
      } else {
        // Wait for the window to load before watching.
        browserWindow.addEventListener('load', function() {
          browserWindow.removeEventListener('load', arguments.callee, false);
          loader(browserWindow);
        });
      }
    });
  };

  WindowWatcherImpl.prototype.forAllWindows = function(callback) {
    var browserWindows = Services.wm.getEnumerator("navigator:browser");

    while (browserWindows.hasMoreElements()) {
      var browserWindow = browserWindows.getNext();
      callback.call(this, browserWindow);
    }
  };

  WindowWatcherImpl.prototype.isActiveBrowserWindow = function(browserWindow) {
    return browserWindow === Services.wm.getMostRecentWindow("navigator:browser");
  };

  WindowWatcherImpl.prototype.isActiveTab = function(browserWindow, tab) {
    return browserWindow.gBrowser.selectedTab === tab;
  };

  module.exports = new WindowWatcherImpl();

}).call(this);
