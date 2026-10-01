//
// Created by Andrei Ghita on 01.10.2026.
//

#include "Script.hpp"

#include <utility>

LambdaScript::LambdaScript(std::string type, UpdateFn update, StartFn start)
    : type(std::move(type)), update(std::move(update)), start(std::move(start))
{}

// Either lambda may be left empty, and calling an empty std::function throws.
void LambdaScript::on_start() {
    if (start) start();
}

void LambdaScript::on_update(float delta) {
    if (update) update(delta);
}
