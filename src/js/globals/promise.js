(function() {
  const resolveValueKey = Symbol(), rejectValueKey = Symbol(),
        subscribersKey = Symbol(), broadcastResolveKey = Symbol(), NullExecutor = Symbol(),
        broadcastRejectKey = Symbol(), taskKey = Symbol(), stateKey = Symbol();
  const PENDING = 0, RESOLVED = 1, REJECTED = 2;
  return class Promise {
    constructor(executor) {
      this[subscribersKey] = [];
      this[stateKey] = PENDING;
      const broadcastResolve = this[broadcastResolveKey] = function(value) {
        if (value === this)
          throw new TypeError('A promise cannot be resolved with itself');
        if (this[stateKey] !== PENDING)
          return;
        if (value instanceof Promise)
        {
          if (value[stateKey] !== PENDING) {
            if (value[stateKey] === RESOLVED)
            {
              this[taskKey] = Nexus.Scheduler.schedule(this[broadcastResolveKey].bind(this, value[resolveValueKey]));
            } else if (value[stateKey] === REJECTED) {
              this[taskKey] = Nexus.Scheduler.schedule(this[broadcastRejectKey].bind(this, value[rejectValueKey]));
            }
          } else {
            value[subscribersKey].push({
              resolve: this[broadcastResolveKey].bind(this),
              reject: this[broadcastRejectKey].bind(this),
              promise: this
            });
          }
          return;
        }
        this[resolveValueKey] = value;
        this[stateKey] = RESOLVED;
        this[subscribersKey].forEach(({ resolve, reject, promise }) => {
          if (resolve) {
            promise[taskKey] = Nexus.Scheduler.schedule(function() {
              try {
                promise[broadcastResolveKey](resolve(value));
              } catch(e) {
                if (reject)
                  promise[broadcastResolveKey](reject(e));
                else
                  promise[broadcastRejectKey](e);
              }
            }.bind(this));
          }
        });
      };
      const broadcastReject = this[broadcastRejectKey] = function(value) {
        if (this[stateKey] !== PENDING)
          return;
        this[rejectValueKey] = value;
        this[stateKey] = REJECTED;
        const rejections = this[subscribersKey].filter(v => v.reject);
        const resolves = this[subscribersKey].filter(v => v.resolve);
        if (rejections.length) {
          rejections.forEach(({ resolve, reject, promise }) => {
            promise[taskKey] = Nexus.Scheduler.schedule(function() {
              try {
                promise[broadcastResolveKey](reject(value));
              } catch(e) {
                e.innerException = value;
                promise[broadcastRejectKey](e);
              }
            }.bind(this));
          });
          return;
        } else if (resolves.length) {
          resolves.forEach(({ resolve, reject, promise }) => {
            promise[taskKey] = Nexus.Scheduler.schedule(function() {
              promise[broadcastRejectKey](value);
            }.bind(this));
          });
          return;
        }
        const error = new Error(value.message);
        error.stack = value.stack || error.stack;
        throw value instanceof Error ? error : new Error('unhandled rejection in promise');
      };
      if (executor === NullExecutor)
        return;
      if (typeof executor !== 'function') throw new TypeError('not a function');
      this[taskKey] = Nexus.Scheduler.schedule(() => {
        try {
          return executor(broadcastResolve.bind(this), broadcastReject.bind(this));
        } catch(e) {
          broadcastReject.call(this, e);
        }
      });
    }
    then(resolve, reject) {
      if (this[stateKey] !== PENDING)
      {
        if (this[stateKey] === RESOLVED && resolve) {
          return Promise.resolve(this[resolveValueKey]).then(resolve);
        } else if (this[stateKey] === REJECTED && reject) {
          return Promise.reject(this[rejectValueKey]).then(reject);
        }
      }
      if (!resolve && !reject) {
        return Promise.reject(new TypeError('invalid arguments passed to promise.then'));
      }
      const promise = new Promise(NullExecutor);
      this[subscribersKey].push({ resolve, reject, promise });
      return promise;
    }
    catch(handler) {
      return this.then(undefined, handler);
    }
    static resolve(value) {
      return new Promise((resolve, reject) => resolve(value));
    }
    static reject(value) {
      return new Promise((resolve, reject) => reject(value));
    }
    static all(collection) {
      if (typeof collection[Symbol.iterator] !== 'function')
        throw new TypeError('argument must be iterable');
      function allOrReject(resolve, reject) {
        const filtered = collection.filter(v => v[stateKey] !== PENDING);
        const rejection = filtered.find(v => v[stateKey] === REJECTED);
        if (rejection) {
          return reject(rejection[rejectValueKey]);
        } else if (filtered.length === collection.length) {
          return resolve(collection.map(v => v instanceof Promise ? v[resolveValueKey] : v));
        } else {
          Nexus.Scheduler.schedule(allOrReject.bind(this, resolve, reject));
        }
      }
      return new Promise(allOrReject.bind(this));
    }
    static race(collection) {
      if (typeof collection[Symbol.iterator] !== 'function')
        throw new TypeError('argument must be iterable');
      function firstOrReject(resolve, reject) {
        const filtered = collection.filter(v => v[stateKey] !== PENDING);
        const rejection = filtered.find(v => v[stateKey] === REJECTED);
        if (rejection) {
          return reject(rejection[rejectValueKey]);
        } else if (filtered.length) {
          return resolve(filtered[0]);
        } else {
          Nexus.Scheduler.schedule(firstOrReject.bind(this, resolve, reject));
        }
      }
      return new Promise(firstOrReject.bind(this));
    }
  }
})();
