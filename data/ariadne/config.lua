-- Ariadne configuration.
--
-- Copy this file to ~/.config/tde/ariadne/config.lua and change what you like. Every
-- setting is optional: anything left out keeps the default shown here. Desktop-wide
-- settings (theme, window buttons, corner radius) are in ~/.config/tde/config.lua.
--
-- Ariadne remembers changes made in its windows (view, sorting, zoom, window size) in
-- state.lua next to this file. Whenever you edit this file, its settings win again.
-- Changes apply as soon as the file is saved; no restart needed.

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
        expandable_folders = true, -- folders in the list view open in place with their arrow
        archives_as_folders = true, -- opening a zip, tarball, … shows what is in it (read-only);
                                    -- false opens it with its application
    },

    -- Icons to use for MIME types instead of the icon theme's. Windows programs and
    -- AppImages already get the generic executable icon.
    -- icons = {
    --     ["application/x-shellscript"] = "application-x-executable",
    -- },

    -- Your own commands in the context menu. Each needs a name and a command: a string,
    -- split like a command line ("double quotes" group), or a list of arguments. In the
    -- command, %f is the first selected file, %F all of them, %u and %U the same as URIs,
    -- %d the folder shown, and %% a percent sign.
    --
    --   types      MIME types the selected items must have; "image/*" matches all
    --              images, "inode/directory" folders. Left out: anything.
    --   selection  "any" (one or more items, the default), "single", "multiple", or
    --              "none" for the menu of a folder's empty space.
    --   icon       an icon name from the icon theme, or a path to an image.
    --   shortcut   keys that run it on the selection, like "Ctrl+Alt+P".
    --   terminal   true to run it in a terminal window.
    --
    -- actions = {
    --     { name = "Open in VS Code", command = "code %F", types = { "text/*", "inode/directory" } },
    --     { name = "Convert to PNG", command = { "magick", "%f", "%f.png" },
    --       types = "image/*", selection = "single", icon = "image-x-generic" },
    --     { name = "Git Status", command = "git status", selection = "none", terminal = true },
    -- },
}
