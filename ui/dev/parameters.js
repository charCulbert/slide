// A hand copy of ../../Parameters.h, for the development harness only. The shipped
// face learns all of this from the plugin's metadata handshake (D10); nothing here
// is imported by ui/face.js or ui/main.js.

export const divisionNames = [
  '1/128T', '1/128', '1/64T', '1/128.', '1/64', '1/32T', '1/64.', '1/32',
  '1/16T', '1/32.', '1/16', '1/8T', '1/16.', '1/8', '1/4T', '1/8.',
  '1/4', '1/2T', '1/4.', '1/2', '1/1T', '1/2.', '1/1', '1/1.'
];

const linkNames = ['Ratio', 'Difference', 'Off'];
const switchNames = ['Off', 'On'];
const modeNames = ['Stereo', 'Ping pong', 'Right is a tap', 'Left is a tap'];
const mediumNames = ['Tape', 'Oil can', 'Bucket', 'Tide', 'Digital'];

// id, identifier, name, unit, min, max, initial, step, mid, digits, options
export const parameters = [
  [0, 'left', 'Left', 'ms', 1, 2000, 350, 0.1, 200, 1, []],
  [1, 'right', 'Right', 'ms', 1, 2000, 525, 0.1, 200, 1, []],
  [2, 'link', 'Link', '', 0, 2, 0, 1, 0, 0, linkNames],
  [3, 'ratio', 'Ratio', 'x', 0.5, 4, 1.5, 0.001, 1, 3, []],
  [4, 'difference', 'Difference', 'ms', -2000, 2000, 175, 0.1, 0, 1, []],
  [5, 'sync', 'Sync', '', 0, 1, 0, 1, 0, 0, switchNames],
  [6, 'left_division', 'Left division', '', 0, 23, 13, 1, 0, 0, divisionNames],
  [7, 'right_division', 'Right division', '', 0, 23, 15, 1, 0, 0, divisionNames],
  [8, 'repeats', 'Repeats', '', 1, 64, 8, 1, 8, 0, []],
  [9, 'hold', 'Hold', '', 0, 1, 0, 1, 0, 0, switchNames],
  [10, 'shape', 'Shape', '', -1, 1, -0.6, 0.01, 0, 2, []],
  [11, 'blur', 'Blur', '%', 0, 100, 20, 1, 0, 0, []],
  [12, 'tone', 'Tone', '', -100, 100, 0, 1, 0, 0, []],
  [13, 'mix', 'Mix', '%', 0, 100, 50, 1, 0, 0, []],
  [14, 'mode', 'Mode', '', 0, 3, 0, 1, 0, 0, modeNames],
  [15, 'medium', 'Medium', '', 0, 4, 0, 1, 0, 0, mediumNames],
  [16, 'wear', 'Wear', '%', 0, 100, 35, 1, 0, 0, []]
];

const isLogarithmic = (min, max, mid) => min > 0 && mid > min && mid < max;

/** The same tab-separated lines Plugin.cpp's sendMetadata() writes. */
export function metadataLines() {
  return parameters.map(([id, identifier, name, unit, min, max, initial, step, mid, digits, options]) =>
    ['parameter', id, identifier, name, unit, min, max, initial, step, digits, mid,
      isLogarithmic(min, max, mid) ? 'log' : 'linear', options.join('|')].join('\t'));
}

export const initialValues = () =>
  Object.fromEntries(parameters.map(([id, , , , , , initial]) => [id, initial]));

export const byIdentifier = Object.fromEntries(
  parameters.map(p => [p[1], {id: p[0], min: p[4], max: p[5], initial: p[6]}]));
