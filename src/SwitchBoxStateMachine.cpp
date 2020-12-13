#include "SwitchBoxStateMachine.h"

#include <Arduino.h>

#include "ABO.h"
#include "DebugLine.h"
#include "SwitchBox.h"

// This is how you avoid defining template bodies in the header
// https://stackoverflow.com/a/36825508
#include "Fsm.cpp"
std::vector<Sbsm *> stateMachines;

// Used to allow a vector of references
struct StateW {
  State &state;
};

Trigger operator++(Trigger t) {
  return (Trigger)(t + 1);
}

#define FSM(FSM_NAME) sbsm_##FSM_NAME

#define SBSM(FSM_NAME, STATE_NAME)                                                         \
  Sbsm FSM(FSM_NAME)(#FSM_NAME, state_##FSM_NAME##_##STATE_NAME);                          \
  const char *sbsm_##FSM_NAME##_label() { return FSM(FSM_NAME).get_current_state().name; } \
  void FSM_NAME##_setup()

#define STATE_ID(FSM_NAME, STATE_NAME) state_##FSM_NAME##_##STATE_NAME

#define STATE3(FSM_NAME, STATE_NAME, ENTER, IN, EXIT)                                                                                                           \
  void state_##FSM_NAME##_##STATE_NAME##_on_enter() ENTER void state_##FSM_NAME##_##STATE_NAME##_on_state() IN void state_##FSM_NAME##_##STATE_NAME##_on_exit() \
      EXIT State state_##FSM_NAME##_##STATE_NAME(#STATE_NAME, state_##FSM_NAME##_##STATE_NAME##_on_enter, state_##FSM_NAME##_##STATE_NAME##_on_state, state_##FSM_NAME##_##STATE_NAME##_on_exit)

#define STATE2(FSM_NAME, STATE_NAME, ENTER, EXIT) STATE3(FSM_NAME, STATE_NAME, ENTER, {}, EXIT)
#define STATE1(FSM_NAME, STATE_NAME, ENTER) STATE3(FSM_NAME, STATE_NAME, ENTER, {}, {})

#define STATE(FSM_NAME, STATE_NAME) STATE3(FSM_NAME, STATE_NAME, ;, ;, ;)

#define TRANSITION(FSM_NAME, EVENT, A, B) FSM(FSM_NAME).add_transition(state_##FSM_NAME##_##A, state_##FSM_NAME##_##B, EVENT, nullptr)

#define TOGGLE(FSM_NAME, EVENT, A, B) \
  TRANSITION(FSM_NAME, EVENT, A, B);  \
  TRANSITION(FSM_NAME, EVENT, B, A)

#define TOGGLE_FSM(FSM_NAME, ENGAGE, BYPASS)                            \
  STATE1(FSM_NAME, bypass, BYPASS);                                     \
  STATE1(FSM_NAME, engage, ENGAGE);                                     \
  SBSM(FSM_NAME, bypass) {                                              \
    TOGGLE(FSM_NAME, kTrigger_##FSM_NAME##_toggle, bypass, engage);     \
    TRANSITION(FSM_NAME, kTrigger_##FSM_NAME##_engage, bypass, engage); \
    TRANSITION(FSM_NAME, kTrigger_##FSM_NAME##_bypass, engage, bypass); \
  }

#define TOGGLE_FSM_PIN_CUSTOM(FSM_NAME, OP, SOUT_ENGAGE, SOUT_BYPASS)                   \
  TOGGLE_FSM(                                                                           \
      FSM_NAME,                                                                         \
      {                                                                                 \
        abo_digitalWrite({kABIPinShiftRegister, kSout_##OP##_##FSM_NAME}, SOUT_ENGAGE); \
      },                                                                                \
      {                                                                                 \
        abo_digitalWrite({kABIPinShiftRegister, kSout_##OP##_##FSM_NAME}, SOUT_BYPASS); \
      })

#define TOGGLE_FSM_PIN(FSM_NAME) TOGGLE_FSM_PIN_CUSTOM(FSM_NAME, engage, SOUT_HIGH, SOUT_LOW)

#define CYCLE_FSM_STATES(FSM_NAME)                                                                   \
  constexpr size_t FSM_NAME##_STATE_COUNT = kTrigger_##FSM_NAME##_end - kTrigger_##FSM_NAME##_begin; \
  StateW FSM_NAME##_states[FSM_NAME##_STATE_COUNT] =

#define CYCLE_FSM(FSM_NAME)                                                                \
  Sbsm FSM(FSM_NAME)(#FSM_NAME, FSM_NAME##_states[0].state);                               \
  const char *sbsm_##FSM_NAME##_label() { return FSM(FSM_NAME).get_current_state().name; } \
  void FSM_NAME##_setup() {                                                                \
    /* next / prev */                                                                      \
    for (int i = 0; i < FSM_NAME##_STATE_COUNT; ++i) {                                     \
      State &a = FSM_NAME##_states[i].state;                                               \
      State &b = FSM_NAME##_states[(i + 1) % FSM_NAME##_STATE_COUNT].state;                \
      FSM(FSM_NAME).add_transition(a, b, TRIGGER(FSM_NAME, next), nullptr);                \
      FSM(FSM_NAME).add_transition(b, a, TRIGGER(FSM_NAME, prev), nullptr);                \
    }                                                                                      \
    /* for each destination, add a transition from each other state directly to it */      \
    for (Trigger i = kTrigger_##FSM_NAME##_begin; i < kTrigger_##FSM_NAME##_end; ++i) {    \
      State &a = FSM_NAME##_states[i - kTrigger_##FSM_NAME##_begin].state;                 \
      for (int s = 0; s < FSM_NAME##_STATE_COUNT; ++s) {                                   \
        State &b = FSM_NAME##_states[(i + 1) % FSM_NAME##_STATE_COUNT].state;              \
        if (&a != &b) {                                                                    \
          FSM(FSM_NAME).add_transition(b, a, i, nullptr);                                  \
        }                                                                                  \
      }                                                                                    \
    }                                                                                      \
  }

STATE1(input, digital, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_a}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_b}, SOUT_LOW);
});
STATE1(input, analog, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_a}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_b}, SOUT_LOW);
});
STATE1(input, aux, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_a}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_input_b}, SOUT_HIGH);
});
CYCLE_FSM_STATES(input){{state_input_digital}, {state_input_analog}, {state_input_aux}};
CYCLE_FSM(input);

