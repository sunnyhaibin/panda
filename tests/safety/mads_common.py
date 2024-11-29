from abc import abstractmethod


class MadsCommonBase:
    @abstractmethod
    def _lkas_button_msg(self, enabled):
        raise NotImplementedError

    @abstractmethod
    def _acc_state_msg(self, enabled):
        raise NotImplementedError

    @abstractmethod
    def _mads_engage_msg(self, enabled):
        raise NotImplementedError

    def _test_lat_enabled_when_msg(self, msg, expected_enabled):
        self.safety.set_controls_allowed_lat(False)
        self._rx(msg)
        self.assertEqual(expected_enabled, self.safety.get_is_lat_active(), (
            f"(mads [{self.safety.get_enable_mads()}]) " +
            f"(acc_main_on [{self.safety.get_acc_main_on()}]) " +
            f"(temp_debug [{self.safety.get_temp_debug()}]) " +
            f"(controls_allowed [{self.safety.get_controls_allowed()}] lat [{self.safety.get_controls_allowed_lat()}]) " +
            f"(is_lat_active [{self.safety.get_is_lat_active()}]) " +
            f"(msg [{hex(msg.addr)}] bus: [{str(msg.bus)}]) lkas_button [{self.safety.get_lkas_button()}]"))

    # TODO-SP: We must also test disengagements. Right now we only really "validate" that we've engaged MADS under the conditions.

    def test_main_cruise_allows_lateral_control_when_mads_enabled(self):
        self._test_enable_lateral_control_via_acc_state(True, True, True)

    def test_main_cruise_prevents_lateral_control_when_mads_disabled(self):
        self._test_enable_lateral_control_via_acc_state(False, True, False)

    def test_non_main_cruise_prevents_lateral_control_when_mads_enabled(self):
        self._test_enable_lateral_control_via_acc_state(True, False, False)

    def _test_enable_lateral_control_via_acc_state(self, mads_enabled, valid_mads_engage, expected_enabled):
        self.safety.set_enable_mads(mads_enabled)
        self._test_lat_enabled_when_msg(self._acc_state_msg(valid_mads_engage), expected_enabled)

    def test_lkas_allows_lateral_control_when_mads_enabled(self):
        self._test_enable_lateral_control_via_lkas(True, True, True)

    def test_lkas_prevents_lateral_control_when_mads_disabled(self):
        self._test_enable_lateral_control_via_lkas(False, True, False)

    def test_non_lkas_prevents_lateral_control_when_mads_enabled(self):
        self._test_enable_lateral_control_via_lkas(True, False, False)

    def _test_enable_lateral_control_via_lkas(self, mads_enabled, valid_mads_engage, expected_enabled):
        self.safety.set_enable_mads(mads_enabled)
        self._test_lat_enabled_when_msg(self._lkas_button_msg(valid_mads_engage), expected_enabled)
