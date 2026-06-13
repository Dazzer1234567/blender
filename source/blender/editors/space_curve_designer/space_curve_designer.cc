/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup spcurvedesigner
 *
 * Curve Designer editor.
 *
 * The main region is a full 3D viewport reusing space_view3d's drawing and
 * region-init pipelines. The space owns its own View3D (camera/lens/clip
 * settings) and each window region owns its own RegionView3D (orbit state),
 * matching the layout the 3D Viewport uses. CTX_wm_view3d() has been taught
 * to return our space's View3D when called from inside this editor.
 */

#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_string_utf8.h"
#include "BLI_utildefines.h"

#include "BKE_context.hh"
#include "BKE_screen.hh"

#include "DNA_view3d_types.h"

#include "ED_screen.hh"
#include "ED_space_api.hh"
#include "ED_view3d.hh"

#include "UI_interface_c.hh"
#include "UI_resources.hh"

#include "BLO_read_write.hh"

#include "WM_api.hh"
#include "WM_types.hh"

namespace blender {

/* ******************** default callbacks for curve designer space ***************** */

static SpaceLink *curve_designer_create(const ScrArea * /*area*/, const Scene *scene)
{
  SpaceCurveDesigner *scd = MEM_new<SpaceCurveDesigner>("init curve designer");
  /* SpaceCurveDesigner's first member IS the View3D — the spacetype field
   * we set here is the SpaceLink header's spacetype, shared with View3D's
   * own header. So this also makes (View3D *)scd cast safely. */
  scd->v3d_base.spacetype = SPACE_CURVE_DESIGNER;

  View3D *v3d = &scd->v3d_base;
  if (scene) {
    v3d->camera = scene->camera;
  }
  /* Defaults that startup.blend sets for fresh View3Ds but our explicit
   * allocation doesn't pick up: enable all transform gizmos and the per-
   * object-type gizmos so the toolbar's Move/Rotate/Scale handles show. */
  v3d->gizmo_show_object = V3D_GIZMO_SHOW_OBJECT_TRANSLATE | V3D_GIZMO_SHOW_OBJECT_ROTATE |
                            V3D_GIZMO_SHOW_OBJECT_SCALE;
  v3d->gizmo_show_empty = V3D_GIZMO_SHOW_EMPTY_IMAGE | V3D_GIZMO_SHOW_EMPTY_FORCE_FIELD;
  v3d->gizmo_show_light = V3D_GIZMO_SHOW_LIGHT_SIZE | V3D_GIZMO_SHOW_LIGHT_LOOK_AT;
  v3d->gizmo_show_camera = V3D_GIZMO_SHOW_CAMERA_LENS | V3D_GIZMO_SHOW_CAMERA_DOF_DIST;

  /* header */
  ARegion *region = BKE_area_region_new();
  BLI_addtail(&v3d->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = (U.uiflag & USER_HEADER_BOTTOM) ? RGN_ALIGN_BOTTOM : RGN_ALIGN_TOP;

  /* tool shelf (T) */
  region = BKE_area_region_new();
  BLI_addtail(&v3d->regionbase, region);
  region->regiontype = RGN_TYPE_TOOLS;
  region->alignment = RGN_ALIGN_LEFT;
  region->flag = RGN_FLAG_HIDDEN;

  /* sidebar / properties (N) */
  region = BKE_area_region_new();
  BLI_addtail(&v3d->regionbase, region);
  region->regiontype = RGN_TYPE_UI;
  region->alignment = RGN_ALIGN_RIGHT;
  region->flag = RGN_FLAG_HIDDEN;

  /* main region (3D viewport) */
  region = BKE_area_region_new();
  BLI_addtail(&v3d->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  /* Per-region 3D view state (orbit, distance, persp). Defaults match
   * what space_view3d sets up in view3d_create(). */
  RegionView3D *rv3d = MEM_new<RegionView3D>("curve designer region view3d");
  rv3d->viewquat[0] = 1.0f;
  rv3d->persp = RV3D_PERSP;
  rv3d->view = RV3D_VIEW_USER;
  rv3d->dist = 10.0f;
  region->regiondata = rv3d;

  return reinterpret_cast<SpaceLink *>(scd);
}

/* Doesn't free the space-link itself. */
static void curve_designer_free(SpaceLink *sl)
{
  SpaceCurveDesigner *scd = reinterpret_cast<SpaceCurveDesigner *>(sl);
  View3D *v3d = &scd->v3d_base;
  /* Mirror view3d_free()'s essential cleanup. */
  if (v3d->localvd) {
    MEM_delete(v3d->localvd);
    v3d->localvd = nullptr;
  }
  if (v3d->runtime.properties_storage_free) {
    v3d->runtime.properties_storage_free(v3d->runtime.properties_storage);
    v3d->runtime.properties_storage_free = nullptr;
  }
}

/* spacetype; init callback */
static void curve_designer_init(wmWindowManager * /*wm*/, ScrArea *area)
{
  /* Migration for areas saved before curve_designer_create() seeded the
   * gizmo show flags. If they're all zero, populate sensible defaults so
   * existing layouts still show transform handles. */
  SpaceCurveDesigner *scd = static_cast<SpaceCurveDesigner *>(area->spacedata.first);
  if (scd && scd->v3d_base.gizmo_show_object == 0 && scd->v3d_base.gizmo_show_empty == 0 &&
      scd->v3d_base.gizmo_show_light == 0 && scd->v3d_base.gizmo_show_camera == 0)
  {
    scd->v3d_base.gizmo_show_object = V3D_GIZMO_SHOW_OBJECT_TRANSLATE |
                                       V3D_GIZMO_SHOW_OBJECT_ROTATE | V3D_GIZMO_SHOW_OBJECT_SCALE;
    scd->v3d_base.gizmo_show_empty = V3D_GIZMO_SHOW_EMPTY_IMAGE | V3D_GIZMO_SHOW_EMPTY_FORCE_FIELD;
    scd->v3d_base.gizmo_show_light = V3D_GIZMO_SHOW_LIGHT_SIZE | V3D_GIZMO_SHOW_LIGHT_LOOK_AT;
    scd->v3d_base.gizmo_show_camera = V3D_GIZMO_SHOW_CAMERA_LENS | V3D_GIZMO_SHOW_CAMERA_DOF_DIST;
  }
}

static SpaceLink *curve_designer_duplicate(SpaceLink *sl)
{
  SpaceCurveDesigner *scd_old = reinterpret_cast<SpaceCurveDesigner *>(sl);
  SpaceCurveDesigner *scd_new = MEM_dupalloc(scd_old);

  /* v3d_base is inline, copied by MEM_dupalloc above. Clear the nested
   * localvd pointer so the duplicate doesn't share it with the original
   * (free would otherwise double-free). */
  scd_new->v3d_base.localvd = nullptr;

  return reinterpret_cast<SpaceLink *>(scd_new);
}

/* Header region callbacks — same as the empty placeholder. */
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

static void curve_designer_operatortypes() {}

static void curve_designer_keymap(wmKeyConfig * /*keyconf*/) {}

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

  /* regions: main window — reuse the 3D Viewport's drawing pipeline. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer main region");
  art->regionid = RGN_TYPE_WINDOW;
  /* Match what space_view3d sets for its main region. */
  art->keymapflag = ED_KEYMAP_GIZMO | ED_KEYMAP_TOOL | ED_KEYMAP_GPENCIL;
  art->init = view3d_main_region_init;
  art->draw = view3d_main_region_draw;
  art->listener = view3d_main_region_listener;

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

  /* regions: buttons/list view (N panel) — reuse view3d's. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer buttons region");
  art->regionid = RGN_TYPE_UI;
  art->prefsizex = UI_SIDEBAR_PANEL_WIDTH;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = view3d_buttons_region_listener;
  art->init = view3d_buttons_region_init;
  art->layout = view3d_buttons_region_layout;
  art->draw = ED_region_panels_draw;
  art->snap_size = ED_region_generic_panel_region_snap_size;
  BLI_addhead(&st->regiontypes, art);
  view3d_buttons_register(art);

  /* regions: tool(bar) — reuse view3d's. */
  art = MEM_new_zeroed<ARegionType>("spacetype curve designer tools region");
  art->regionid = RGN_TYPE_TOOLS;
  art->prefsizex = int(UI_TOOLBAR_WIDTH);
  art->prefsizey = 50;
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_FRAMES;
  art->listener = view3d_buttons_region_listener;
  art->init = view3d_tools_region_init;
  art->draw = view3d_tools_region_draw;
  art->snap_size = ED_region_generic_tools_region_snap_size;
  BLI_addhead(&st->regiontypes, art);

  BKE_spacetype_register(std::move(st));
}

}  // namespace blender
