# Compost request: a cycling choice mode for `compost-button`

Measured against `7b75bb3`.

## Context

Slide's top band has five words that change a discrete parameter by clicking
them: Link (Ratio / Difference / Off), Mode (Stereo / Ping pong / Right is a
tap / Left is a tap), Medium (Tape / Oil can / Bucket / Tide / Digital), plus
Sync and Hold. Clicking the word advances to the next choice and the word
changes. Sync and Hold are `compost-button mode="switch"` styled through
`part="button"` and `part="label"`, which works today. Link, Mode and Medium
have three to five states, and the button only knows `switch` and `trigger`.
`compost-select` covers choices, but as a native dropdown.

Your value-controls plan lists choice widgets as a separate proposal. This is
the smallest version of that.

## The ask

A third mode on `compost-button`:

```html
<compost-button mode="cycle" parameter-id="medium"
  text="Tape|Oil can|Bucket|Tide|Digital" value="0"></compost-button>
```

- `text` uses the existing pipe convention from `valueTextOption()` in
  `src/utils.js:44`; the label shows the entry at the current value.
- A press advances `value` by one and wraps, inside a parameter gesture:
  `parameter-begin`, `parameter-edit` with the new value, `parameter-end`.
  `kind` is `discrete`. Shift-press steps backwards.
- `setValue(value, false, source)` updates the label silently, so host values
  flow in through `ParameterController.applyValue()` as they do now.
- `parameterValues` returns the integer list `0..n-1` so
  `definitionFromControl()` yields the right range and step without a
  definitions array, the way `compost-select` does at `compost-select.js:142`.
- Keyboard: Space/Enter advances, arrows step either way, Home/End go to the
  first and last choice. ARIA: `aria-valuetext` carries the label.
- Existing `switch` and `trigger` behaviour unchanged.

## Not asked

A popup or list of the choices, and any new visual. The cycle mode reuses the
button's parts, so a host can style it as a bare word.

## Fallback

Slide can draw these three as canvas chips and dispatch the parameter events
itself. That loses keyboard and ARIA, which is why the ask.
