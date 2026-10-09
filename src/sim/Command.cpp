//
// Created by Andrei Ghita on 04.10.2026.
//

#include "sim/Command.hpp"

#include "sim/Simulation.hpp"

namespace sim {

namespace {

// A route with more points than this in a command is taken to be corruption
constexpr uint32_t MAX_ROUTE_POINTS = 1u << 16;

void write_point(ByteWriter& out, const Point& p) {
    out.write_fixed(p.x);
    out.write_fixed(p.y);
    out.write_fixed(p.z);
}

bool read_point(ByteReader& in, Point* p) {
    return in.read_fixed(&p->x) && in.read_fixed(&p->y) && in.read_fixed(&p->z);
}

template <typename Tag>
void write_handle(ByteWriter& out, const Handle<Tag> h) {
    out.write_u32(h.index);
    out.write_u32(h.generation);
}

template <typename Tag>
bool read_handle(ByteReader& in, Handle<Tag>* h) {
    return in.read_u32(&h->index) && in.read_u32(&h->generation);
}

bool valid_asset(const AssetId& model) {
    return !model.empty() && model.size() <= MAX_ASSET_ID_LENGTH;
}

// The payload of one type, read out of a reader that holds that payload and
// nothing else. Nothing when it is short or the type is unknown.
std::unique_ptr<Command> read_payload(const CommandType type, ByteReader& in) {
    switch (type) {
        case CommandType::AddRoute: {
            uint32_t count = 0;
            if (!in.read_u32(&count) || count > MAX_ROUTE_POINTS) return nullptr;
            std::vector<Point> points(count);
            for (Point& p : points) {
                if (!read_point(in, &p)) return nullptr;
            }
            return std::make_unique<AddRoute>(std::move(points));
        }
        case CommandType::SpawnVehicle: {
            RouteId route;
            Fixed distance, speed;
            AssetId model;
            if (!read_handle(in, &route) || !in.read_fixed(&distance) || !in.read_fixed(&speed)
                || !in.read_string(&model, MAX_ASSET_ID_LENGTH)) return nullptr;
            return std::make_unique<SpawnVehicle>(route, distance, speed, std::move(model));
        }
        case CommandType::DespawnVehicle: {
            VehicleId vehicle;
            if (!read_handle(in, &vehicle)) return nullptr;
            return std::make_unique<DespawnVehicle>(vehicle);
        }
        case CommandType::SetVehicleSpeed: {
            VehicleId vehicle;
            Fixed speed;
            if (!read_handle(in, &vehicle) || !in.read_fixed(&speed)) return nullptr;
            return std::make_unique<SetVehicleSpeed>(vehicle, speed);
        }
    }
    return nullptr;
}

} // namespace

// --- apply ---

RejectReason AddRoute::apply(World& world) const {
    const std::optional<RouteId> id = world.routes.add(points);
    if (!id) return RejectReason::InvalidRoute;
    world.emit(RouteAdded{*id});
    return RejectReason::None;
}

RejectReason SpawnVehicle::apply(World& world) const {
    const Route* r = world.routes.get(route);
    if (r == nullptr) return RejectReason::UnknownRoute;
    if (!valid_asset(model)) return RejectReason::InvalidAsset;

    const VehicleId id = world.vehicles.add(Vehicle{route, wrap(distance, r->length()), speed, model});
    world.emit(VehicleSpawned{id});
    return RejectReason::None;
}

RejectReason DespawnVehicle::apply(World& world) const {
    if (!world.vehicles.remove(vehicle)) return RejectReason::UnknownVehicle;
    world.emit(VehicleDespawned{vehicle});
    return RejectReason::None;
}

RejectReason SetVehicleSpeed::apply(World& world) const {
    Vehicle* v = world.vehicles.get(vehicle);
    if (v == nullptr) return RejectReason::UnknownVehicle;
    v->speed = speed;
    return RejectReason::None;
}

// --- payloads ---

void AddRoute::write_payload(ByteWriter& out) const {
    out.write_u32(static_cast<uint32_t>(points.size()));
    for (const Point& p : points) write_point(out, p);
}

void SpawnVehicle::write_payload(ByteWriter& out) const {
    write_handle(out, route);
    out.write_fixed(distance);
    out.write_fixed(speed);
    out.write_string(model);
}

void DespawnVehicle::write_payload(ByteWriter& out) const {
    write_handle(out, vehicle);
}

void SetVehicleSpeed::write_payload(ByteWriter& out) const {
    write_handle(out, vehicle);
    out.write_fixed(speed);
}

// --- stamped commands ---

StampedCommand StampedCommand::clone() const {
    return StampedCommand{tick, player, sequence, command ? command->clone() : nullptr};
}

void write_command(ByteWriter& out, const StampedCommand& command) {
    out.write_u64(command.tick);
    out.write_u32(command.player);
    out.write_u32(command.sequence);

    // Written on its own first, so its length can go in front of it
    ByteWriter payload;
    if (command.command) command.command->write_payload(payload);
    out.write_u16(command.command ? static_cast<uint16_t>(command.command->type()) : 0);
    out.write_u32(static_cast<uint32_t>(payload.data().size()));
    out.write_bytes(payload.data());
}

std::optional<StampedCommand> read_command(ByteReader& in) {
    StampedCommand c;
    uint16_t type = 0;
    uint32_t payload_bytes = 0;
    std::span<const uint8_t> payload;
    if (!in.read_u64(&c.tick) || !in.read_u32(&c.player) || !in.read_u32(&c.sequence)
        || !in.read_u16(&type) || !in.read_u32(&payload_bytes)
        || !in.read_bytes(&payload, payload_bytes)) return std::nullopt;

    ByteReader payload_in(payload);
    c.command = read_payload(static_cast<CommandType>(type), payload_in);
    // A payload with bytes left over is as wrong as a short one
    if (c.command == nullptr || !payload_in.at_end()) return std::nullopt;
    return c;
}

std::vector<uint8_t> write_command_log(const std::span<const StampedCommand> commands) {
    ByteWriter out;
    out.write_u32(static_cast<uint32_t>(commands.size()));
    for (const StampedCommand& c : commands) write_command(out, c);
    return out.data();
}

std::optional<std::vector<StampedCommand>> read_command_log(const std::span<const uint8_t> bytes) {
    ByteReader in(bytes);
    uint32_t count = 0;
    if (!in.read_u32(&count)) return std::nullopt;

    std::vector<StampedCommand> commands;
    for (uint32_t i = 0; i < count; ++i) {
        std::optional<StampedCommand> c = read_command(in);
        if (!c) return std::nullopt;
        commands.push_back(std::move(*c));
    }
    if (!in.at_end()) return std::nullopt;
    return commands;
}

// --- the queue ---

uint32_t CommandQueue::submit(std::unique_ptr<Command> command, const uint32_t player) {
    const uint32_t sequence = next_sequence++;
    queued.push_back(StampedCommand{0, player, sequence, std::move(command)});
    return sequence;
}

void CommandQueue::write(ByteWriter& out) const {
    out.write_u32(next_sequence);
    out.write_u32(static_cast<uint32_t>(queued.size()));
    // The tick in each is meaningless until take() stamps it, but written
    // anyway, so a queued command has the same layout as one in a log
    for (const StampedCommand& c : queued) write_command(out, c);
}

bool CommandQueue::read(ByteReader& in) {
    queued.clear();
    next_sequence = 0;

    uint32_t sequence = 0;
    uint32_t count = 0;
    if (!in.read_u32(&sequence) || !in.read_u32(&count)) return false;

    std::vector<StampedCommand> commands;
    for (uint32_t i = 0; i < count; ++i) {
        std::optional<StampedCommand> c = read_command(in);
        if (!c) return false;
        commands.push_back(std::move(*c));
    }
    queued = std::move(commands);
    next_sequence = sequence;
    return true;
}

std::vector<StampedCommand> CommandQueue::take(const uint64_t tick) {
    std::vector<StampedCommand> out = std::move(queued);
    queued.clear();
    for (StampedCommand& c : out) c.tick = tick;
    return out;
}

} // namespace sim
