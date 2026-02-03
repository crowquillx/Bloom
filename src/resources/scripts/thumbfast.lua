-- thumbfast.lua
-- Thumbfast-compatible shim for Bloom media player
-- Based on jellyfin-mpv-shim's thumbfast.lua implementation
-- https://github.com/jellyfin/jellyfin-mpv-shim/blob/master/jellyfin_mpv_shim/thumbfast.lua
--
-- This script receives pre-processed trickplay data from Bloom via script-message
-- and exposes the standard thumbfast API (thumbfast-info, thumb, clear)
-- so any thumbfast-compatible OSC (like ModernX) works.
--
-- The key difference from the original thumbfast:
-- - Bloom pre-processes all trickplay tiles into a single raw BGRA binary file
-- - This script calculates byte offsets to display individual frames
-- - Uses mpv's overlay-add with file offset for direct display
--
-- License: MPL-2.0 (compatible with thumbfast)

local utils = require 'mp.utils'
local msg = require 'mp.msg'

-- Trickplay state (compatible with jellyfin-mpv-shim's thumbfast.lua)
local img_count = 0          -- Total number of thumbnail frames
local img_multiplier = 0     -- Interval between frames in milliseconds
local img_width = 0          -- Width of each thumbnail
local img_height = 0         -- Height of each thumbnail
local img_file = ""          -- Path to the raw BGRA binary file
local img_last_frame = -1    -- Last rendered frame index (for deduplication)
local img_is_shown = false   -- Whether overlay is currently visible
local img_enabled = false    -- Whether trickplay is available
local img_overlay_id = 46    -- Overlay ID (same as thumbfast default)

-- Send thumbfast-info message to compatible OSCs (e.g., ModernX)
local function send_thumbfast_info()
    local json, err = utils.format_json({
        width = img_width,
        height = img_height,
        disabled = not img_enabled,
        available = img_enabled,
        overlay_id = img_overlay_id,
    })
    
    if err then
        msg.error("Failed to format thumbfast-info JSON: " .. err)
    else
        mp.commandv("script-message", "thumbfast-info", json)
        msg.debug("Sent thumbfast-info: " .. json)
    end
end

-- Handle client-message events
-- This is the main message handler for both Bloom messages and thumbfast API
local function client_message_handler(event)
    local args = event.args
    if not args or #args < 1 then return end
    
    local event_name = args[1]
    
    if event_name == "shim-trickplay-clear" then
        -- Clear trickplay data (sent by Bloom when changing media)
        msg.info("Clearing trickplay data")
        img_enabled = false
        img_count = 0
        img_multiplier = 0
        img_width = 0
        img_height = 0
        img_file = ""
        img_last_frame = -1
        
        if img_is_shown then
            mp.commandv("overlay-remove", img_overlay_id)
            img_is_shown = false
        end
        
        send_thumbfast_info()
        
    elseif event_name == "shim-trickplay-bif" then
        -- Receive trickplay binary file info from Bloom
        -- Format: shim-trickplay-bif <count> <interval_ms> <width> <height> <file_path>
        msg.info("Received trickplay BIF data")
        
        img_count = tonumber(args[2]) or 0
        img_multiplier = tonumber(args[3]) or 0
        img_width = tonumber(args[4]) or 0
        img_height = tonumber(args[5]) or 0
        img_file = args[6] or ""
        img_last_frame = -1
        
        -- Validate file path exists
        if img_file == "" then
            msg.warn("Trickplay file path is empty")
            img_enabled = false
        else
            img_enabled = img_count > 0 and img_width > 0 and img_height > 0
        end
        
        msg.info(string.format("Trickplay enabled: %s, count=%d, interval=%dms, size=%dx%d, file=%s",
            tostring(img_enabled), img_count, img_multiplier, img_width, img_height, img_file))
        
        send_thumbfast_info()
        
    elseif event_name == "thumb" then
        -- Thumb request from OSC (thumbfast API)
        -- Format: thumb <time_seconds> <x> <y>
        local offset_seconds = tonumber(args[2])
        local x = tonumber(args[3])
        local y = tonumber(args[4])
        
        if not offset_seconds or not x or not y then
            return
        end
        
        if not img_enabled then
            return
        end
        
        -- Calculate which frame to show
        -- Frame index = time_seconds / (interval_ms / 1000)
        local frame = 0
        if img_multiplier > 0 then
            frame = math.floor(offset_seconds / (img_multiplier / 1000))
        end
        
        -- Clamp to valid range
        if frame >= img_count then
            frame = img_count - 1
        end
        if frame < 0 then
            frame = 0
        end
        
        -- Only render if frame changed (optimization)
        if frame ~= img_last_frame then
            img_last_frame = frame
            
            -- Calculate byte offset in the binary file
            -- Each frame is width * height * 4 bytes (BGRA)
            local offset = frame * img_width * img_height * 4
            local stride = img_width * 4
            
            -- Display the thumbnail using overlay-add
            -- overlay-add <id> <x> <y> <file> <offset> <fmt> <w> <h> <stride>
            mp.commandv("overlay-add", img_overlay_id, x, y, img_file, offset, "bgra", img_width, img_height, stride)
            img_is_shown = true
            
            msg.debug(string.format("Showing frame %d at (%d, %d), offset=%d", frame, x, y, offset))
        end
        
    elseif event_name == "clear" then
        -- Clear request from OSC (thumbfast API)
        if img_is_shown then
            mp.commandv("overlay-remove", img_overlay_id)
            img_is_shown = false
            img_last_frame = -1
        end
    end
end

-- Register event handler
mp.register_event("client-message", client_message_handler)

-- Also register script-message handlers for direct calls
-- These allow script-message-to thumbfast thumb <time> <x> <y>
mp.register_script_message("thumb", function(time_seconds, x, y)
    if not img_enabled then return end
    
    local offset_seconds = tonumber(time_seconds)
    x = tonumber(x)
    y = tonumber(y)
    
    if not offset_seconds or not x or not y then return end
    
    local frame = 0
    if img_multiplier > 0 then
        frame = math.floor(offset_seconds / (img_multiplier / 1000))
    end
    
    if frame >= img_count then frame = img_count - 1 end
    if frame < 0 then frame = 0 end
    
    if frame ~= img_last_frame then
        img_last_frame = frame
        local offset = frame * img_width * img_height * 4
        local stride = img_width * 4
        mp.commandv("overlay-add", img_overlay_id, x, y, img_file, offset, "bgra", img_width, img_height, stride)
        img_is_shown = true
    end
end)

mp.register_script_message("clear", function()
    if img_is_shown then
        mp.commandv("overlay-remove", img_overlay_id)
        img_is_shown = false
        img_last_frame = -1
    end
end)

-- Send initial thumbfast-info (disabled, no trickplay data yet)
send_thumbfast_info()

msg.info("thumbfast.lua loaded (Bloom jellyfin-mpv-shim style)")
