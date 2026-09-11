import './compost/components/compost-button.js';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const send = text => parent.postMessage(encoder.encode(text).buffer, '*');

addEventListener('message', ({data, source}) => {
  // The native CHOC bridge dispatches a MessageEvent without a source window.
  if (source !== parent && !(source === null && parent === window)) return;
  if (!(data instanceof ArrayBuffer) && !ArrayBuffer.isView(data)) return;
  console.log(decoder.decode(data));
});

send('ready');
