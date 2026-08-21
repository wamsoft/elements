/*=============================================================================
   Copyright (c) 2026 Cycfi Research. All rights reserved.

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#if !defined(CYCFI_ELEMENTS_GAMEPAD_2026_06_06)
#define CYCFI_ELEMENTS_GAMEPAD_2026_06_06

#include <cstdint>

namespace cycfi::elements
{
   /**
    * \enum pad_button
    *
    * \brief
    *    Discrete gamepad buttons. Naming follows the Xbox / XInput
    *    convention but maps to the same physical positions on
    *    PlayStation / Switch controllers when those are reported as
    *    XInput-style gamepads (which is what SDL3's SDL_Gamepad layer
    *    does by default).
    */
   enum class pad_button : int16_t
   {
      unknown = -1,

      // Face buttons. The default mapping ties them to the four most
      // common UI verbs: A=accept, B=cancel, X=focus-back, Y=focus-next.
      a, b, x, y,

      // Directional pad.
      dpad_up, dpad_down, dpad_left, dpad_right,

      // Shoulder buttons / triggers as binary (the analog trigger
      // values are also delivered via pad_axis::lt / rt).
      lb, rb,
      lt_click, rt_click,

      // Stick clicks (L3 / R3).
      l3, r3,

      // Misc.
      back, start, guide,

      // Face buttons addressed by *position* instead of label.
      // A single physical press also raises the label-based value
      // above (a/b/x/y); a binding picks whichever basis it wants.
      // Nintendo pads label the south button B and the east button A,
      // so "the button on the right" is only expressible this way.
      face_south, face_east, face_west, face_north
   };

   /**
    * \enum pad_axis
    *
    * \brief
    *    Continuous gamepad axes. Each axis is normalized to
    *    [-1.0, +1.0] (or [0, 1] for triggers) before being delivered.
    *    `dpad_x` / `dpad_y` are populated from the discrete D-Pad
    *    buttons (-1 / 0 / +1) so the axis-mode plumbing can treat
    *    the D-Pad uniformly with the analog sticks.
    */
   enum class pad_axis : int16_t
   {
      unknown = -1,

      dpad_x,
      dpad_y,
      left_x,
      left_y,
      right_x,
      right_y,
      lt,
      rt
   };

   /**
    * \enum pad_axis_mode
    *
    * \brief
    *    How an individual axis (or axis group) participates in UI
    *    input.
    *
    *    - `disabled`: the axis is ignored.
    *    - `focus`   : tilting past the activation threshold moves
    *                  keyboard focus in the corresponding direction
    *                  (first hit immediately, then auto-repeat).
    *    - `value`   : tilt magnitude is delivered to the focused
    *                  widget's `pad_axis()` for continuous adjustment.
    *    - `both`    : try `value` first; if the widget did not
    *                  consume the input, fall through to `focus`.
    */
   enum class pad_axis_mode : int8_t
   {
      disabled,
      focus,
      value,
      both
   };

   struct pad_button_info
   {
      pad_button  button;
      bool        down;     // true on press, false on release
   };

   struct pad_axis_info
   {
      pad_axis    axis;
      float       value;    // -1.0f .. +1.0f after deadzone
   };
}

#endif
