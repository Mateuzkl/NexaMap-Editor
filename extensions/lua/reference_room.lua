return {
    name = "Reference Style Room",
    version = "1.0",
    author = "NexaMap",
    category = "AI Reference",
    description = "Uses captured SOURCE brush names for style, but writes only to captured TARGET.",
    target = "selection",
    parameters = {
        { name = "seed", type = "seed", default = 31 },
        { name = "decorationDensity", type = "number", min = 0, max = 1, default = 0.12 },
    },
    run = function(ctx, params)
        local source = Reference.current()
        local target = Target.current()
        if not source then error("Capture a SOURCE reference first.") end
        if not target then error("Capture a different TARGET area first.") end
        local ground, doodad
        for _, name in ipairs(source.brushes) do
            local brush = Brushes.find(name)
            if brush then
                if not ground and brush:kind() == "ground" then ground = brush end
                if not doodad and brush:kind() == "doodad" then doodad = brush end
            end
        end
        if not ground then error("SOURCE has no usable ground brush in current resources.") end
        local rng = Random.new(params.seed)
        for pos in target:positions() do
            ground:apply(pos)
            if doodad and rng:next() < params.decorationDensity then doodad:apply(pos) end
        end
    end,
}
