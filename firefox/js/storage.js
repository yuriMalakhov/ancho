(function() {

  var Cc = Components.classes;
  var Ci = Components.interfaces;
  var Cu = Components.utils;

  Cu.import("resource://gre/modules/Services.jsm");
  Cu.import("resource://gre/modules/FileUtils.jsm");

  var StorageAPI = function(extensionState, contentWindow, storageSpace) {
    var dbFile = FileUtils.getFile('ProfD', ['ancho_storage.sqlite3']);
    this._connection = Services.storage.openDatabase(dbFile); // create the file if it does not exist
    this._tableName = extensionState.id.replace(/[^A-Za-z]/g, '_') + '_' + storageSpace; // no sanitization necessary
    this._connection.executeSimpleSQL('CREATE TABLE IF NOT EXISTS '+this._tableName+' ( key TEXT PRIMARY KEY, value TEXT )');
    // A separate CREATE INDEX command for keys not needed:
    // http://stackoverflow.com/questions/3379292/is-an-index-needed-for-a-primary-key-in-sqlite
  };

  function dbError(err) {
    throw new Error('Storage error: ' + err.message);
  }

  function _prepareArrayArgument(arg, results) {
    if (Array.isArray(arg)) {
      return arg;
    } else if (typeof arg === 'string') {
      return [arg];
    } else if (typeof arg === 'object' && results) {
      var array = [];
      for (var item in arg) {
        array.push(item);
        results[item] = arg[item]; // preparing defaults
      }
      return array;
    } else {
      // Signal error.
      return null;
    }
  }

  StorageAPI.prototype = {
    get: function(keys, callback) {
      if (keys) {
        var results = {};
        var myKeys = _prepareArrayArgument(keys, results);
        if (!myKeys) {
          throw new Error("Invocation of method get doesn't match definition");
        }

        var callCallback = true;
        var myCallback = function(results) {
          if (callCallback && typeof callback === 'function') {
            callCallback = false;
            callback(results);
          }
        };
        if (myKeys.length) {
          var statement =
            this._connection.createStatement('SELECT key, value FROM '+this._tableName+' WHERE key IN (:key)');
          var par, params = statement.newBindingParamsArray();
          for (var i=0; i<myKeys.length; i++) {
            par = params.newBindingParams();
            par.bindByName('key', myKeys[i]);
            params.addParams(par);
          }
          statement.bindParameters(params);

          statement.executeAsync({
            handleResult: function(resultRows) {
              var key, row = resultRows.getNextRow();
              while (row) {
                key = row.getResultByName('key');
                results[key] = JSON.parse(row.getResultByName('value'));
                row = resultRows.getNextRow();
              }
              myCallback(results);
            },

            // handleResult is not called with the empty resultRows
            // so we have to call back in this case
            handleCompletion: function(reason) {
              if (reason === Ci.mozIStorageStatementCallback.REASON_FINISHED) {
                myCallback(results);
              } else {
                dbError({ message: 'select statement not finished' });
              }
            },

            handleError: dbError
          });
        } else {
          // Empty myKeys has to be handled separately otherwise statement.bindParameters fails.
          // Note that get([]) and get({}) work in Chrome without errors.
          myCallback({});
        }
      }
    },

    set: function(items, callback) {
      if (typeof items === 'object') {
        var statement =
          this._connection.createStatement('REPLACE INTO '+this._tableName+' (key, value) VALUES (:key, :value)');

        var par, params = statement.newBindingParamsArray();
        for (var key in items) {
          par = params.newBindingParams();
          par.bindByName('key', key);
          par.bindByName('value', JSON.stringify(items[key]));
          params.addParams(par);
        }
        statement.bindParameters(params);

        statement.executeAsync({
          handleCompletion: function(reason) {
            if (reason === Ci.mozIStorageStatementCallback.REASON_FINISHED) {
              if (typeof callback === 'function') {
                callback();
              }
            } else {
              dbError({ message: 'replace statement not finished' });
            }
          },

          handleError: dbError
        });

      } else {
        throw new Error("Invocation of set doesn't match definition");
      }
    },

    remove: function(keys, callback) {
      if (keys) {
        var results = {};
        var myKeys = _prepareArrayArgument(keys, results);
        if (!myKeys) {
          throw new Error("Invocation of method remove doesn't match definition");
        }
        var statement =
          this._connection.createStatement('DELETE FROM '+this._tableName+' WHERE key IN (:key)');

        var par, params = statement.newBindingParamsArray();
        for (var i=0; i<myKeys.length; i++) {
          par = params.newBindingParams();
          par.bindByName('key', myKeys[i]);
          params.addParams(par);
        }
        statement.bindParameters(params);

        statement.executeAsync({
          handleCompletion: function(reason) {
            if (reason === Ci.mozIStorageStatementCallback.REASON_FINISHED) {
              if (typeof callback === 'function') {
                callback();
              }
            } else {
              dbError({ message: 'delete statement not finished' });
            }
          },

          handleError: dbError
        });
      }
    }
  };

  module.exports = StorageAPI;

}).call(this);

