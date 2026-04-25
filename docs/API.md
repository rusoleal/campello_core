# campello_core API Reference

Concise reference for the public API. For detailed behavior, see the header files and `ARCHITECTURE.md`.

## World

```cpp
#include <campello/core/core.hpp>
```

### Entity Lifecycle

| Method | Signature | Description |
|---|---|---|
| `spawn` | `Entity spawn()` | Create empty entity |
| `spawn_with` | `Entity spawn_with(Cs&&... comps)` | Create entity with components |
| `spawn_many` | `vector<Entity> spawn_many<N>(count, Cs&&...)` | Bulk spawn |
| `despawn` | `void despawn(Entity e)` | Remove entity and all components |
| `despawn_many` | `void despawn_many(vector<Entity>)` | Batch despawn |
| `is_alive` | `bool is_alive(Entity e) const noexcept` | Check entity validity |

### Component Operations

| Method | Signature | Description |
|---|---|---|
| `insert` | `T& insert<T>(Entity e, Args&&...)` | Insert or get component |
| `insert_many` | `void insert_many<T>(vector<Entity>, T)` | Batch insert |
| `get` | `T* get<T>(Entity e)` | Get component (nullptr if missing) |
| `get<T>` (const) | `const T* get<T>(Entity e) const` | Const accessor |
| `has` | `bool has<T>(Entity e) const noexcept` | Check component presence |
| `remove` | `void remove<T>(Entity e)` | Remove component |
| `mark_changed` | `void mark_changed<T>(Entity e)` | Update changed tick |

### Queries

| Method | Signature | Description |
|---|---|---|
| `query` | `Query<Cs...> query<Cs...>()` | Build query for components |
| `query<C>().each` | `void each(Func&&)` | Iterate all matches |
| `query<C>().each_par` | `void each_par(ThreadPool&, Func&&)` | Parallel chunk iteration |
| `query<C>().count` | `size_t count() const` | Match count |
| `query<C>().empty` | `bool empty() const` | True if no matches |

### Query Filters

```cpp
world.query<Position>()
    .added<Position>()           // only entities added since last_run_tick
    .changed<Position>()         // only entities changed since last_run_tick
    .with_last_run_tick(tick);   // set comparison baseline
```

Filters return by value: chain them in one expression or store the result.

### Change Detection

| Method | Signature | Description |
|---|---|---|
| `change_tick` | `Tick change_tick() const` | Current global tick |
| `increment_change_tick` | `void increment_change_tick()` | Advance tick, clear removed |
| `removed` | `const vector<Entity>& removed<T>()` | Entities that lost T this tick |

### Hooks

| Method | Signature | Description |
|---|---|---|
| `on_add` | `void on_add<T>(function<void(Entity, T&)>)` | Callback after insert |
| `on_remove` | `void on_remove<T>(function<void(Entity, T&)>)` | Callback before remove |

### Hierarchy

| Method | Signature | Description |
|---|---|---|
| `set_parent` | `void set_parent(Entity child, Entity parent)` | Set parent, detects cycles |
| `remove_parent` | `void remove_parent(Entity child)` | Unlink from parent |
| `is_descendant_of` | `bool is_descendant_of(Entity, Entity) const` | Check ancestry |

### Resources

| Method | Signature | Description |
|---|---|---|
| `init_resource` | `void init_resource<T>(T&&)` | Create resource |
| `resource` | `Res<T> resource<T>()` | Immutable borrow |
| `resource_mut` | `ResMut<T> resource_mut<T>()` | Mutable borrow |

### Events

| Method | Signature | Description |
|---|---|---|
| `send` | `void send<T>(T&&)` | Emit event |
| `read_events` | `span<const T> read_events<T>()` | Read unprocessed events |
| `clear_events` | `void clear_events<T>()` | Clear event buffer |

### Snapshots

| Method | Signature | Description |
|---|---|---|
| `snapshot` | `string snapshot() const` | JSON serialize world state |
| `restore` | `void restore(string_view json)` | Restore from JSON |
| `clear` | `void clear()` | Remove all entities |

### Memory

| Method | Signature | Description |
|---|---|---|
| `memory_stats` | `MemoryStats memory_stats() const` | Live memory diagnostics |

```cpp
struct MemoryStats {
    size_t chunk_data_bytes;
    size_t chunk_ticks_bytes;
    size_t entity_meta_bytes;
    size_t archetype_count;
    size_t chunk_count;
    size_t alive_entities;
    size_t total_capacity;
    size_t total_bytes() const;
    double bytes_per_entity() const;
    double chunk_utilization() const;
};
```

---

## Schedule

```cpp
#include <campello/core/schedule.hpp>
```

| Method | Signature | Description |
|---|---|---|
| `add_system` | `void add_system(Func, SystemDescriptor)` | Register system |
| `add_system` | `void add_system(Func, string name)` | Register named system |
| `run` | `void run(World&)` | Execute all stages sequentially |
| `run_parallel` | `void run_parallel(World&, ThreadPool&)` | Execute with thread pool |

### SystemDescriptor

```cpp
struct SystemDescriptor {
    vector<ComponentId> reads_components;
    vector<ComponentId> writes_components;
    vector<TypeId>      reads_resources;
    vector<TypeId>      writes_resources;
    string              after_system;
    string              before_system;
};
```

---

## Component Registration

```cpp
// Specialize ComponentTraits for editor/serializer support
template<>
struct campello::core::ComponentTraits<MyComp> : ComponentTraitsBase<MyComp> {
    static constexpr std::string_view name = "MyComp";
    static void reflect(ComponentBuilder& b) {
        b.property("field", &MyComp::field);
    }
};

// Register before use
world.register_component<MyComp>();
```

---

## CommandBuffer

Deferred operations for use inside queries:

```cpp
CommandBuffer commands;
world.query<Health>().each([&](Entity e, Health& h) {
    if (h.hp <= 0) commands.despawn(e);
});
commands.apply(world);
```

| Method | Description |
|---|---|
| `commands.spawn()` | Deferred spawn |
| `commands.despawn(e)` | Deferred despawn |
| `commands.spawn_with<C...>(...)` | Deferred spawn_with |
| `commands.insert<T>(e, ...)` | Deferred insert |
| `commands.remove<T>(e)` | Deferred remove |
| `commands.apply(world)` | Execute all commands |
