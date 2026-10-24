#include "input_synthetic.hpp"

#include <cstdlib>
#include <iostream>

using namespace aoe2::game_hooks::synthetic;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void mouse_motion_is_reported_once_per_period_step() {
    SyntheticMouse mouse;
    Check(mouse.Next(0).delta_x == 0.0f, "no motion at step 0");
    const auto moved = mouse.Next(50);
    Check(moved.delta_x == 3.0f && moved.delta_y == -2.0f, "motion at step 50");
    const auto repeated = mouse.Next(50);
    Check(repeated.delta_x == 0.0f && repeated.delta_y == 0.0f, "the same step is not reported twice");
    Check(mouse.Next(51).delta_x == 0.0f, "no motion between periods");
    Check(mouse.Next(100).delta_x == 3.0f, "motion at step 100");
}

void left_button_toggles_every_200_steps() {
    Check(!MouseButtonDown(0) && !MouseButtonDown(199), "up for steps 0 to 199");
    Check(MouseButtonDown(200) && MouseButtonDown(399), "down for steps 200 to 399");
    Check(!MouseButtonDown(400) && MouseButtonDown(600), "toggles again");
    SyntheticMouse mouse;
    Check(mouse.Next(200).buttons == aoe2::input::kMouseButtonLeft, "report carries the left button bit");
}

void keyboard_holds_a_for_30_of_every_120_steps() {
    Check(KeyboardState(0).test(kKeyboardUsageA) && KeyboardState(29).test(kKeyboardUsageA), "A held at the start");
    Check(!KeyboardState(30).any() && !KeyboardState(119).any(), "released for the rest of the period");
    Check(KeyboardState(120).test(kKeyboardUsageA), "held again in the next period");
    Check(KeyboardState(0).count() == 1, "only A is ever held");
}

void two_instances_produce_the_same_sequence() {
    SyntheticMouse first, second;
    for (uint64_t step = 0; step < 1000; ++step) {
        const auto a = first.Next(step), b = second.Next(step);
        Check(a.delta_x == b.delta_x && a.delta_y == b.delta_y && a.buttons == b.buttons, "mouse sequence is reproducible");
        Check(KeyboardState(step) == KeyboardState(step), "keyboard state is a pure function of the step");
    }
}

void logical_steps_do_not_advance_when_synthetic_input_is_off() {
    const uint64_t before = LogicalSteps(Module::kMouse).load();
    NoteUpdate(Module::kMouse);
    Check(Enabled() || LogicalSteps(Module::kMouse).load() == before, "disabled input never counts steps");
}
}  // namespace

int main() {
    mouse_motion_is_reported_once_per_period_step();
    left_button_toggles_every_200_steps();
    keyboard_holds_a_for_30_of_every_120_steps();
    two_instances_produce_the_same_sequence();
    logical_steps_do_not_advance_when_synthetic_input_is_off();
    std::cout << "input synthetic tests passed\n";
    return 0;
}
