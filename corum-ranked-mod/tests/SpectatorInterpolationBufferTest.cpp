#include <cstdio>
#include "../src/domain/SpectatorInterpolationBuffer.hpp"
#include "../src/domain/SpectatorProgressPolicy.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace corum::ranked;

SpectatorBufferedSample linear(int sequence) {
    SpectatorGhostPose pose; pose.x = sequence * 15; pose.y = 80;
    pose.rotation = sequence == 0 ? 359 : 1;
    pose.camera = SpectatorCameraOrigin{pose.x - 165, 40};
    return {sequence, sequence * 50.0, pose};
}
void close(double a, double b, double epsilon = 0.01) { assert(std::abs(a - b) < epsilon); }

int main() {
    // HTTP A contains three samples, B arrives 120ms later with three more.
    // Every intermediate publisher time is used; no 50ms rush then 70ms stop.
    SpectatorInterpolationBuffer batches;
    for (int i = 0; i < 3; ++i) assert(batches.push(linear(i), false, 0));
    assert(batches.size() == 3);
    for (int t = 0; t <= 116; t += 4) {
        if (t == 100) for (int i = 3; i < 6; ++i) assert(batches.push(linear(i), false, 100));
        auto f = batches.render(t); assert(f); close(f->publisherElapsedMs, t);
        close(f->pose.x, t * 0.3); close(f->pose.x - f->pose.camera->x, 165);
    }
    // With the measured arrival budget, the required 120ms batching fixture
    // shows its first pose immediately, then ALL intermediate times at 1x.
    SpectatorInterpolationBuffer exactGap;
    exactGap.noteResponse(0, 120, 0, 3);
    for (int i = 0; i < 3; ++i) assert(exactGap.push(linear(i), false, 0));
    close(exactGap.render(0)->pose.x, 0);
    exactGap.noteResponse(120, 120, 0, 3);
    for (int i = 3; i < 6; ++i) assert(exactGap.push(linear(i), false, 120));
    for (int wall = 120; wall <= 360; wall += 4) {
        if (wall == 240) {
            exactGap.noteResponse(240, 120, 0, 3);
            for (int i = 6; i < 9; ++i) assert(exactGap.push(linear(i), false, 240));
        }
        close(exactGap.render(wall)->pose.x, (wall - 120) * 0.3);
    }
    assert(exactGap.starvations() == 0);

    // Actual device-like RTT, irregular 100..130ms responses, 1..3 packets.
    // Start is tied to the response timestamp, not the next render frame/FPS.
    for (int fps : {60, 120, 144, 240}) {
        SpectatorInterpolationBuffer buffer;
        std::vector<double> arrivals{100, 230, 335, 455, 565, 695, 795, 920, 1040, 1160, 1280, 1400};
        int nextSample = 0; std::size_t nextResponse = 0;
        auto receive = [&](double wall) {
            while (nextResponse < arrivals.size() && arrivals[nextResponse] <= wall) {
                auto const arrival = arrivals[nextResponse];
                auto const lastSample = static_cast<int>(arrival / 50);
                auto const packets = lastSample - nextSample + 1;
                assert(packets >= 1 && packets <= 3);
                buffer.noteResponse(arrival, nextResponse % 2 ? 124 : 108, 80, packets);
                for (; nextSample <= lastSample; ++nextSample) assert(buffer.push(linear(nextSample), false, arrival));
                ++nextResponse;
            }
        };
        for (double wall = 0; wall < 1335; wall += 1000.0 / fps) {
            receive(wall); auto frame = buffer.render(wall);
            if (frame) {
                close(frame->pose.x - frame->pose.camera->x, 165);
                if (wall >= 335) close(frame->publisherElapsedMs, wall - 335);
            }
        }
        receive(1335); auto frame = buffer.render(1335); assert(frame);
        close(frame->publisherElapsedMs, 1000); close(frame->pose.x, 300);
        assert(buffer.starvations() == 0 && buffer.size() <= SpectatorInterpolationBuffer::kMaxSamples);
        assert(buffer.delayMs() >= 150 && buffer.delayMs() <= 350);
    }
    // The code schedules 80ms AFTER an ACK: 108..124ms RTT can mean
    // 188..204ms actual arrival intervals. Exercise that serialized cadence too.
    for (int fps : {60,120,240}) {
        SpectatorInterpolationBuffer buffer;
        std::vector<double> arrivals{196,384,588,780,976,1164,1368,1560};
        int nextSample=0;std::size_t response=0;
        auto receive=[&](double wall){
            while(response<arrivals.size() && arrivals[response]<=wall){
                auto arrival=arrivals[response];auto last=static_cast<int>(arrival/50);
                buffer.noteResponse(arrival,response%2?108:124,80,last-nextSample+1);
                for(;nextSample<=last;++nextSample)assert(buffer.push(linear(nextSample),false,arrival));
                ++response;
            }
        };
        for(double wall=0;wall<1384;wall+=1000.0/fps){
            receive(wall);auto rendered=buffer.render(wall);
            if(rendered && wall>=384)close(rendered->publisherElapsedMs,wall-384);
        }
        receive(1384);close(buffer.render(1384)->pose.x,300);assert(buffer.starvations()==0);
        assert(buffer.averageArrivalMs()>=188 && buffer.averageArrivalMs()<=204);
    }
    // Shortest-angle rotation and discrete state switch at sample boundary.
    SpectatorInterpolationBuffer states;
    auto first = linear(0), second = linear(1), third = linear(2);
    second.pose.mode = SpectatorGameMode::Ship; second.pose.mini = second.pose.upsideDown = true;
    second.pose.flipped = true; second.pose.visible = false;
    second.pose.cameraSnap = true; second.pose.camera->x = 700;
    assert(states.push(first, false, 0)); assert(states.push(second, false, 0)); assert(states.push(third, false, 0));
    auto halfway = states.render(25); close(std::remainder(halfway->pose.rotation, 360), 0);
    assert(halfway->pose.mode == SpectatorGameMode::Cube && !halfway->pose.mini && halfway->pose.visible);
    close(halfway->pose.camera->x, -165); // Camera trigger does not sweep across map.
    auto boundary = states.render(50); assert(boundary->pose.mode == SpectatorGameMode::Ship);
    assert(boundary->pose.mini && boundary->pose.upsideDown && boundary->pose.flipped && !boundary->pose.visible);
    close(boundary->pose.camera->x, 700);

    // One HTTP batch retains A/B/snap C/D/E. All fields cross the boundary on
    // publisher time, and no position/rotation/camera field sweeps B -> C.
    SpectatorInterpolationBuffer segments;
    for (int i = 0; i < 5; ++i) {
        auto s = linear(i); s.publisherElapsedMs += 1000;
        if (i >= 2) {
            s.pose.x += 729; s.pose.y = 740; s.pose.rotation = 271;
            s.pose.mode = SpectatorGameMode::Spider;
            s.pose.upsideDown = s.pose.mini = s.pose.flipped = true;
            s.pose.camera = SpectatorCameraOrigin{s.pose.x - 165, 200};
        }
        assert(segments.push(s, i == 2, 0));
    }
    assert(segments.size() == 5 && segments.renderTimeMs() == 1000);
    auto hold = segments.render(75); close(hold->pose.x, 15); close(hold->pose.y, 80);
    assert(hold->segment == 0 && hold->pose.mode == SpectatorGameMode::Cube);
    close(hold->pose.rotation, 1); close(hold->pose.camera->x, -150);
    assert(!hold->pose.upsideDown && !hold->pose.mini && !hold->pose.flipped);
    auto exact = segments.render(100); close(exact->pose.x, 759); close(exact->pose.rotation, 271);
    close(exact->pose.camera->x, 594); close(exact->pose.camera->y, 200);
    assert(exact->segment == 1 && exact->pose.mode == SpectatorGameMode::Spider);
    assert(exact->pose.upsideDown && exact->pose.mini && exact->pose.flipped);
    close(segments.render(125)->pose.x, 766.5); // C -> D resumes normally
    assert(segments.starvations() == 0);

    // Real native source coordinates/camera allow continuous travel up to the
    // click. They never interpolate across the vertical Spider destination.
    SpectatorInterpolationBuffer sources;
    auto a = linear(0), b = linear(1), c = linear(2), d = linear(3);
    c.pose.mode = d.pose.mode = SpectatorGameMode::Spider;
    c.pose.y = d.pose.y = 740; c.pose.upsideDown = d.pose.upsideDown = true;
    c.pose.rotation = d.pose.rotation = 271;
    c.pose.camera = SpectatorCameraOrigin{-135, 200}; d.pose.camera = SpectatorCameraOrigin{-120, 200};
    c.pose.discontinuity = SpectatorSnapEvidence{SpectatorDiscontinuity::SpiderSnap,30,80,30,740};
    c.pose.discontinuity->fromCamera = SpectatorCameraOrigin{-135,40};
    assert(sources.push(a,false,0) && sources.push(b,false,0) && sources.push(c,true,0) && sources.push(d,false,0));
    auto beforeClick = sources.render(75);
    close(beforeClick->pose.x,22.5); close(beforeClick->pose.y,80); close(beforeClick->pose.camera->x,-142.5);
    close(beforeClick->pose.camera->y,40); assert(!beforeClick->pose.upsideDown);
    auto atClick = sources.render(100);
    close(atClick->pose.x,30); close(atClick->pose.y,740); close(atClick->pose.rotation,271); close(atClick->pose.camera->y,200);
    assert(atClick->pose.upsideDown && atClick->segment==1 && sources.starvations()==0);

    // Multiple snaps in one batch, including same-timestamp terminal. Replacing
    // the final sample cannot erase the destination segment boundary.
    SpectatorInterpolationBuffer multiple;
    for (int i = 0; i < 7; ++i) assert(multiple.push(linear(i), i == 2 || i == 4, 0));
    assert(multiple.size() == 7); close(multiple.render(75)->pose.x, 15);
    assert(multiple.render(100)->segment == 1); close(multiple.render(175)->pose.x, 45);
    assert(multiple.render(200)->segment == 2);
    for (bool dead : {true, false}) {
        SpectatorInterpolationBuffer immediate;
        assert(immediate.push(linear(0), false, 0));
        assert(immediate.push(linear(1), false, 0));
        auto destination = linear(2); destination.pose.y = 740;
        assert(immediate.push(destination, true, 0));
        destination.sequence = 3; destination.pose.dead = dead; destination.pose.finished = !dead;
        assert(immediate.push(destination, false, 0));
        assert(immediate.size() == 3); close(immediate.render(75)->pose.y, 80);
        auto final = immediate.render(100); assert(final->segment == 1);
        close(final->pose.y, 740); assert(final->pose.dead == dead && final->pose.finished == !dead);
    }

    // 26 Spider snaps at alternating 150/200ms, under the ORIGINAL 80ms poll
    // gap + 110ms RTT. Compare the clock/starvation to a continuous control.
    // The snap itself must never consume buffer headroom or change 1x speed.
    for (int fps : {60,120,144,240}) {
        SpectatorInterpolationBuffer stress, control;
        int nextSample = 0, nextSnap = 3, snaps = 0;
        double arrival = 190;
        for (double wall = 0; wall < 5000; wall += 1000.0 / fps) {
            while (arrival <= wall) {
                auto last = static_cast<int>(arrival / 50);
                auto packets = last - nextSample + 1;
                stress.noteResponse(arrival,110,80,packets); control.noteResponse(arrival,110,80,packets);
                for (; nextSample <= last; ++nextSample) {
                    bool snap = nextSample == nextSnap;
                    if (snap) { ++snaps; nextSnap += snaps % 2 ? 4 : 3; }
                    auto s = linear(nextSample); s.pose.mode = SpectatorGameMode::Spider;
                    s.pose.y = snaps % 2 ? 740 : 40; s.pose.upsideDown = snaps % 2;
                    s.pose.camera->y = s.pose.y - 100;
                    auto beforeTime = stress.renderTimeMs(); auto beforeSize = stress.size();
                    assert(stress.push(s,snap,arrival)); assert(control.push(s,false,arrival));
                    if (snap) { close(stress.renderTimeMs(),beforeTime); assert(stress.size()==beforeSize+1); }
                }
                arrival += 190;
            }
            auto a=stress.render(wall),b=control.render(wall);
            assert(bool(a)==bool(b));
            if (a) { close(a->publisherElapsedMs,b->publisherElapsedMs); assert(a->segment<=static_cast<unsigned>(snaps)); }
            assert(stress.starvations()==control.starvations());
        }
        assert(snaps>=26 && stress.starvations()==0 && stress.size()>1);
    }

    // Both terminal types occur on publisher time, not HTTP receipt, then hold.
    for (bool dead : {true, false}) {
        SpectatorInterpolationBuffer terminal;
        for (int i = 0; i < 3; ++i) assert(terminal.push(linear(i), false, 0));
        auto end = linear(3); end.pose.dead = dead; end.pose.finished = !dead; end.pose.rotation = 123;
        assert(terminal.push(end, false, 80));
        assert(!terminal.render(120)->pose.dead && !terminal.render(120)->pose.finished);
        auto final = terminal.render(150); assert(final->pose.dead == dead && final->pose.finished == !dead);
        close(final->pose.rotation, 123); close(terminal.render(3000)->publisherElapsedMs, 150);
        close(terminal.render(4000)->pose.camera->x, end.pose.camera->x); assert(terminal.terminalRendered());
    }
    SpectatorInterpolationBuffer starving;
    for (int i = 0; i < 3; ++i) assert(starving.push(linear(i), false, 0));
    close(starving.render(500)->pose.x, 30); close(starving.render(751)->pose.x, 30);
    assert(starving.buffering() && starving.starvations() == 1);
    for (int i = 3; i < 6; ++i) assert(starving.push(linear(i), false, 800));
    close(starving.render(816)->pose.x, 34.8); assert(!starving.buffering());
    starving.clear(); assert(!starving.render(900));

    // Intentional server-authorized pause drains to the last frame and holds
    // without a stream reset/starvation. Gameplay timestamps exclude wall time.
    SpectatorInterpolationBuffer paused;
    for (int i = 0; i < 4; ++i) assert(paused.push(linear(i), false, 0));
    paused.render(50); paused.setSourcePaused(true, 50);
    close(paused.render(200)->publisherElapsedMs, 150);
    close(paused.render(12200)->publisherElapsedMs, 150);
    assert(!paused.buffering() && !paused.starving() && paused.starvations() == 0);
    paused.setSourcePaused(false, 12200);
    paused.render(12201); assert(paused.starvations() == 0);
    for (int i = 4; i < 8; ++i) assert(paused.push(linear(i), false, 12201));
    close(paused.render(12251)->publisherElapsedMs, 200);
    assert(paused.starvations() == 0);

    SpectatorInterpolationBuffer bounded;
    bool overflow = false;
    for (int i = 0; i < 100; ++i) if (!bounded.push(linear(i), false, 0)) { overflow = true; break; }
    assert(overflow && bounded.size() == 0);
    for(int i=0;i<64;++i)assert(bounded.push({i,static_cast<double>(i),{}},false,0));
    assert(bounded.size()==64 && !bounded.push({64,64,{}},false,0) && bounded.size()==0);
    assert(!bounded.push({0, -1, {}}, false, 0));
    assert(bounded.push(linear(0), false, 0)); assert(!bounded.push(linear(0), false, 0));

    // Pure progress presentation: distance classic, timestamp 2.2, server
    // authority first, and exact remote Clear. Never mutate dummy/local state.
    SpectatorRemoteFrame progress; progress.pose = SpectatorGhostPose{500, 0, 0};
    close(*spectatorProgressPercent(progress, 1000, 0), 50);
    progress.publisherElapsedMs = 5000; close(*spectatorProgressPercent(progress, 100, 2400), 50);
    close(*spectatorProgressPercent(progress, 100, 2400, 73), 73);
    progress.pose->finished = true; close(*spectatorProgressPercent(progress, 1000, 2400, 0), 100);
    progress.pose->finished = false; progress.pose->dead = true; close(*spectatorProgressPercent(progress, 1000, 0), 50);
    std::cout << "Spectator timestamp buffer, 60/120/144/240Hz, terminal and UI progress passed\n";
    // P1-C measurement: identical 50ms publisher + bursty network (each 8th
    // response stalls 450ms, then delivers the backlog). Compare the old EWMA
    // policy with p95 + hysteresis; keep the new one only if it does not worsen.
    {
        auto run=[](bool adaptive){
            SpectatorInterpolationBuffer buffer;buffer.setAdaptiveJitter(adaptive);
            std::int64_t produced=0;double lastResponse=0;int response=0;double rebufferMs=0;std::uint64_t snaps=0;double lastX=-1;
            for(double wall=0;wall<20000;wall+=1000.0/120){
                double gap=(response%8==7)?450:60;
                if(wall-lastResponse>=gap){
                    std::size_t delivered=0;
                    while(produced*50<=wall-30){SpectatorBufferedSample s;s.sequence=produced;s.publisherElapsedMs=produced*50.0;s.pose.x=static_cast<float>(produced*10);s.pose.camera=SpectatorCameraOrigin{0,0};
                        buffer.push(s,false,wall);++produced;++delivered;}
                    buffer.noteResponse(wall,40,gap-40,delivered);lastResponse=wall;++response;
                }
                bool before=buffer.buffering();auto frame=buffer.render(wall);if(before)rebufferMs+=1000.0/120;
                if(frame){if(lastX>=0&&frame->pose.x+0.001<lastX)++snaps;lastX=frame->pose.x;}
            }
            struct R{std::uint64_t starvations;double rebufferMs,delay,maxDelay;std::uint64_t backwardSnaps;};
            return R{buffer.starvations(),rebufferMs,buffer.delayMs(),buffer.maxDelayMs(),snaps};
        };
        auto old=run(false),next=run(true);
        std::printf("P1-C jitter A/B: starvations %llu -> %llu, rebuffer %.0fms -> %.0fms, delay %.0f -> %.0fms (max %.0f), backward snaps %llu -> %llu\n",
            static_cast<unsigned long long>(old.starvations),static_cast<unsigned long long>(next.starvations),old.rebufferMs,next.rebufferMs,old.delay,next.delay,next.maxDelay,
            static_cast<unsigned long long>(old.backwardSnaps),static_cast<unsigned long long>(next.backwardSnaps));
        assert(next.starvations<=old.starvations&&next.rebufferMs<=old.rebufferMs);
        // No measured stall benefit -> the default remains the existing policy.
        assert(next.delay>=old.delay);SpectatorInterpolationBuffer defaults;(void)defaults;
        assert(next.backwardSnaps==0&&next.maxDelay<=SpectatorInterpolationBuffer::kMaxDelayMs);
    }

}
