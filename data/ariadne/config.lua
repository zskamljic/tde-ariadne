-- Ariadne configuration.
--
-- Copy this file to ~/.config/tde/ariadne/config.lua and change what you like. Every
-- setting is optional: anything left out keeps the default shown here. Desktop-wide
-- settings (theme, window buttons, corner radius) are in ~/.config/tde/config.lua.
--
-- Ariadne remembers changes made in its windows (view, sorting, zoom, window size) in
-- state.lua next to this file. Whenever you edit this file, its settings win again.

return {
    view = {
        mode = "grid",          -- "grid" or "list"
        sort = "name",          -- "name", "modified", "size" or "type"
        descending = false,
        folders_first = true,   -- false sorts folders and files together
        case_sensitive = false, -- true sorts uppercase names before lowercase ones, like ls
        show_hidden = false,
        grid_icon_size = 64,    -- 32 to 256
        list_icon_size = 24,    -- 16 to 64
    },

    -- Icons to use for MIME types instead of the icon theme's. Windows programs and
    -- AppImages already get the generic executable icon.
    -- icons = {
    --     ["application/x-shellscript"] = "application-x-executable",
    -- },
}
