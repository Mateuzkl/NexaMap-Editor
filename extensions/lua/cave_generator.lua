return {
    name = "Cave Generator",
    version = "1.0",
    author = "NexaMap",
    category = "Procedural",
    description = "Cellular cave using active ground and wall brushes.",
    target = "selection",
    parameters = {
        { name = "seed", type = "seed", default = 8123 },
        { name = "fillChance", type = "number", min = 0, max = 1, default = 0.45 },
        { name = "iterations", type = "integer", min = 0, max = 8, default = 5 },
        { name = "groundBrush", type = "brush", kind = "ground", default = "" },
        { name = "wallBrush", type = "brush", kind = "wall", default = "" },
    },
    run = function(ctx, params)
        local ground = Brushes.find(params.groundBrush)
        local wall = Brushes.find(params.wallBrush)
        if not ground or ground:kind() ~= "ground" then error("Choose a valid cave ground brush.") end
        if not wall or wall:kind() ~= "wall" then error("Choose a valid wall brush.") end
        local cave = Algo.cellularAutomata({area = ctx.selection, seed = params.seed,
            fillChance = params.fillChance, iterations = params.iterations})
        for pos in ctx.selection:positions() do
            ground:apply(pos)
            if not cave:isFloor(pos) then wall:apply(pos) end
        end
    end,
}
