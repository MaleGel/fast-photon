-- Warrior unit script.
--
-- Demonstrates the Step 3 binding surface:
--   self.transform / self.sprite — component handles
--   self:faction()               — owner faction as "Player" / "Enemy" / "Neutral"
--   log.info / .warn / .error    — engine logger
--   audio.play(id)               — fire a sound by qualified id
--   vfx.spawn(id, x, y, count?)  — fire a one-shot particle burst
--   event.emit(name, payload)    — broadcast a custom event to every script
--   on_init / on_update          — lifecycle hooks
--   on_turn_start / on_turn_end  — fired by TurnManager
--   on_event                     — generic catch-all for event.emit

local spawn_x, spawn_y, spawn_z = 0, 0, 0
local time_alive = 0.0

function on_init(self)
    spawn_x, spawn_y, spawn_z = self.transform:position()
    log.info("warrior spawned at", spawn_x, spawn_y, "faction =", self:faction())
end

function on_update(self, dt)
    time_alive = time_alive + dt
    -- Idle bob: cosmetic vertical oscillation around the spawn position.
    local bob = math.sin(time_alive * 4.0) * 0.05
    self.transform:set_position(spawn_x, spawn_y + bob, spawn_z)
end

function on_turn_start(self, faction_name)
    if faction_name == self:faction() then
        log.info("warrior: my turn started")
        -- Demo: emit a custom event when our turn begins. Receivers
        -- (including this script's on_event below) will see it.
        event.emit("warrior_ready", { unit = self })
        -- Fire-and-forget VFX burst at our position. The persistent
        -- ParticleEmitter on the prefab keeps trickling sparks; this
        -- one-shot just adds a louder pop on turn start.
        local x, y = self.transform:position()
        vfx.spawn("player/spark", x, y, 24)
    end
end

function on_turn_end(self, ended, next_)
    if ended == self:faction() then
        log.info("warrior: my turn ended, next is", next_)
    end
end

function on_event(self, name, payload)
    -- Filter by name. Echoing here mostly proves the round-trip works;
    -- real scripts would react to gameplay-meaningful events only.
    if name == "warrior_ready" then
        log.info("warrior heard 'warrior_ready' event")
    end
end
