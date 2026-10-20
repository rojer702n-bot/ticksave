#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/SceneManager.hpp>
#include <deque>

using namespace geode::prelude;

// 240 Hz physics = 1 tick = 1 frame at 240fps (how the video uses "ticks/frames")
static const float TICK       = 1.f / 240.f;
static const int   CONSOLE_TAG = 9001;
static const std::vector<int> STAGES = { 0, 1, 2, 4, 8, 16 }; // like the video's stages

namespace ts {
    inline int stageIndex = 0;   // F6 cycles this
    inline int savedTotal = 0;   // "ticks saved" counter shown on the console
}

struct Snap {
    float  time;
    CCPoint p1, p2;   // player positions
    float  yv1, yv2;  // vertical velocities
};

struct InputEvent {
    float time;
    bool  held;
};

// ---------------------------------------------------------------------------
// Core: rollback + input delay ("buffered clicks")
//
// When the player dies and ticks are enabled:
//   1. Roll the player back N ticks (their position/velocity from N ticks ago).
//   2. For the next N ticks, apply the user's PHYSICAL input but delayed by
//      N ticks (an early click lands exactly N ticks later = "it jumps for me").
//   3. If the corrected input still dies inside the window -> real death.
//      (You were off by more than N ticks, no save.)
// ---------------------------------------------------------------------------
$modify(GJBaseGameLayer) {
    struct Fields {
        std::deque<Snap>       snaps;   // recent player states
        std::deque<InputEvent> inputs;  // user hold/release timeline
        bool  windowActive = false;     // input-delay window running
        float windowLeft   = 0.f;
        float delay         = 0.f;      // N ticks in seconds
        int   savesThisAttempt = 0;
        bool  userHold      = false;    // physical mouse state
        float clock         = 0.f;
    };

    // state of the user's input at a past time (for delayed replay)
    bool userHoldAt(float t) {
        auto& f = m_fields;
        bool held = false;
        for (auto& e : f.inputs) {          // sorted by time
            if (e.time > t) break;
            held = e.held;
        }
        return held;
    }

    void update(float dt) override {
        auto& f = m_fields;
        f.clock += dt;

        // -- keep the snapshot ring buffer (last 0.5s is plenty) --
        if (m_player1) {
            f.snaps.push_back({
                f.clock,
                m_player1->getPosition(),
                m_player2 ? m_player2->getPosition() : CCPoint{0, 0},
                m_player1->m_yVelocity,
                m_player2 ? m_player2->m_yVelocity : 0.f
            });
            while (f.snaps.size() > 2 && f.clock - f.snaps.front().time > 0.5f)
                f.snaps.pop_front();
        }

        // -- input-delay window: force delayed clicks --
        if (f.windowActive) {
            f.windowLeft -= dt;
            bool delayed = f.userHoldAt(f.clock - f.delay);
            m_player1->m_isHolding = delayed;
            if (m_player2) m_player2->m_isHolding = delayed;

            if (f.windowLeft <= 0.f) {
                f.windowActive = false;
                this->handleButton(f.userHold, 0); // re-sync to physical input
            }
        }

        GJBaseGameLayer::update(dt);
        this->updateConsole();
    }

    void handleButton(bool push, int button) override {
        auto& f = m_fields;
        f.userHold = push;
        f.inputs.push_back({ f.clock, push });
        if (f.inputs.size() > 512) f.inputs.pop_front();

        // during the delay window WE control m_isHolding, ignore raw clicks
        if (!f.windowActive)
            GJBaseGameLayer::handleButton(push, button);
    }

    void destroyPlayer(GameObject* obj, int id) override {
        auto& f = m_fields;
        int   ticks = STAGES[ts::stageIndex];
        float need  = ticks * TICK;

        // die normally if: cheat off, correction already in progress,
        // out of snapshots, or too many saves this attempt (anti-immortal)
        if (!ticks || f.windowActive || f.savesThisAttempt >= 10
            || !m_player1 || f.clock < need || f.snaps.empty()) {
            return GJBaseGameLayer::destroyPlayer(obj, id);
        }

        // find the snapshot at least N ticks old (oldest match = deepest save)
        Snap* target = nullptr;
        for (auto it = f.snaps.rbegin(); it != f.snaps.rend(); ++it) {
            if (f.clock - it->time >= need) { target = &*it; break; }
        }
        if (!target)
            return GJBaseGameLayer::destroyPlayer(obj, id);

        // ---- rollback the player N ticks ----
        m_player1->setPosition(target->p1);
        m_player1->m_yVelocity = target->yv1;
        m_player1->m_isHolding  = false;
        if (m_player2) {
            m_player2->setPosition(target->p2);
            m_player2->m_yVelocity = target->yv2;
            m_player2->m_isHolding = false;
        }

        // ---- start the input-delay window (buffered clicks) ----
        f.delay = need;
        f.windowActive = true;
        f.windowLeft = need;
        f.savesThisAttempt++;
        ts::savedTotal += ticks;   // "how many ticks you saved" on the console

        // DO NOT call original -> death cancelled, run continues
    }

    void updateConsole() {
        if (auto* label = typeinfo_cast<CCLabelBMFont*>(this->getChildByTag(CONSOLE_TAG))) {
            int ticks = STAGES[ts::stageIndex];
            label->setString(fmt::format(
                "[TickSave] F6: {} | Saved: {}",
                ticks ? fmt::format("+{} ticks", ticks) : "OFF",
                ts::savedTotal
            ).c_str());
        }
    }
  
    void resetLevel() override {
        auto& f = m_fields;
        f.windowActive = false;
        f.savesThisAttempt = 0;
        GJBaseGameLayer::resetLevel();
    }

};

// ---------------------------------------------------------------------------
// Console overlay + per-attempt reset
// ---------------------------------------------------------------------------
$modify(PlayLayer) {
    bool init() override {
        if (!PlayLayer::init()) return false;

        auto label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setTag(CONSOLE_TAG);
        label->setScale(0.5f);
        label->setAnchorPoint({ 0.f, 1.f });
        label->setPosition({ 8.f, CCDirector::get()->getWinSize().height - 8.f });
        this->addChild(label, 9999);
        return true;
    }
};

// ---------------------------------------------------------------------------
// F6 cycles the stages, exactly like in the video
// ---------------------------------------------------------------------------
$modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat) {
        if (key == KEY_F6 && down && !repeat) {
            if (SceneManager::get()->getCurrentPlayLayer()) {
                ts::stageIndex = (ts::stageIndex + 1) % STAGES.size();
                return true; // consume so it doesn't reach the level
            }
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat);
    }
};
