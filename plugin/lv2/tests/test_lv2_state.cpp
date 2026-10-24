// SPDX-License-Identifier: MIT
// D3-R2: state (D1's JSON in one atom:String property), the worker jobs, the notify port and the
// thread rules (`run` never calls a controller main-thread function).
#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "lv2_test_support.hpp"
#include "pme/plugin/state.hpp"

using namespace pme::lv2;
using namespace pme::lv2::test;

namespace {

std::string stateText(const StateStore& store, const Rig& rig) {
    const auto it = store.items.find(rig.plugin->urids().stateKey);
    REQUIRE(it != store.items.end());
    return {reinterpret_cast<const char*>(it->second.bytes.data())};
}

/// Run a recorded job on a separate thread, as a host's worker does; returns the response.
JobResult runJob(Rig& rig, const Job& job) {
    JobResult result;
    LV2_Worker_Status status = LV2_WORKER_ERR_UNKNOWN;
    std::thread worker([&] {
        // No doctest assertions off the main thread: the status is checked after the join.
        status = rig.plugin->work(
            [](LV2_Worker_Respond_Handle h, std::uint32_t size, const void* data) {
                if (size != sizeof(JobResult)) {
                    return LV2_WORKER_ERR_UNKNOWN;
                }
                std::memcpy(h, data, size);
                return LV2_WORKER_SUCCESS;
            },
            &result, sizeof(Job), &job);
    });
    worker.join();
    CHECK(status == LV2_WORKER_SUCCESS);
    return result;
}

}  // namespace

TEST_CASE("D3-R2 state saves D1's JSON byte for byte in one atom:String property") {
    Rig rig;
    rig.plugin->controller().setInstrumentReference("/some/instrument.json");
    StateStore store;
    REQUIRE(rig.plugin->save(StateStore::store, &store, 0) == LV2_STATE_SUCCESS);
    REQUIRE(store.items.size() == 1);
    const auto& item = store.items.begin()->second;
    CHECK(item.type == rig.plugin->urids().atomString);
    CHECK(item.flags == (LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE));
    CHECK(item.bytes.back() == 0);  // the NUL is part of an atom:String
    const std::string expected =
        pme::plugin::serializeState(rig.plugin->controller().captureState());
    CHECK(stateText(store, rig) == expected);
    CHECK(stateText(store, rig).find("/some/instrument.json") != std::string::npos);
}

TEST_CASE("D3-R2 state round trip is exact") {
    Rig a;
    REQUIRE(a.plugin->activate());
    a.plugin->controller().params().setValue(a.plugin->controller().params().indexOf(2001), -6.0);
    a.plugin->controller().params().setValue(a.plugin->controller().params().indexOf(5001), 0.5);
    a.plugin->controller().setInstrumentReference("/x/y.json");
    StateStore s1;
    REQUIRE(a.plugin->save(StateStore::store, &s1, 0) == LV2_STATE_SUCCESS);

    Rig b;
    REQUIRE(b.plugin->restore(StateStore::retrieve, &s1) == LV2_STATE_SUCCESS);
    StateStore s2;
    REQUIRE(b.plugin->save(StateStore::store, &s2, 0) == LV2_STATE_SUCCESS);
    CHECK(stateText(s1, a) == stateText(s2, b));
    CHECK(b.plugin->controller().instrumentReference() == "/x/y.json");
    CHECK(b.plugin->controller().params().value(b.plugin->controller().params().indexOf(5001)) ==
          doctest::Approx(0.5));
}

TEST_CASE("D3-R2 a newer schema or unparsable text is rejected and leaves the plug-in unchanged") {
    Rig rig;
    rig.plugin->controller().params().setValue(rig.plugin->controller().params().indexOf(2001),
                                               -3.0);
    const auto key = rig.plugin->urids().stateKey;
    const auto string = rig.plugin->urids().atomString;
    StateStore newer;
    newer.put(
        key, string,
        R"({"instrument":"","interfaces_version":2,"params":{"2001":-9},"plugin_version":"0","state_schema_version":99})");
    CHECK(rig.plugin->restore(StateStore::retrieve, &newer) != LV2_STATE_SUCCESS);
    StateStore garbage;
    garbage.put(key, string, "not json");
    CHECK(rig.plugin->restore(StateStore::retrieve, &garbage) != LV2_STATE_SUCCESS);
    StateStore wrongType;
    wrongType.put(key, string + 1, "{}");
    CHECK(rig.plugin->restore(StateStore::retrieve, &wrongType) == LV2_STATE_ERR_BAD_TYPE);
    StateStore empty;
    CHECK(rig.plugin->restore(StateStore::retrieve, &empty) == LV2_STATE_ERR_NO_PROPERTY);
    CHECK(rig.plugin->controller().params().value(
              rig.plugin->controller().params().indexOf(2001)) == doctest::Approx(-3.0));
}

