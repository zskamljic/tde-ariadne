-- TDE desktop configuration, shared by all TDE applications.
--
-- Copy this file to ~/.config/tde/config.lua and change what you like. Every setting is
-- optional: anything left out keeps the default shown here. Application specific
-- settings live next to it, in ~/.config/tde/<application>/config.lua.
--
-- Installed copies of the examples are in /usr/share/doc/ariadne/examples/tde.

return {
    window_buttons = {
        -- Which end of the header bar holds the window buttons: "left" or "right".
        position = "right",
        -- The buttons to show, in order: any of "minimize", "maximize" and "close".
        order = { "minimize", "maximize", "close" },
    },

    -- Terminal to open ("Open in Terminal", terminal applications). By default $TERMINAL,
    -- else the first one installed of the usual ones.
    -- terminal = "kitty",

    appearance = {
        theme = "arc-dark",     -- "arc-dark", "arc" or "system" (follows the light/dark preference)

        -- Roundness of buttons, entries, selections and popups, in pixels. 0 makes them square.
        corner_radius = 5,      -- 0 to 24

        -- Icon theme; by default the one the rest of the desktop uses.
        -- icon_theme = "Papirus-Dark",

        -- Override single theme colours. Names: window, base, header, sidebar, sidebar_text,
        -- text, dim_text, accent, accent_text, border, hover, pressed, entry, scrollbar,
        -- close_hover, error. Values are "#rrggbb" or "#aarrggbb".
        -- colors = {
        --     accent = "#5294e2",
        -- },
    },
}
