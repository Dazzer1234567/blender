# SPDX-FileCopyrightText: 2026 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

import bpy


class CURVE_DESIGNER_HT_header(bpy.types.Header):
    bl_space_type = 'CURVE_DESIGNER'

    def draw(self, context):
        layout = self.layout

        # Standard editor-type switcher + area split/join controls. Without
        # this the user can switch INTO the editor but has no way to switch
        # back out.
        layout.template_header()

        CURVE_DESIGNER_MT_editor_menus.draw_collapsible(context, layout)

        layout.separator_spacer()

        layout.label(text="Curve Designer (placeholder)")


class CURVE_DESIGNER_MT_editor_menus(bpy.types.Menu):
    bl_idname = "CURVE_DESIGNER_MT_editor_menus"
    bl_label = ""

    def draw(self, _context):
        layout = self.layout
        layout.menu("CURVE_DESIGNER_MT_view")


class CURVE_DESIGNER_MT_view(bpy.types.Menu):
    bl_label = "View"

    def draw(self, _context):
        layout = self.layout
        layout.menu("INFO_MT_area")


classes = (
    CURVE_DESIGNER_HT_header,
    CURVE_DESIGNER_MT_editor_menus,
    CURVE_DESIGNER_MT_view,
)


if __name__ == "__main__":  # Only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
