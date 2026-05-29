#include "sol/scene/actor.h"
#include "sol/engine.h"

namespace sol {

void Actor::on_ready  (Engine& engine)           { begin_play(engine); }
void Actor::on_update (Engine& engine, float dt) { tick(engine, dt); }
void Actor::on_destroy(Engine& engine)           { end_play(engine); }

} // namespace sol
