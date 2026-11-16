-- Gameplay-only physics settings. Editor Stop restores the authored Scene.
-- Speeds are world units/second; collider sizes are nonnegative half extents.
function on_create(e)
    assert(scene.set_collider(e:id(), 0.5, 0.5, 0.5, false, 0.25, 0))
    assert(scene.set_rigid_body(e:id(), 1, 0, 0, 1, true, true))
end

function on_fixed_update(e, dt)
    local vx, vy, vz, mass, dynamic, gravity, grounded = scene.rigid_body(e:id())
    if grounded then
        -- Existing velocity commands preserve mass/dynamic/gravity settings.
        scene.set_velocity(e:id(), 1, vy, vz)
    end
end
