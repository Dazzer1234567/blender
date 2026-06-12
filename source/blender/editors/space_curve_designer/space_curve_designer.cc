/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spcurvedesigner
 *
 * Curve Designer editor (placeholder).
 *
 * Minimal "hello world" space type providing the standard 2D canvas plumbing
 * (header + main viewport region with View2D + theme background) so future
 * work can drop in curve editing logic without rewiring the editor itself.
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
#include "UI_view2d.hh"

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

  /* main region */
  region = BKE_area_region_new();
  BLI_addtail(&scd->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  /* Sensible defaults for a 2D pannable/zoomable canvas (Illustrator-like). */
  region->v2d.tot.xmin = -1000.0f;
  region->v2d.tot.ymin = -1000.0f;
  region->v2d.tot.xmax = 1000.0f;
  region->v2d.tot.ymax = 1000.0f;
  region->v2d.cur = region->v2d.tot;
  region->v2d.min[0] = 1.0f;
  region->v2d.min[1] = 1.0f;
  region->v2d.max[0] = 32000.0f;
  region->v2d.max[1] = 32000.0f;
  region->v2d.minzoom = 0.01f;
  region->v2d.maxzoom = 32.0f;
  region->v2d.keepzoom = V2D_KEEPASPECT;
  region->v2d.keeptot = 0;

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

/* add handlers, stuff you only do once or on area/region changes */
static void curve_designer_main_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  ui::view2d_region_reinit(&region->v2d, ui::V2D_COMMONVIEW_CUSTOM, region->winx, region->winy);
}

static void curve_designer_main_region_draw(const bContext *C, ARegion *region)
{
  View2D *v2d = &region->v2d;

  /* Clear with theme background. */
  ui::theme::frame_buffer_clear(TH_BACK);

  /* Set up the 2D view matrix so future curve drawing uses canvas coordinates. */
  ui::view2d_view_ortho(v2d);

  /* TODO(curve_designer): draw curves here. */

  ui::view2d_view_restore(C);
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

static void curve_designer_keymap(wmKeyConfig * /*keyconf*/) {}

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

  /* regions: main window */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer main region");
  art->regionid = RGN_TYPE_WINDOW;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES;
  art->init = curve_designer_main_region_init;
  art->draw = curve_designer_main_region_draw;
  art->listener = curve_designer_main_region_listener;

  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer header region");
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_FRAMES | ED_KEYMAP_HEADER;
  art->init = curve_designer_header_region_init;
  art->layout = ED_region_header_layout;
  art->draw = ED_region_header_draw;
  art->listener = curve_designer_header_region_listener;

  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

}  // namespace blender
