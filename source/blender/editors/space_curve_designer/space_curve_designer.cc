/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spcurvedesigner
 *
 * Curve Designer editor.
 *
 * A panels-only editor (no canvas, no sidebar) — the WINDOW region
 * IS the panels area, so Python panels with `bl_region_type='WINDOW'`
 * fill the editor. The Layers UIList (the addon's space_curve_designer
 * module) is the primary content. Designed as a thin shell — no
 * editor-specific drawing or interaction in C beyond hosting Blender's
 * standard panel layout, header, and per-region listener for redraws.
 */

#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "BKE_context.hh"
#include "BKE_screen.hh"

#include "ED_screen.hh"
#include "ED_space_api.hh"

#include "GPU_immediate.hh"
#include "GPU_state.hh"

#include "UI_resources.hh"

#include "BLO_read_write.hh"

#include "WM_api.hh"
#include "WM_types.hh"

namespace blender {

/* ******************** default callbacks for curve designer space ***************** */

static SpaceLink *curve_designer_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARegion *region;
  SpaceCurveDesigner *scd;

  scd = MEM_new<SpaceCurveDesigner>("init curve designer");
  scd->spacetype = SPACE_CURVE_DESIGNER;

  /* header */
  region = BKE_area_region_new();
  BLI_addtail(&scd->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* Grids strip (top pane) — resizable via draggable divider
   * between it and the main WINDOW region below. Hosts Python
   * panels registered against `bl_region_type='UI'`. */
  region = BKE_area_region_new();
  BLI_addtail(&scd->regionbase, region);
  region->regiontype = RGN_TYPE_UI;
  region->alignment = RGN_ALIGN_TOP;

  /* Bottom pane — resizable via divider between it and WINDOW.
   * Hosts Python panels registered against `bl_region_type='TOOLS'`.
   * Uses TOOLS region-type simply to have a distinct id from the
   * top UI region; naming is generic since Blender's region-type
   * enum doesn't have a "bottom strip" identifier. */
  region = BKE_area_region_new();
  BLI_addtail(&scd->regionbase, region);
  region->regiontype = RGN_TYPE_TOOLS;
  region->alignment = RGN_ALIGN_BOTTOM;

  /* main region — hosts Blender's standard panel layout
   * (ED_region_panels). Sits between the top UI strip and the
   * bottom TOOLS strip, gets whatever vertical space is left. */
  region = BKE_area_region_new();
  BLI_addtail(&scd->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return reinterpret_cast<SpaceLink *>(scd);
}

/* Doesn't free the space-link itself. */
static void curve_designer_free(SpaceLink * /*sl*/) {}

/* spacetype; init callback */
static void curve_designer_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *curve_designer_duplicate(SpaceLink *sl)
{
  SpaceCurveDesigner *scd_new = MEM_dupalloc(reinterpret_cast<SpaceCurveDesigner *>(sl));

  /* Nothing to clear on duplicate yet. */

  return reinterpret_cast<SpaceLink *>(scd_new);
}

/**
 * Draw a full-region tinted overlay on top of already-rendered
 * panel content. Uses `GPU_BLEND_ALPHA` so the panel text/widgets
 * behind stay readable. Colour is passed as `(r, g, b, a)` — keep
 * alpha low (~0.10) so it's just a wash.
 */
static void curve_designer_draw_region_tint(const ARegion *region, const float rgba[4])
{
  const float x2 = float(region->winx);
  const float y2 = float(region->winy);

  GPU_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", blender::gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_UNIFORM_COLOR);
  immUniform4fv("color", rgba);

  immBegin(GPU_PRIM_TRI_STRIP, 4);
  immVertex2f(pos, 0.0f, 0.0f);
  immVertex2f(pos, x2, 0.0f);
  immVertex2f(pos, 0.0f, y2);
  immVertex2f(pos, x2, y2);
  immEnd();

  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
}

/**
 * Draw wrapper for the main WINDOW region.
 *
 * Order:
 *   1. `ED_region_panels`  — panel content.
 *   2. Green tint overlay  — visually marks this region as the
 *      "main / bottom" pane.
 *   3. Dashed amber line   — divider stripe along the top edge
 *      (where WINDOW meets the top UI "Grids" region and the
 *      resize handle lives).
 */
static void curve_designer_window_region_draw(const bContext *C, ARegion *region)
{
  ED_region_panels(C, region);

  /* Green tint — bottom pane. */
  const float green_tint[4] = {0.15f, 0.55f, 0.25f, 0.10f};
  curve_designer_draw_region_tint(region, green_tint);

  /* Dashed divider on the top edge. Draw in pixel space (region-
   * local), one pixel down from `winy` so the stripe stays inside
   * the region and isn't clipped. */
  const float y = float(region->winy) - 1.0f;
  const float x1 = 0.0f;
  const float x2 = float(region->winx);

  GPU_line_width(1.5f);
  GPU_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", blender::gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  GPU_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);
  immUniform1i("colors_len", 0);
  immUniform4f("color", 0.85f, 0.60f, 0.15f, 0.95f);
  immUniform1f("dash_width", 8.0f);
  immUniform1f("udash_factor", 0.5f);

  immBegin(GPU_PRIM_LINES, 2);
  immVertex2f(pos, x1, y);
  immVertex2f(pos, x2, y);
  immEnd();

  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
  GPU_line_width(1.0f);
}

/**
 * Draw wrapper for the top UI region (Grids strip).
 * Calls the standard panel-host draw, then applies a blue tint
 * overlay to visually distinguish it from the green WINDOW region
 * below.
 */
static void curve_designer_ui_region_draw(const bContext *C, ARegion *region)
{
  ED_region_panels(C, region);

  const float blue_tint[4] = {0.15f, 0.30f, 0.65f, 0.10f};
  curve_designer_draw_region_tint(region, blue_tint);
}

/**
 * Draw wrapper for the bottom TOOLS region.
 * Order:
 *   1. `ED_region_panels`   — panel content.
 *   2. Orange tint overlay  — marks this region as the bottom pane.
 *   3. Dashed amber line    — divider stripe along the top edge
 *      (where TOOLS meets WINDOW above; resize handle lives here).
 */
static void curve_designer_tools_region_draw(const bContext *C, ARegion *region)
{
  ED_region_panels(C, region);

  const float orange_tint[4] = {0.85f, 0.45f, 0.10f, 0.10f};
  curve_designer_draw_region_tint(region, orange_tint);

  /* Dashed divider on top edge. Same amber as the WINDOW-region
   * divider so the two seams read as visually related — the two
   * dashed stripes bracket the WINDOW region between them. */
  const float y = float(region->winy) - 1.0f;
  const float x1 = 0.0f;
  const float x2 = float(region->winx);

  GPU_line_width(1.5f);
  GPU_blend(GPU_BLEND_ALPHA);

  GPUVertFormat *format = immVertexFormat();
  const uint pos = GPU_vertformat_attr_add(format, "pos", blender::gpu::VertAttrType::SFLOAT_32_32);
  immBindBuiltinProgram(GPU_SHADER_3D_LINE_DASHED_UNIFORM_COLOR);

  float viewport_size[4];
  GPU_viewport_size_get_f(viewport_size);
  immUniform2f("viewport_size", viewport_size[2], viewport_size[3]);
  immUniform1i("colors_len", 0);
  immUniform4f("color", 0.85f, 0.60f, 0.15f, 0.95f);
  immUniform1f("dash_width", 8.0f);
  immUniform1f("udash_factor", 0.5f);

  immBegin(GPU_PRIM_LINES, 2);
  immVertex2f(pos, x1, y);
  immVertex2f(pos, x2, y);
  immEnd();

  immUnbindProgram();
  GPU_blend(GPU_BLEND_NONE);
  GPU_line_width(1.0f);
}

static void curve_designer_main_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  switch (wmn->category) {
    case NC_SCENE:
      ED_region_tag_redraw(region);
      break;
    case NC_OBJECT:
      if (ELEM(wmn->data, ND_TRANSFORM, ND_DRAW)) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_GEOM:
      ED_region_tag_redraw(region);
      break;
    case NC_WINDOW:
      ED_region_tag_redraw(region);
      break;
  }
}

static void curve_designer_operatortypes() {}

static void curve_designer_keymap(wmKeyConfig *keyconf)
{
  /* Per-space keymap. Empty for now — Layers UIList interactions
   * come through standard panel widget handling and don't need
   * custom bindings. Future Layers operators (e.g. duplicate,
   * delete by Del key) can register here. */
  WM_keymap_ensure(keyconf, "Window", SPACE_EMPTY, RGN_TYPE_WINDOW);
  WM_keymap_ensure(keyconf, "Curve Designer", SPACE_CURVE_DESIGNER, RGN_TYPE_WINDOW);
}

/* add handlers, stuff you only do once or on area/region changes */
static void curve_designer_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ED_region_header_init(region);
}

