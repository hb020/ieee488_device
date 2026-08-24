import logging

from typing import Optional
import pyvisa
import pyvisa.constants
import datetime
import visadevice_base

# region Logging setup
# Configure logging, and set global log level (for pyvisa etc)
LOG_LEVEL = logging.INFO  # DEBUG, INFO, WARNING, ERROR, CRITICAL

logging.basicConfig(
    level=LOG_LEVEL, format="%(asctime)s - %(levelname)s - %(name)s - %(message)s", handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)
# Set log level for this module
logger.setLevel(LOG_LEVEL)
# endregion


# findings:
# 
# in general: timing is not always respected. Lock timing is seen as minimal time. 
# It can take significantly longer than the requested timeout, but should not be shorter than the requested timeout.
# KS 34465A respect timing rather well, DMM6500 take easily 300%.
#
# socket: no locking support, and will not return an error when trying to open a locked resource.
# vxi-11:
#  - pyvisa-py 0.8.1: 
#       - does not support proper locking timing, it is always 10secs
#       - does not respect lock in create_link
#       - does not set lockDevice=True on device_lock
#  - NI-Visa: OK. But 
#       - `viOpen(AccessModes.exclusive_lock: 1)` is done with a separate calls to `viOpen()` and `viLock()` by NI-Visa
#       - Once the instrument is locked, further lock handling is done inside NI-Visa
# hislip:
#  - pyvisa-py 0.8.1: locking not supported
#  - NI-Visa: timing issue, and sometimes we have TMO instead of RSRC_LOCKED, but that is probably a timing issue. 

