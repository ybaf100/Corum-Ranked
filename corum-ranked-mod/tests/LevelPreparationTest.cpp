#include "../src/domain/LevelPreparationPolicy.hpp"

#include <cassert>
#include <iostream>

using namespace corum::ranked;

namespace {
LevelPreparationContext context() { return {"match", 123, 1, 0, false, 0, 0}; }
LevelMetadata online() { return {123, "Native name", "Native creator", 0, 0, 0, 0, 1, true}; }

void metadataProof() {
    PreparedLevelProof proof;
    auto map = context();
    auto metadata = online();
    assert(metadata.complete()); // Actual built-in track 0, unregistered creator.
    assert(!proof.matches(map, metadata)); // Valid-looking cache is NOT an attestation.
    assert(proof.attest(map, metadata));
    assert(proof.matches(map, metadata));
    auto changed = metadata;
    changed.name.clear();
    assert(!proof.matches(map, changed)); // Same native pointer can mutate in place.
    changed = metadata;
    changed.creator.clear();
    assert(!proof.attest(map, changed));
    assert(!proof.matches(map, metadata)); // Failed attestation clears old authority.
    changed = metadata;
    changed.online = false;
    assert(!proof.attest(map, changed));
    changed = metadata;
    changed.levelId++;
    assert(!proof.attest(map, changed));
    metadata.songId = 456;
    assert(proof.attest(map, metadata));
    assert(proof.matches(map, metadata));
    changed = metadata;
    changed.songId = 0;
    assert(!proof.matches(map, changed)); // Download replaced custom with defaults.
    changed = metadata;
    changed.audioTrack++;
    assert(!proof.matches(map, changed));
    changed = metadata;
    changed.version++;
    assert(!proof.matches(map, changed)); // Update needs fresh online metadata.
    changed = metadata;
    changed.userId++;
    assert(!proof.matches(map, changed));
    auto next = map;
    next.matchId = "next-match";
    assert(!proof.matches(next, metadata));
    next = map;
    next.round++;
    assert(!proof.matches(next, metadata));
    next = map;
    next.deathmatch++;
    assert(!proof.matches(next, metadata));
    next = map;
    next.placement = true;
    next.stage = 1;
    next.map = 1;
    assert(proof.attest(next, metadata));
    next.map++;
    assert(!proof.matches(next, metadata)); // Even when both maps have the same GD ID.
    next.map--;
    next.stage++;
    assert(!proof.matches(next, metadata));
    proof.clear();
    assert(!proof.matches(map, metadata));
}

void requestLifecycle() {
    LevelLookupTicket ticket;
    auto first = context();
    auto next = first;
    next.round++;
    assert(!ticket.begin({}));
    assert(ticket.begin(first));
    auto const generation = ticket.generation();
    assert(!ticket.begin(first)); // No same-key timeout overlap.
    assert(!ticket.begin(next));
    assert(ticket.generation() == generation);
    ticket.observe(next);
    ticket.observe(first);
    assert(!ticket.finish(first)); // A -> B -> A cannot resurrect an obsolete request.
    assert(!ticket.finish(first)); // Duplicate completion.
    assert(ticket.begin(first));
    assert(ticket.generation() > generation);
    ticket.discard(); // Popup exited or another native delegate took ownership.
    assert(ticket.busy()); // Drain, do not locally invent HTTP cancellation.
    assert(!ticket.finish(first));
    assert(ticket.begin(next));
    assert(ticket.finish(next));
    assert(!ticket.busy());
    assert(ticket.begin(first));
    assert(!ticket.finish(next));
    assert(ticket.begin(first));
    ticket.observe({});
    assert(!ticket.finish(first));
}

void launchMatrix() {
    // Exercise every combination, including song bypass + incomplete metadata,
    // modal opened after scheduling, expired start window and native busy state.
    for (unsigned mask = 0; mask < 256; ++mask) {
        bool current = mask & 1;
        bool metadata = mask & 2;
        bool geometry = mask & 4;
        bool song = mask & 8;
        bool bypass = mask & 16;
        bool start = mask & 32;
        bool modal = mask & 64;
        bool busy = mask & 128;
        auto allowed = preparationAllowsLaunch(current, metadata, geometry, song, bypass, start, modal, busy);
        if (allowed) {
            assert(current && metadata && geometry && start);
            assert(song || bypass);
            assert(!modal && !busy);
        } else {
            assert(!current || !metadata || !geometry || (!song && !bypass) || !start || modal || busy);
        }
    }
    // Prepare -> options hold -> close -> launch; no mutation of the server clock.
    assert(!preparationAllowsLaunch(true, true, true, true, false, true, true, false));
    assert(preparationAllowsLaunch(true, true, true, true, false, true, false, false));
    assert(!preparationAllowsLaunch(true, true, true, true, false, false, false, false));
    // Actual RobTop and downloaded custom/Jukebox both use the same proven gate.
    assert(preparationAllowsLaunch(true, true, true, true, false, true, false, false));
    assert(preparationAllowsLaunch(true, true, true, false, true, true, false, false));
    assert(!preparationAllowsLaunch(true, false, true, false, true, true, false, false));
}
}

int main() {
    metadataProof();
    requestLifecycle();
    launchMatrix();
    std::cout << "Level preparation proof, callback lifecycle and 256 launch combinations passed\n";
}
