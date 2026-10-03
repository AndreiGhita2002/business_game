//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_COMMAND_HPP
#define BUSINESS_GAME_SIM_COMMAND_HPP
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "sim/Event.hpp"
#include "sim/Fixed.hpp"
#include "sim/Handle.hpp"
#include "sim/Routes.hpp"
#include "sim/Serial.hpp"
#include "sim/Vehicles.hpp"

namespace sim {

class World;

/**
 * Every kind of command, as written to bytes. The numbers are the wire format,
 * so a kind keeps its number forever and a new one takes the next free one.
 */
enum class CommandType : uint16_t {
    AddRoute = 1,
    SpawnVehicle = 2,
    DespawnVehicle = 3,
    SetVehicleSpeed = 4,
};

/**
 * Something a player asks the simulation to do. The only way anything outside
 * the simulation changes it, in single player as much as in a networked game.
 *
 * A command is a request, not a change: apply() runs inside Simulation::step()
 * at the tick it was stamped for, checks that it still makes sense (the
 * vehicle it names may be gone by then) and either changes the world or
 * refuses. A refusal is deterministic and comes back as a CommandRejected
 * event.
 *
 * Adding one: a subclass with its data as public members, a CommandType, the
 * four overrides, and a case in read_command() in Command.cpp.
 */
class Command {
public:
    virtual ~Command() = default;

    virtual CommandType type() const = 0;

    /** Changes the world, or says why it would not. */
    virtual RejectReason apply(World& world) const = 0;

    /** The command's own data. The type and the stamp are written around it. */
    virtual void write_payload(ByteWriter& out) const = 0;

    virtual std::unique_ptr<Command> clone() const = 0;
};

/** A new route, see Routes::is_valid for what makes one. Emits RouteAdded. */
class AddRoute final : public Command {
public:
    std::vector<Point> points;

    explicit AddRoute(std::vector<Point> points) : points(std::move(points)) {}

    CommandType type() const override { return CommandType::AddRoute; }
    RejectReason apply(World& world) const override;
    void write_payload(ByteWriter& out) const override;
    std::unique_ptr<Command> clone() const override { return std::make_unique<AddRoute>(*this); }
};

/**
 * A new vehicle on a route. Emits VehicleSpawned, which is the only way the
 * submitter learns the new vehicle's id - the same way a networked client
 * would have to.
 */
class SpawnVehicle final : public Command {
public:
    RouteId route;
    // Wrapped onto the route, so any distance is accepted
    Fixed distance;
    Fixed speed;
    AssetId model;

    SpawnVehicle(RouteId route, Fixed distance, Fixed speed, AssetId model)
        : route(route), distance(distance), speed(speed), model(std::move(model)) {}

    CommandType type() const override { return CommandType::SpawnVehicle; }
    RejectReason apply(World& world) const override;
    void write_payload(ByteWriter& out) const override;
    std::unique_ptr<Command> clone() const override { return std::make_unique<SpawnVehicle>(*this); }
};

/** Takes a vehicle out of the simulation. Emits VehicleDespawned. */
class DespawnVehicle final : public Command {
public:
    VehicleId vehicle;

    explicit DespawnVehicle(VehicleId vehicle) : vehicle(vehicle) {}

    CommandType type() const override { return CommandType::DespawnVehicle; }
    RejectReason apply(World& world) const override;
    void write_payload(ByteWriter& out) const override;
    std::unique_ptr<Command> clone() const override { return std::make_unique<DespawnVehicle>(*this); }
};

/** Changes how fast a vehicle drives. Negative runs it backwards. */
class SetVehicleSpeed final : public Command {
public:
    VehicleId vehicle;
    Fixed speed;

    SetVehicleSpeed(VehicleId vehicle, Fixed speed) : vehicle(vehicle), speed(speed) {}

    CommandType type() const override { return CommandType::SetVehicleSpeed; }
    RejectReason apply(World& world) const override;
    void write_payload(ByteWriter& out) const override;
    std::unique_ptr<Command> clone() const override { return std::make_unique<SetVehicleSpeed>(*this); }
};

/**
 * A command with the stamp that places it in the game: the tick it runs at,
 * who sent it, and its place in that player's order. step() applies a tick's
 * commands sorted by (player, sequence), so the order they arrived in never
 * matters, which a network cannot promise anyway.
 */
struct StampedCommand {
    uint64_t tick = 0;
    uint32_t player = 0;
    uint32_t sequence = 0;
    std::unique_ptr<Command> command;

    StampedCommand clone() const;
};

/**
 * One stamped command as bytes:
 *   u64 tick, u32 player, u32 sequence, u16 type, u32 payload_bytes, payload
 * The payload carries its own length, so a reader that does not know a type
 * can tell where the next command starts.
 */
void write_command(ByteWriter& out, const StampedCommand& command);

/** Reads what write_command() wrote. Nothing on a short, corrupt or unknown record. */
std::optional<StampedCommand> read_command(ByteReader& in);

/** A whole log: u32 count, then the commands. What a replay is built from. */
std::vector<uint8_t> write_command_log(std::span<const StampedCommand> commands);
std::optional<std::vector<StampedCommand>> read_command_log(std::span<const uint8_t> bytes);

/**
 * Where commands wait between being submitted and being run.
 *
 * Commands are stamped with a sequence number as they are submitted, and with
 * a tick only when take() hands them over, so locally a command runs at the
 * very next tick. A lockstep game would stamp the tick at submit time instead,
 * a few ticks ahead so every machine has the command before it is due, and
 * send the stamped command to everyone - this is the one place that changes.
 */
class CommandQueue {
public:
    /** Queues a command. Returns its sequence number, which a refusal will name. */
    uint32_t submit(std::unique_ptr<Command> command, uint32_t player = 0);

    /** Every queued command, stamped for `tick`, in submission order. Empties the queue. */
    std::vector<StampedCommand> take(uint64_t tick);

    size_t pending() const { return queued.size(); }

private:
    std::vector<StampedCommand> queued;
    uint32_t next_sequence = 0;
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_COMMAND_HPP