STATE1(mode, day, {
  sbsm_trigger(kTrigger_subwoofer_engage);
  sbsm_trigger(kTrigger_mute_bypass);
});
STATE1(mode, night, {
  sbsm_trigger(kTrigger_subwoofer_bypass);
  sbsm_trigger(kTrigger_mute_bypass);
});
STATE1(mode, off, {
  sbsm_trigger(kTrigger_subwoofer_bypass);
  sbsm_trigger(kTrigger_mute_engage);
});
CYCLE_FSM_STATES(mode){{state_mode_day}, {state_mode_night}, {state_mode_off}};
CYCLE_FSM(mode);

// State       OutputA  OutputB  OutputC  Headphones  Trigger
// Geshelli    low      (low)    (low)    low         -
// Monolith    high     low      (low)    (low)       -
// Speakers    high     high     low      (low)
// ADC         high     high     high     (low)
// Valhalla    high     high     high     high        <engage valhalla>    // will bypass attenuator
STATE1(output, geshelli, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_a}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_b}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_c}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_headphones}, SOUT_LOW);
});
STATE1(output, monolith, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_a}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_b}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_c}, SOUT_LOW);
  abo_digitalWrite({kABIPinShiftRegister, kSout_headphones}, SOUT_LOW);
});
STATE2(
    output, valhalla,
    {
      abo_digitalWrite({kABIPinShiftRegister, kSout_output_a}, SOUT_HIGH);
      abo_digitalWrite({kABIPinShiftRegister, kSout_output_b}, SOUT_HIGH);
      abo_digitalWrite({kABIPinShiftRegister, kSout_output_c}, SOUT_HIGH);
      abo_digitalWrite({kABIPinShiftRegister, kSout_headphones}, SOUT_HIGH);
      sbsm_trigger(kTrigger_valhalla_force);
    },
    { sbsm_trigger(kTrigger_valhalla_release); });
