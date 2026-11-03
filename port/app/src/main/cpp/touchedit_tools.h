/* Written by tools/editor_tools_sheet.py - do not edit by hand. */
#ifndef TOUCHEDIT_TOOLS_H
#define TOUCHEDIT_TOOLS_H

/* one per language set (tw_set_for_language): en ru de es jp kr zh */
#define TOOL_SETS 7
#define TOOL_HIGH 34.0f
static const float TOOL_STEP_OF[TOOL_SETS] = { 44.0f, 44.0f, 44.0f, 52.0f, 57.0f, 44.0f, 44.0f };

/* the opacity slider, in texels (the editor draws them at 2 f: a texel a unit) */
#define SLIDER_TRACK 112.0f
#define SLIDER_KNOB_H 15.0f
#define SLIDER_END_R 5.0f
#define SLIDER_LABEL_H 14.0f
#define SLIDER_PCT_LOW 10
#define SLIDER_PCT_HIGH 100
#define SLIDER_PCT_STEP 5
static const float OPACITY_W[TOOL_SETS] = { 37.0f, 72.0f, 47.0f, 41.0f, 41.0f, 40.0f, 41.0f };
static const float PCT_W[19] = { 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 16.0f, 21.0f };

#endif
