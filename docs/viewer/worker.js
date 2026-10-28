"use strict";
// Web Worker entry: runs the FFT-VIZ core off the main thread. All logic is in engine.js (shared with the fallback).
importScripts("engine.js");
const host = new FvCore.Host((msg, transfer) => self.postMessage(msg, transfer || []));
self.onmessage = (e) => host.handle(e.data);
