#pragma once

#include "types.hpp"
#include "component.hpp"
#include <memory>
#include <vector>

namespace campello::core {

class World;

// ------------------------------------------------------------------
// CommandBuffer: deferred entity mutations
// ------------------------------------------------------------------
class CommandBuffer {
public:
    CommandBuffer() = default;

    // Spawn an empty entity (deferred)
    void spawn();

    // Spawn an entity with components (deferred)
    template<typename... Cs>
    void spawn_with(Cs&&... components);

    // Despawn entity (deferred)
    void despawn(Entity entity);

    // Insert component (deferred)
    template<component T, typename... Args>
    void insert(Entity entity, Args&&... args);

    // Remove component (deferred)
    template<component T>
    void remove(Entity entity);

    // Apply all deferred commands to the world
    void apply(World& world);

    bool empty() const noexcept { return commands_.empty(); }
    void clear() { commands_.clear(); }

private:
    struct CommandBase {
        virtual void execute(World& world) = 0;
        virtual ~CommandBase() = default;
    };

    template<typename F>
    struct CommandImpl : CommandBase {
        F f;
        explicit CommandImpl(F&& func) : f(std::move(func)) {}
        void execute(World& world) override { f(world); }
    };

    std::vector<std::unique_ptr<CommandBase>> commands_;

    template<typename F>
    void push(F&& f) {
        commands_.push_back(std::make_unique<CommandImpl<F>>(std::forward<F>(f)));
    }
};

// Alias for systems
using Commands = CommandBuffer;

} // namespace campello::core