STATE1(output, speakers, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_a}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_b}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_c}, SOUT_LOW);
});
STATE1(output, adc, {
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_a}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_b}, SOUT_HIGH);
  abo_digitalWrite({kABIPinShiftRegister, kSout_output_c}, SOUT_HIGH);
});

CYCLE_FSM_STATES(output){{state_output_geshelli}, {state_output_monolith}, {state_output_valhalla}, {state_output_speakers}, {state_output_adc}};
CYCLE_FSM(output);

TOGGLE_FSM_PIN(loki);
TOGGLE_FSM_PIN(bellari);
STATE1(valhalla, bypass, { abo_digitalWrite({kABIPinShiftRegister, kSout_engage_valhalla}, SOUT_LOW); });
STATE1(valhalla, engage, { abo_digitalWrite({kABIPinShiftRegister, kSout_engage_valhalla}, SOUT_HIGH); });
STATE1(valhalla, bypass_force_engage, { state_valhalla_engage_on_enter(); });
STATE1(valhalla, engage_force_engage, { state_valhalla_engage_on_enter(); });
SBSM(valhalla, bypass) {
  TOGGLE(valhalla, kTrigger_valhalla_toggle, bypass, engage);
  TOGGLE(valhalla, kTrigger_valhalla_toggle, bypass_force_engage, engage_force_engage);
  TRANSITION(valhalla, kTrigger_valhalla_force, bypass, bypass_force_engage);
  TRANSITION(valhalla, kTrigger_valhalla_force, engage, engage_force_engage);
  TRANSITION(valhalla, kTrigger_valhalla_release, bypass_force_engage, bypass);
  TRANSITION(valhalla, kTrigger_valhalla_release, engage_force_engage, engage);
};
TOGGLE_FSM_PIN_CUSTOM(subwoofer, disable, SOUT_LOW, SOUT_HIGH);
TOGGLE_FSM_PIN(level);
TOGGLE_FSM_PIN(mute);

std::map<Trigger, const char *> triggerNames;

const char *missingString = "MISSING";

const char *sbsm_trigger_name(Trigger event) {
  const auto it = triggerNames.find(event);
  if (it == triggerNames.end()) {
    return missingString;
  }
  return it->second;
}

void sbsm_trigger(Trigger event) {
  Serial_printf("\nsbsm_trigger: [%d] (%s)\n", event, sbsm_trigger_name(event));
  for (auto fsm : stateMachines) {
    fsm->trigger(event);
  }
}

struct MenuDef {
  Menu &menu;
  Trigger trigger;
  const char *label;
  Sbsm *fsm;
  State *state;
};

Menu inputMenu("Input");
Menu preampMenu("Preamp");
Menu outputMenu("Output");

MenuDef menuDefs[] = {
    {inputMenu, kTrigger_input_next, "Next Input", nullptr, nullptr},
    {inputMenu, kTrigger_input_digital, "Digital", &sbsm_input, &state_input_digital},
    {inputMenu, kTrigger_input_analog, "Digital", &sbsm_input, &state_input_analog},
    {inputMenu, kTrigger_input_aux, "Aux", &sbsm_input, &state_input_aux},
};

void sbsm_setup() {
  for (const auto e : menuDefs) {
    triggerNames.insert({e.trigger, e.label});
    Menu *menu = new TriggerMenu(e.trigger, e.label, e.fsm, e.state);
    Serial_printf("New menu item: 0x%x\n", menu);
    e.menu.add(menu);
  }
}

void sbsm_loop() {
  for (auto machine : stateMachines) {
    machine->run_machine();
  }
}

void TriggerMenu::onEnter() {
  sbsm_trigger(m_trigger);
}

bool TriggerMenu::isChecked() {
  if (!m_fsm || !m_state) {
    return false;
  }
  return &m_fsm->get_current_state() == m_state;
}
