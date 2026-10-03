-- Bundled example. Preview first; all writes stay inside the captured target.
return {
    name = "Forest Generator",
    version = "1.0",
    author = "NexaMap",
    category = "Procedural",
    description = "Paints a ground brush and sparse decoration inside the exact target mask.",
    target = "selection",
    parameters = {
        { name = "seed", type = "seed", default = 12345 },
        { name = "density", type = "number", min = 0, max = 1, default = 0.18 },
        { name = "groundBrush", type = "brush", kind = "ground", default = "" },
        { name = "doodadBrush", type = "brush", kind = "doodad", default = "" },
    },
    run = function(ctx, params)
        local ground = Brushes.find(params.groundBrush)
        if not ground or ground:kind() ~= "ground" then
            error("Choose a valid ground brush from the active resources.")
        end
        local doodad = params.doodadBrush ~= "" and Brushes.find(params.doodadBrush) or nil
        if doodad and doodad:kind() ~= "doodad" then error("Decoration must be a doodad brush.") end
        local rng = Random.new(params.seed)
        for pos in ctx.selection:positions() do
            ground:apply(pos)
            if doodad and rng:next() < params.density then doodad:apply(pos) end
        end
    end,
}
