import logging
import pyvisa
import pyvisa.constants
import time

import vxi11_2_base

#region Logging setup
# Configure logging, and set global log level (for pyvisa etc)
LOG_LEVEL = logging.INFO # DEBUG, INFO, WARNING, ERROR, CRITICAL

logging.basicConfig(
    level=LOG_LEVEL,
    format='%(asctime)s - %(levelname)s - %(name)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)
# Set log level for this module
logger.setLevel(LOG_LEVEL)
#endregion  

class VXI11_2_delay(vxi11_2_base.VXI11_2_Base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Delay: write", "Delay: read", "Delay: reply"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests

        inst = context["inst"]
        cmd_setup = context["cmd_setup"]
        delay_time_ms = context["delay_time_ms"]
        cmd_test = context["cmd_test"]
        expected_reply = context["expected_reply"]
        
        inst.clear()  # clear the device buffers, and error queue, and timing stuff

        inst.write(cmd_setup)
        
        original_timeout = inst.timeout
        expected_delay = original_timeout
        min_delay = 10  # minimum delay in milliseconds
        if test == 0:
            expected_delay = delay_time_ms + 100  # add some slack for the communication overhead
            min_delay = (delay_time_ms * 0.9) + 100
        inst.timeout = expected_delay
        timer_start = time.time()
        try:
            inst.write(cmd_test)
            cmd_duration = (time.time() - timer_start) * 1000  # convert to milliseconds
            if cmd_duration < min_delay:
                self.logger.warning(f"{testname}: instrument nr {inst_nr}: write completed too quickly: {cmd_duration:.2f} ms, expected minimum delay: {min_delay} ms")
        except pyvisa.errors.VisaIOError as e:
            cmd_duration = (time.time() - timer_start) * 1000  # convert to milliseconds
            self.logger.error(f"{testname}: instrument nr {inst_nr}: write failed with error: {e} after {cmd_duration:.2f} ms, expected delay: {expected_delay} ms")
            inst.timeout = original_timeout
            inst.clear()  # clear the device buffers, and error queue, and timing stuff
            return False
        
        if test == 1 or test == 2:
            expected_delay = delay_time_ms + 100  # add some slack for the communication overhead
            min_delay = (delay_time_ms * 0.9) + 100
        else:
            expected_delay = original_timeout
            min_delay = 10  # minimum delay in milliseconds
        inst.timeout = expected_delay
        timer_start = time.time()
        reply = ""
        try:
            reply = inst.read().strip()
            cmd_duration = (time.time() - timer_start) * 1000  # convert to milliseconds
            if cmd_duration < min_delay:
                self.logger.warning(f"{testname}: instrument nr {inst_nr}: read completed too quickly: {cmd_duration:.2f} ms, expected minimum delay: {min_delay} ms")
        except pyvisa.errors.VisaIOError as e:
            cmd_duration = (time.time() - timer_start) * 1000  # convert to milliseconds
            self.logger.error(f"{testname}: instrument nr {inst_nr}: read failed with error: {e} after {cmd_duration:.2f} ms, expected delay: {expected_delay} ms")
            inst.timeout = original_timeout
            inst.clear()  # clear the device buffers, and error queue, and timing stuff
            return False
        
        if reply != expected_reply:
            self.logger.error(f"{testname}: instrument nr {inst_nr}: expected reply: {expected_reply}, got: {reply}")
            inst.clear()
            return False
        
        inst.clear()  # clear the device buffers, and error queue, and timing stuff
        
        return self.check_errors(inst_nr, context)

    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        ret = super().get_instrument_commands(inst_nr, idn, test)
        cmd_setup = ""
        delay_time_ms = 0
        cmd_test = ""
        expected_reply = ""
        if "ieee488_device" in idn:
            if test == 0:
                # write test
                nr_of_bytes = 1000
                inter_char_delay_ms = 5
                delay_time_ms = nr_of_bytes * max(0.05, inter_char_delay_ms)  # delay time is the time it takes to send the data, but at least 50us per character
                cmd_setup = f"SLOWWR {inter_char_delay_ms}"
                cmd_test = "LONGWR? "
                expected_len = nr_of_bytes - len(cmd_test) - 2
                if (expected_len <= 0):
                    self.logger.error(f"instrument nr {inst_nr}: expected length for write test is too small: {expected_len}")
                    return {}
                data = "".join(chr(0x30 + (i % (0x7E - 0x30 + 1))) for i in range(expected_len))
                if len(data) != expected_len:
                    self.logger.error(f"Failed to create data of length {expected_len}, got length {len(data)}")
                    return {}
                cmd_test = cmd_test + data
                expected_reply = f"{expected_len},48,\"\""
            if test == 1:
                # read test
                nr_of_bytes = 1000
                inter_char_delay_ms = 5
                delay_time_ms = nr_of_bytes * max(0.05, inter_char_delay_ms)  # delay time is the time it takes to send the data, but at least 50us per character
                cmd_setup = f"SLOWRD {inter_char_delay_ms}"
                expected_len = nr_of_bytes - 2
                cmd_test = f"LONGRD? {expected_len}"
                data = "".join(chr(0x30 + (i % (0x7E - 0x30 + 1))) for i in range(expected_len))
                if len(data) != expected_len:
                    self.logger.error(f"Failed to create data of length {expected_len}, got length {len(data)}")
                    return {}
                expected_reply = data
            if test == 2:
                # reply test
                nr_of_bytes = 1000
                delay_time_ms =5000
                cmd_setup = f"DELAYRD {delay_time_ms - (nr_of_bytes * 0.05) - 80}"  # delay time reply delay + the time it takes to send the data (at least 100us per character)
                expected_len = nr_of_bytes - 2
                cmd_test = f"LONGRD? {expected_len}"
                data = "".join(chr(0x30 + (i % (0x7E - 0x30 + 1))) for i in range(expected_len))
                if len(data) != expected_len:
                    self.logger.error(f"Failed to create data of length {expected_len}, got length {len(data)}")
                    return {}
                expected_reply = data
        if len(cmd_test) == 0:
            # self.logger.warning(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for end condition tests")
            return {}
        ret.update({
                    "cmd_setup": cmd_setup,
                    "delay_time_ms": delay_time_ms,
                    "cmd_test": cmd_test, 
                    "expected_reply": expected_reply
                    })
        return ret
    
#endregion
#region private helpers
    ###############################################################################################

#endregion
#region the tests
    ###############################################################################################
            

    
#endregion