TEST_CASE("D3-R2 an unknown parameter id in the state is ignored") {
    Rig rig;
    StateStore store;
    store.put(
        rig.plugin->urids().stateKey, rig.plugin->urids().atomString,
        R"({"instrument":"","interfaces_version":2,"params":{"2001":-9,"999999":1},"plugin_version":"0","state_schema_version":1})");
    CHECK(rig.plugin->restore(StateStore::retrieve, &store) == LV2_STATE_SUCCESS);
    CHECK(rig.plugin->controller().params().value(
              rig.plugin->controller().params().indexOf(2001)) == doctest::Approx(-9.0));
}

TEST_CASE("D3-R2 the worker job is trivially copyable and run never calls a main-thread function") {
    static_assert(std::is_trivially_copyable_v<Job>);
    static_assert(std::is_trivially_copyable_v<JobResult>);
    static_assert(std::is_standard_layout_v<Job>);
    CHECK(std::is_trivially_copyable_v<Job>);

    Rig rig;
    REQUIRE(rig.plugin->activate());
    const int before = rig.engine->mainThreadCalls.load();
    // 20 blocks of 512 frames = 10240 frames, more than two 100 ms ticks at 48 kHz, with an
    // instrument change in the middle: the audio thread only schedules.
    for (int i = 0; i < 20; ++i) {
        if (i == 5) {
            rig.begin().setPath(10, rig.instrumentUrid(), "/a/b.json").end();
        } else {
            rig.emptyInput();
        }
        rig.run();
    }
    CHECK(rig.engine->mainThreadCalls.load() == before);
    CHECK(rig.engine->processThread == std::this_thread::get_id());
    CHECK(rig.scheduler.countOf(JobKind::Instrument) == 1);
    CHECK(rig.scheduler.countOf(JobKind::Tick) == 1);  // at most one tick outstanding
}

TEST_CASE("D3-R2 the worker runs the instrument and tick jobs and answers on the notify port") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.begin().setPath(10, rig.instrumentUrid(), "/a/b.json").end();
    rig.run();
    REQUIRE(rig.scheduler.countOf(JobKind::Instrument) == 1);
    const Job job = rig.scheduler.jobs[0];
    CHECK(job.kind == static_cast<std::uint32_t>(JobKind::Instrument));
    CHECK(std::string(job.path.data(), job.length) == "/a/b.json");

    const JobResult result = runJob(rig, job);
    CHECK(result.kind == job.kind);
    CHECK(rig.plugin->controller().instrumentReference() == "/a/b.json");

    // The audio thread receives the response, then reports the instrument on the notify port.
    CHECK(rig.plugin->workResponse(sizeof(JobResult), &result) == LV2_WORKER_SUCCESS);
    rig.emptyInput();
    rig.run();
    bool found = false;
    for (const Reply& r : readNotify(rig.notify, rig.urids)) {
        if (r.property == rig.instrumentUrid() && r.isPath && r.path == "/a/b.json") {
            found = true;
        }
    }
    CHECK(found);

    // A tick: runs collectRetired on a non-audio thread, response clears the outstanding flag.
    for (int i = 0; i < 12; ++i) {
        rig.emptyInput();
        rig.run();
    }
    REQUIRE(rig.scheduler.countOf(JobKind::Tick) == 1);
    Job tick{};
    for (std::size_t i = 0; i < rig.scheduler.count; ++i) {
        if (rig.scheduler.jobs[i].kind == static_cast<std::uint32_t>(JobKind::Tick)) {
            tick = rig.scheduler.jobs[i];
        }
    }
    const int collectBefore = rig.engine->collectCalls.load();
    const JobResult tickResult = runJob(rig, tick);
    CHECK(rig.engine->collectCalls.load() == collectBefore + 1);
    CHECK(rig.engine->collectThread != std::this_thread::get_id());
    CHECK(rig.plugin->workResponse(sizeof(JobResult), &tickResult) == LV2_WORKER_SUCCESS);
    for (int i = 0; i < 12; ++i) {
        rig.emptyInput();
        rig.run();
    }
    CHECK(rig.scheduler.countOf(JobKind::Tick) == 2);  // the next tick after the response
}

TEST_CASE("D3-R2 an over-long instrument path is rejected and counted") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.begin().setPath(0, rig.instrumentUrid(), std::string(kMaxPath + 10, 'x')).end();
    rig.run();
    CHECK(rig.scheduler.countOf(JobKind::Instrument) == 0);
    CHECK(rig.plugin->droppedInstrumentJobs() == 1);
}

TEST_CASE(
    "D3-R2 without work:schedule instrument changes are ignored and ticks run in deactivate and "
    "save") {
    Rig rig(false);
    REQUIRE(rig.plugin->activate());
    CHECK_FALSE(rig.plugin->hasWorker());
    rig.begin().setPath(0, rig.instrumentUrid(), "/a/b.json").end();
    rig.run();
    CHECK(rig.plugin->droppedInstrumentJobs() == 1);
    CHECK(rig.plugin->controller().instrumentReference().empty());

    const int collect0 = rig.engine->collectCalls.load();
    StateStore store;
    CHECK(rig.plugin->save(StateStore::store, &store, 0) == LV2_STATE_SUCCESS);
    CHECK(rig.engine->collectCalls.load() == collect0 + 1);
    rig.plugin->deactivate();
    CHECK(rig.engine->collectCalls.load() == collect0 + 2);
}