static void curve_designer_header_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  switch (wmn->category) {
    case NC_SCREEN:
      if (wmn->data == ND_LAYER) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_SCENEBROWSE) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void curve_designer_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  writer->write_struct_cast<SpaceCurveDesigner>(sl);
}

void ED_spacetype_curve_designer()
{
  std::unique_ptr<SpaceType> st = std::make_unique<SpaceType>();
  ARegionType *art;

  st->spaceid = SPACE_CURVE_DESIGNER;
  STRNCPY_UTF8(st->name, "Curve Designer");

  st->create = curve_designer_create;
  st->free = curve_designer_free;
  st->init = curve_designer_init;
  st->duplicate = curve_designer_duplicate;
  st->operatortypes = curve_designer_operatortypes;
  st->keymap = curve_designer_keymap;
  st->blend_write = curve_designer_space_blend_write;

  /* regions: main window — panel-host. Layers UIList renders here.
   * Uses `curve_designer_window_region_draw` (which wraps
   * `ED_region_panels`) so the dashed divider stripe gets painted
   * along the top edge on top of the panel content. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer main region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = ED_region_panels_init;
  art->draw = curve_designer_window_region_draw;
  art->listener = curve_designer_main_region_listener;

  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->init = curve_designer_header_region_init;
  art->layout = ED_region_header_layout;
  art->draw = ED_region_header_draw;
  art->listener = curve_designer_header_region_listener;

  BLI_addhead(&st->regiontypes, art);

  /* regions: UI (top strip for Grids list — resizable via the
   * divider between this region and the main WINDOW region).
   * Uses the same panel-host callbacks as WINDOW so any Python
   * Panel registered with `bl_space_type='CURVE_DESIGNER'` and
   * `bl_region_type='UI'` renders here with auto-scroll on
   * overflow. `prefsizey` sets the pane's initial height in
   * pixels; user drag persists per workspace via Blender's
   * region-resize handling. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer UI region");
  art->regionid = RGN_TYPE_UI;
  art->prefsizey = 200;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = ED_region_panels_init;
  art->draw = curve_designer_ui_region_draw;
  art->listener = curve_designer_main_region_listener;

  BLI_addhead(&st->regiontypes, art);

  /* regions: TOOLS (bottom strip — third pane, resizable via the
   * divider between this region and the main WINDOW region above).
   * Same panel-host callbacks as WINDOW / UI. `prefsizey` = 150
   * (slightly shorter than the top strip's 200 by default). */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer TOOLS region");
  art->regionid = RGN_TYPE_TOOLS;
  art->prefsizey = 150;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = ED_region_panels_init;
  art->draw = curve_designer_tools_region_draw;
  art->listener = curve_designer_main_region_listener;

  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

}  // namespace blender