class visadevice_lock(visadevice_base.visadevice_base):
    # region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Lock: upon open", "Lock: after open"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        resource_name = context["resource_name"]
        self.close_instrument(inst_nr)
        if test == 0:
            return self.test_locking(resource_name, testname, lock_on_open=True)
        elif test == 1:
            return self.test_locking(resource_name, testname, lock_on_open=False)
        else:
            self.logger.error(f"{testname}: Unknown test {test}")
            return False
    
    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        if self.visa_type in ["socket"]:
            # cannot do lock stuff over socket
            return { "reason": f"Locking is not supported on {self.visa_type} instruments" }
        
        return super().get_instrument_commands(inst_nr, idn, test)

    def try_open_with_lock(
        self, testname: str, resource_name: str, lock_on_open: bool, lock_after_open: bool, expect_to_fail: bool, timeout: int
    ) -> tuple[bool, Optional[pyvisa.resources.Resource]]:
        inst = None
        if self.rm is None:
            self.logger.error(f"{testname}: Resource manager is not initialized, cannot open resource {resource_name}")
            return False, None
        am_locked = False
        am_opened = False
        try:
            if lock_on_open:
                # lock on open
                self.logger.debug(f"{testname}: Trying to open+lock resource {resource_name} with timeout {timeout} ms")
                inst = self.rm.open_resource(
                    resource_name, open_timeout=timeout, access_mode=pyvisa.constants.AccessModes.exclusive_lock
                )
                am_opened = True
                am_locked = True
            elif lock_after_open:
                self.logger.debug(f"{testname}: Trying to open and then lock resource {resource_name} with timeout {timeout} ms")
                inst = self.rm.open_resource(resource_name)
                am_opened = True
                inst.timeout = timeout * 1.5
                inst.lock_excl(timeout=timeout)
                am_locked = True
            else:
                self.logger.debug(f"{testname}: Trying to open resource {resource_name} with timeout {timeout} ms")
                inst = self.rm.open_resource(resource_name)
                am_opened = True
                inst.timeout = timeout * 1.5
                if self.visa_provider == "pyvisa-py" and (self.visa_type == "vxi11" or self.visa_type == "gateway"):
                    self.rm.visalib.sessions[inst.session].lock_timeout = timeout
                am_locked = False
                r = inst.query("*IDN?")
            if expect_to_fail:
                # self.logger.error(f"{testname}: Opened an already locked resource, but should have failed.")
                try:
                    if am_locked and inst is not None:
                        inst.unlock()
                    if inst is not None:
                        inst.close()
                except Exception as e:
                    pass
                return False, None
            else:
                return True, inst
        except Exception as e:
            if (
                expect_to_fail
                and hasattr(e, "error_code")
                and (
                    e.error_code == pyvisa.constants.VI_ERROR_RSRC_LOCKED
                    or e.error_code == pyvisa.constants.VI_ERROR_RSRC_BUSY
                    or e.error_code == pyvisa.constants.VI_ERROR_TMO
                )
            ):
                # print("SUCCESS: Failed to open locked resource, as expected.")
                if e.error_code != pyvisa.constants.VI_ERROR_RSRC_LOCKED:
                    e_wanted = "VI_ERROR_RSRC_LOCKED"
                    e_got = e.error_code
                    if e.error_code == pyvisa.constants.VI_ERROR_RSRC_BUSY:
                        e_got = "VI_ERROR_RSRC_BUSY"
                    elif e.error_code == pyvisa.constants.VI_ERROR_TMO:
                        e_got = "VI_ERROR_TMO"
                    self.logger.warning(
                        f"{testname}: Opening an already locked resource did not return the expected error code. Got: {e_got}, expected: {e_wanted}"
                    )                    
                return True, None
            else:
                # exception: close it
                if am_locked and inst is not None:
                    inst.unlock()
                if inst is not None:
                    try:
                        inst.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
                    except Exception as e2:
                        pass
                    inst.close()
                    inst = None
                if expect_to_fail:
                    if am_opened:
                        self.logger.error(f"{testname}: {e}")
                        return False, None
                    else:
                        self.logger.warning(
                            f"{testname}: Opening an already locked resource did not return the correct error. Got: {e}"
                        )
                        return True, None
                else:
                    if am_opened:
                        self.logger.error(f"{testname}: Failed to lock resource (unexpected): {e}")
                    else:
                        self.logger.error(f"{testname}: Failed to open resource (unexpected): {e}")
                    return False, None
                
    def test_locking(self, resource_name: str, testname: str, lock_on_open: bool) -> bool:
        success, inst1 = self.try_open_with_lock(
            testname, resource_name, lock_on_open=lock_on_open, lock_after_open=True, expect_to_fail=False, timeout=1000
        )
        if not success:
            self.logger.debug("{testname}: Error on first open and lock, cannot continue with test.")
            return False

        timeout = 1  # seconds
        start_time = datetime.datetime.now()
        success, inst2 = self.try_open_with_lock(
            testname, resource_name, lock_on_open=False, lock_after_open=True, expect_to_fail=True, timeout=timeout * 1000
        )
        end_time = datetime.datetime.now()

        duration_secs = (end_time - start_time).total_seconds()
        if success:
            desired_min_duration = timeout * 0.8  # Allowing a 20% margin for timing variations
            desired_max_duration = timeout * 1.5  # Allowing a 50% margin for timing variations
            if duration_secs < desired_min_duration:
                self.logger.warning(
                    f"{testname}: Double Locking rejection was respected, but faster than expected: {duration_secs:.1f} seconds (< {desired_min_duration:.1f} seconds). This might indicate a problem with the locking mechanism."
                )
            elif duration_secs > desired_max_duration:
                self.logger.warning(
                    f"{testname}: Double Locking rejection was respected, but slower than expected: {duration_secs:.1f} seconds (> {desired_max_duration:.1f} seconds). This might indicate a problem with the locking mechanism."
                )
            else:
                self.logger.debug(f"{testname}: Double Locking rejection wait duration: {duration_secs:.1f} seconds")
        else:
            self.logger.error(
                f"{testname}: Double Locking rejection was not respected, and took duration {duration_secs:.1f} seconds while a lock timeout of {timeout:.1f} seconds was requested."
            )
            
        timeout = 1  # seconds
        start_time = datetime.datetime.now()
        success, inst3 = self.try_open_with_lock(
            testname, resource_name, lock_on_open=False, lock_after_open=False, expect_to_fail=True, timeout=timeout * 1000
        )
        end_time = datetime.datetime.now()

        duration_secs = (end_time - start_time).total_seconds()
        if success:
            desired_min_duration = timeout * 0.8  # Allowing a 20% margin for timing variations
            desired_max_duration = timeout * 1.5  # Allowing a 50% margin for timing variations
            if duration_secs < desired_min_duration:
                self.logger.warning(
                    f"{testname}: Locking was respected, but faster than expected: {duration_secs:.1f} seconds (< {desired_min_duration:.1f} seconds). This might indicate a problem with the locking mechanism."
                )
            elif duration_secs > desired_max_duration:
                self.logger.warning(
                    f"{testname}: Locking was respected, but slower than expected: {duration_secs:.1f} seconds (> {desired_max_duration:.1f} seconds). This might indicate a problem with the locking mechanism."
                )
            else:
                self.logger.debug(f"{testname}: Locking wait duration: {duration_secs:.1f} seconds")
        else:
            self.logger.error(
                f"{testname}: Lock was not respected, and took duration {duration_secs:.1f} seconds while a lock timeout of {timeout:.1f} seconds was requested."
            )
            
        # close down
        if inst1 is not None:
            try:
                inst1.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
            except Exception as e:
                pass            
            inst1.close()
        if inst2 is not None:
            try:
                inst2.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
            except Exception as e:
                pass            
            inst2.close()
        if inst3 is not None:
            try:
                inst3.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
            except Exception as e:
                pass            
            inst3.close()
        return success