TEST_CASE("D3-R2 patch:Get answers with patch:Set and restore reports the restored properties") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.plugin->controller().params().setValue(rig.plugin->controller().params().indexOf(5001),
                                               0.5);
    rig.emptyInput();
    rig.run();

    rig.begin().get(20, rig.structuralUrid(5001)).end();
    rig.run();
    std::vector<Reply> replies = readNotify(rig.notify, rig.urids);
    REQUIRE(replies.size() == 1);
    CHECK(replies[0].property == rig.structuralUrid(5001));
    CHECK(replies[0].number == doctest::Approx(0.5));
    CHECK(replies[0].frame == 20);

    // patch:Get without a property: every structural parameter and the instrument.
    rig.begin().get(0, 0).end();
    rig.run();
    replies = readNotify(rig.notify, rig.urids);
    std::size_t structural = 0;
    for (const pme::ParamInfo& info : pme::paramRegistry()) {
        structural += info.cls == pme::ParamClass::Structural ? 1U : 0U;
    }
    CHECK(replies.size() == structural + 1);

    // After a restore the next run reports everything without being asked.
    StateStore store;
    store.put(
        rig.plugin->urids().stateKey, rig.plugin->urids().atomString,
        R"({"instrument":"/r.json","interfaces_version":2,"params":{"5001":0.25},"plugin_version":"0","state_schema_version":1})");
    REQUIRE(rig.plugin->restore(StateStore::retrieve, &store) == LV2_STATE_SUCCESS);
    rig.emptyInput();
    rig.run();
    replies = readNotify(rig.notify, rig.urids);
    CHECK(replies.size() == structural + 1);
    bool instrument = false;
    bool structuralValue = false;
    for (const Reply& r : replies) {
        instrument = instrument || (r.property == rig.instrumentUrid() && r.path == "/r.json");
        structuralValue = structuralValue || (r.property == rig.structuralUrid(5001) &&
                                              r.number == doctest::Approx(0.25));
    }
    CHECK(instrument);
    CHECK(structuralValue);
}

TEST_CASE(
    "D3-R2 reviewer 200 truncated or mutated or random state strings never crash or change a "
    "rejected plug-in") {
    Rig rig;
    REQUIRE(rig.plugin->activate());
    rig.plugin->controller().setInstrumentReference("/some/instrument.json");
    StateStore seed;
    REQUIRE(rig.plugin->save(StateStore::store, &seed, 0) == LV2_STATE_SUCCESS);
    const std::string valid = stateText(seed, rig);
    REQUIRE(valid.size() > 40);

    const auto key = rig.plugin->urids().stateKey;
    const auto string = rig.plugin->urids().atomString;
    std::uint64_t rng = 0x9e3779b97f4a7c15ULL;  // fixed seed: the case is deterministic
    const auto next = [&rng]() {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(rng >> 33);
    };

    int rejected = 0;
    for (int i = 0; i < 200; ++i) {
        std::string text;
        switch (i % 4) {
            case 0:  // truncated at every kind of position
                text = valid.substr(0, next() % valid.size());
                break;
            case 1:  // one byte flipped
                text = valid;
                text[next() % text.size()] = static_cast<char>(next());
                break;
            case 2:  // random bytes (no NUL inside: the text ends at the NUL put() adds)
                for (std::uint32_t n = next() % 200; n > 0; --n) {
                    text.push_back(static_cast<char>(1 + next() % 255));
                }
                break;
            default:  // valid prefix followed by junk
                text = valid.substr(0, next() % valid.size()) + "}}]]\"\\";
                break;
        }
        const std::string before = [&] {
            StateStore s;
            REQUIRE(rig.plugin->save(StateStore::store, &s, 0) == LV2_STATE_SUCCESS);
            return stateText(s, rig);
        }();
        StateStore store;
        store.put(key, string, text);
        const LV2_State_Status status = rig.plugin->restore(StateStore::retrieve, &store);
        if (status != LV2_STATE_SUCCESS) {
            ++rejected;
            StateStore s;
            REQUIRE(rig.plugin->save(StateStore::store, &s, 0) == LV2_STATE_SUCCESS);
            CHECK_MESSAGE(stateText(s, rig) == before,
                          "rejected restore changed the plug-in, case ", i);
        }
        // Whatever happened, the plug-in still runs and saves.
        rig.plugin->run(kRigBlock);
    }
    CHECK(rejected > 100);  // most mutations are not valid state

    // A restored instrument path longer than a worker job can carry must not overrun a buffer.
    StateStore longPath;
    longPath.put(
        key, string,
        R"({"instrument":")" + std::string(5000, 'p') +
            R"(","interfaces_version":2,"params":{},"plugin_version":"0","state_schema_version":1})");
    (void)rig.plugin->restore(StateStore::retrieve, &longPath);
    rig.plugin->run(kRigBlock);
}
