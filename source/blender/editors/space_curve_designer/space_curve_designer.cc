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

  /* main region — hosts Blender's standard panel layout
   * (ED_region_panels). The Layers UIList lives here. No View2D
   * canvas, no sidebar — the editor IS the layers list. */
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

  /* regions: main window — panel-host. Layers UIList renders here. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer main region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->init = ED_region_panels_init;
  art->draw = ED_region_panels;
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

  BKE_spacetype_register(std::move(st));
}

}  // namespace blender
