//
// Created by Andrei Ghita on 01.10.2026.
//

#ifndef BUSINESS_GAME_SCRIPT_HPP
#define BUSINESS_GAME_SCRIPT_HPP
#include <functional>
#include <string>

/**
 * A piece of in-game behaviour, run once per frame.
 *
 * Scripts are owned globally (global::scripts in main.hpp) and are not attached
 * to anything: a script that acts on a grid, a light or anything else holds its
 * own pointer to it. They run before the view tree updates, so whatever they
 * change is seen by that same frame's update and draw.
 *
 * TODO: save and load script state. That will want a write/read pair here and a
 *  loader registry keyed on get_type(), the way voxel_file::register_grid_loader()
 *  works for grids.
 */
class Script {
public:
    virtual ~Script() = default;

    /** Called once, when the script is handed to global::add_script(). */
    virtual void on_start() {}

    /** Called every frame. `delta` is the frame time in seconds, from GetFrameTime(). */
    virtual void on_update(float delta) {}

    virtual const std::string& get_type() const = 0;
};

/**
 * A script made of lambdas, for behaviour too small to deserve a class of its
 * own. Any state it needs lives in the captures, which is also why it cannot be
 * saved: give the behaviour a proper subclass once it needs to be.
 */
class LambdaScript : public Script {
public:
    using StartFn = std::function<void()>;
    using UpdateFn = std::function<void(float delta)>;

    /** `type` names the script for debugging, as there is no class name to go by. */
    explicit LambdaScript(std::string type, UpdateFn update, StartFn start = {});

    void on_start() override;
    void on_update(float delta) override;

    const std::string& get_type() const override { return type; }

private:
    std::string type;
    UpdateFn update;
    StartFn start;
};


#endif //BUSINESS_GAME_SCRIPT_HPP
