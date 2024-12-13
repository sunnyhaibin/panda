#!/usr/bin/env python3
import argparse
import os
from collections import Counter, defaultdict

from panda.python import ALTERNATIVE_EXPERIENCE
from panda.tests.libpanda import libpanda_py
from panda.tests.safety_replay.helpers import package_can_msg, init_segment

# Define debug variables and their getter methods
DEBUG_VARS = {
  'lat_active': lambda safety: safety.get_lat_active(),
  'controls_allowed': lambda safety: safety.get_controls_allowed(),
  'controls_requested_lat': lambda safety: safety.get_controls_requested_lat(),
  'controls_allowed_lat': lambda safety: safety.get_controls_allowed_lat(),
  'current_disengage_reason': lambda safety: safety.mads_get_current_disengage_reason(),
  'stock_acc_main': lambda safety: safety.get_acc_main_on(),
  'mads_acc_main': lambda safety: safety.get_mads_acc_main(),
}


# replay a drive to check for safety violations
def replay_drive(lr, safety_mode, param, alternative_experience, segment=False):
  safety = libpanda_py.libpanda

  err = safety.set_safety_hooks(safety_mode, param)
  assert err == 0, "invalid safety mode: %d" % safety_mode
  safety.set_alternative_experience(alternative_experience)

  _enable_mads = bool(alternative_experience & ALTERNATIVE_EXPERIENCE.ENABLE_MADS)
  _disengage_lateral_on_brake = bool(alternative_experience & ALTERNATIVE_EXPERIENCE.DISENGAGE_LATERAL_ON_BRAKE)
  safety.set_mads_params(_enable_mads, _disengage_lateral_on_brake)
  print("alternative experience:")
  print(f"  enable mads: {_enable_mads}")
  print(f"  disengage lateral on brake: {_disengage_lateral_on_brake}")

  if segment:
    init_segment(safety, lr, safety_mode, param)
    lr.reset()

  rx_tot, rx_invalid, tx_tot, tx_blocked, tx_controls, tx_controls_lat, tx_controls_blocked, tx_controls_lat_blocked, mads_mismatch = 0, 0, 0, 0, 0, 0, 0, 0, 0
  safety_tick_rx_invalid = False
  blocked_addrs = Counter()
  invalid_addrs = set()

  # Track last good state for each address
  last_good_states = defaultdict(lambda: {
    'timestamp': None,
    **{var: None for var in DEBUG_VARS}
  })

  can_msgs = [m for m in lr if m.which() in ('can', 'sendcan')]
  start_t = can_msgs[0].logMonoTime
  end_t = can_msgs[-1].logMonoTime
  for msg in can_msgs:
    safety.set_timer((msg.logMonoTime // 1000) % 0xFFFFFFFF)

    # skip start and end of route, warm up/down period
    if msg.logMonoTime - start_t > 1e9 and end_t - msg.logMonoTime > 1e9:
      safety.safety_tick_current_safety_config()
      safety_tick_rx_invalid |= not safety.safety_config_valid() or safety_tick_rx_invalid

    if msg.which() == 'sendcan':
      for canmsg in msg.sendcan:
        to_send = package_can_msg(canmsg)
        sent = safety.safety_tx_hook(to_send)

        # mismatched
        if (safety.get_controls_allowed() and not safety.get_controls_allowed_lat()):
          mads_mismatch += 1
          print(f"controls allowed but not controls allowed lat [{mads_mismatch}]")
          print(f"msg:{canmsg.address} ({hex(canmsg.address)})")
          for var, getter in DEBUG_VARS.items():
            print(f"  {var}: {getter(safety)}")

        if not sent:
          tx_blocked += 1
          tx_controls_blocked += safety.get_controls_allowed()
          tx_controls_lat_blocked += safety.get_controls_allowed_lat()
          blocked_addrs[canmsg.address] += 1

          if "DEBUG" in os.environ:
            last_good = last_good_states[canmsg.address]
            print(f"\nBlocked message at {(msg.logMonoTime - start_t) / 1e9:.3f}s:")
            print(f"Address: {hex(canmsg.address)} (bus {canmsg.src})")
            print("Current state:")
            for var, getter in DEBUG_VARS.items():
              print(f"  {var}: {getter(safety)}")

            if last_good['timestamp'] is not None:
              print(f"\nLast good state ({last_good['timestamp']:.3f}s):")
              for var in DEBUG_VARS:
                print(f"  {var}: {last_good[var]}")
            else:
              print("\nNo previous good state found for this address")
            print("-" * 80)
        else:  # Update last good state if message is allowed
          last_good_states[canmsg.address].update({
            'timestamp': (msg.logMonoTime - start_t) / 1e9,
            **{var: getter(safety) for var, getter in DEBUG_VARS.items()}
          })

        tx_controls += safety.get_controls_allowed()
        tx_controls_lat += safety.get_controls_allowed_lat()
        tx_tot += 1
    elif msg.which() == 'can':
      # ignore msgs we sent
      for canmsg in filter(lambda m: m.src < 128, msg.can):
        to_push = package_can_msg(canmsg)
        recv = safety.safety_rx_hook(to_push)
        if not recv:
          rx_invalid += 1
          invalid_addrs.add(canmsg.address)
        rx_tot += 1

  print("\nRX")
  print("total rx msgs:", rx_tot)
  print("invalid rx msgs:", rx_invalid)
  print("safety tick rx invalid:", safety_tick_rx_invalid)
  print("invalid addrs:", invalid_addrs)
  print("\nTX")
  print("total openpilot msgs:", tx_tot)
  print("total msgs with controls allowed:", tx_controls)
  print("total msgs with controls_lat allowed:", tx_controls_lat)
  print("blocked msgs:", tx_blocked)
  print("blocked with controls allowed:", tx_controls_blocked)
  print("blocked with controls_lat allowed:", tx_controls_lat_blocked)
  print("blocked addrs:", blocked_addrs)
  print("mads enabled:", safety.get_enable_mads())

  return tx_controls_blocked == 0 and tx_controls_lat_blocked == 0 and rx_invalid == 0 and not safety_tick_rx_invalid


if __name__ == "__main__":
  from openpilot.tools.lib.logreader import LogReader

  parser = argparse.ArgumentParser(description="Replay CAN messages from a route or segment through a safety mode",
                                   formatter_class=argparse.ArgumentDefaultsHelpFormatter)
  parser.add_argument("route_or_segment_name", nargs='+')
  parser.add_argument("--mode", type=int, help="Override the safety mode from the log")
  parser.add_argument("--param", type=int, help="Override the safety param from the log")
  parser.add_argument("--alternative-experience", type=int, help="Override the alternative experience from the log")
  args = parser.parse_args()

  lr = LogReader(args.route_or_segment_name[0])

  if None in (args.mode, args.param, args.alternative_experience):
    for msg in lr:
      if msg.which() == 'carParams':
        if args.mode is None:
          args.mode = msg.carParams.safetyConfigs[-1].safetyModel.raw
        if args.param is None:
          args.param = msg.carParams.safetyConfigs[-1].safetyParam
        if args.alternative_experience is None:
          args.alternative_experience = msg.carParams.alternativeExperience
        break
    else:
      raise Exception("carParams not found in log. Set safety mode and param manually.")

    lr.reset()

  print(f"replaying {args.route_or_segment_name[0]} with safety mode {args.mode}, param {args.param}, alternative experience {args.alternative_experience}")
  replay_drive(lr, args.mode, args.param, args.alternative_experience, segment=len(lr.logreader_identifiers) == 1)
