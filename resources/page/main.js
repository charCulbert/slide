// The bridge. Messages over clap.webview in both directions (lib/messages.js);
// the face never talks to the plugin itself, and the plugin never knows how
// the face is drawn. plugin.cpp lists the messages above PluginSendMetadata.

import {sendMessage, onMessage} from './lib/messages.js';
import './face.js';

const face = document.createElement('slide-face');
// mote's data-editing idea: a value arriving from the plugin must not fight the
// gesture the user is in the middle of.
const editing = new Set();

// The face is the plug-in window's whole world: no frame, no chrome. The host sizes
// the window after the page loads; the face narrows by width inside its own canvas.
document.querySelector('#face-host').append(face);

// ---- UI -> plugin -----------------------------------------------------------

addEventListener('parameter-begin', ({detail}) => {
  const id = Number(detail.parameterID);
  editing.add(id);
  sendMessage({type: 'begin', id});
});
addEventListener('parameter-edit', ({detail}) => {
  sendMessage({type: 'value', id: Number(detail.parameterID), value: detail.value});
});
addEventListener('parameter-end', ({detail}) => {
  const id = Number(detail.parameterID);
  // Escape cancels a gesture: compost resets the control and reports the start
  // value only here, so resend it or the plugin keeps the last dragged value.
  if (detail.cancelled) sendMessage({type: 'value', id, value: detail.value});
  editing.delete(id);
  sendMessage({type: 'end', id});
});

// ---- plugin -> UI -----------------------------------------------------------

onMessage(message => {
  if (message.type === 'metadata') {
    face.setMetadata(message.parameters);
  } else if (message.type === 'values') {
    message.values.forEach((value, id) => {
      if (!editing.has(id)) face.setValue(id, value);
    });
  } else if (message.type === 'visual') {
    face.setTelemetry({bpm: message.bpm, wobble: message.wobble});
  }
});

const pull = () => {
  requestAnimationFrame(pull);
  if (!document.hidden) sendMessage({type: 'visual'});
};
requestAnimationFrame(pull);

sendMessage({type: 'ready'});
