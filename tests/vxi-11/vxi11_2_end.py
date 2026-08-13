import logging
import pyvisa
import pyvisa.constants
from vxi11_2_helpers import ieee488_device_longrd_query, str_diff

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

class VXI11_2_end(vxi11_2_base.VXI11_2_Base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["End conditions: EOS", "End conditions: EOI", "End conditions: Count"]

    def set_instrument_to_eos(self, inst, cmd_goto_eos):
        if len(cmd_goto_eos) > 0:
            inst.write(cmd_goto_eos)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, True)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR, ord("\n"))
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SEND_END_EN, False)
        try:
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SUPPRESS_END_EN, True)
        except:
            # some backends do not support this attribute, so ignore the error
            pass

    def set_instrument_to_eoi(self, inst, cmd_goto_eoi):
        inst.write(cmd_goto_eoi)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, False)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SEND_END_EN, True)
        try:                
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SUPPRESS_END_EN, False)
        except:
            # some backends do not support this attribute, so ignore the error
            pass

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests

        inst = context["inst"]
        cmd_goto_eos = context["cmd_goto_eos"]
        cmd_goto_eoi = context["cmd_goto_eoi"]
        cmd_test = context["cmd_test"]
        expected_reply = context["expected_reply"]
        
        
        # - write from client to device:
        #   when using EOI:
        #     - VXI-11.2 client: adds 'end flag' to device_write
        #     - Gateway: adds EOI to the end of the message
        #     - Device: listens to EOI during read
        #   when not using EOI, but using EOS:
        #     - VXI-11.2 client: TODO: pyvisa-py will add 'end flag' to device_write, NI-VISA also ??
        #     - Gateway: adds EOI to the end of the message. The message may contain an EOS character, but the gateway will not use it to determine end of message
        #     - Device: listens to EOI AND EOS during read
        #
        # - read of client from device:
        #   when using EOI:
        #     - VXI-11.2 client: does not specify an EOS character in command (can specify a max length)
        #     - Gateway: asks device to talk, no extra information
        #     - Device: adds EOI to the end of the message
        #     - Gateway: intercepts EOI or number of characters and indicates the reason (be it EOI, or length) as end condition to client
        #     - Client: receives the reason (be it EOI, or length), and stops reading
        #   when not using EOI, but using EOS:
        #     - VXI-11.2 client: specifies EOS character in command (and can specify a max length)
        #     - Gateway: asks device to talk, no extra information
        #     - Device: adds EOS to the end of the message, but no EOI
        #     - Gateway: intercepts either EOI or EOS or number of characters, and sends the earliest detected end condition as the end reason to the client
        #     - Client: receives the reason (be it EOI, EOS, or length), and stops reading
        
        
        # Note: in all "read of client from device" cases, the client can request a maximum size, and the 
        #       gateway should set an end reason if the number of bytes is received. 
        #       But pyvisa-py does not support that reason code explicitly, as it does its own counting.
        #       TODO NI-VISA support it?
        inst.write_terminator = "\n"
        inst.read_terminator = "\n"
        
        if test == 0:
            # EOS case
            rv = True
            try:
                self.set_instrument_to_eos(inst, cmd_goto_eos)
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii")
                expected = expected_reply + "\n"
                if r != expected:
                    self.logger.error(f"instrument nr {inst_nr}: EOS test failed: expected \"{expected}\", got \"{r}\"")
                    rv = False
            except Exception as e:
                self.logger.error(f"instrument nr {inst_nr}: EOS test raised an exception: {e}")
                rv = False
                
            self.set_instrument_to_eoi(inst, cmd_goto_eoi)
            return rv

        if test == 1:
            # normal case (EOI)
            try:
                self.set_instrument_to_eoi(inst, cmd_goto_eoi)
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii")
                expected = expected_reply
                r = r.rstrip() # remove the trailing newline, as the EOI case does not have a newline
                if r != expected:
                    self.logger.error(f"instrument nr {inst_nr}: EOI test failed: expected \"{expected}\", got \"{r}\"")
                    return False          
                
            except Exception as e:
                self.logger.error(f"instrument nr {inst_nr}: EOI test raised an exception: {e}")
                return False
            
        if test == 2:
            try:
                self.set_instrument_to_eoi(inst, cmd_goto_eoi)    
                # do it once to get the normal length
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii").rstrip() # remove the trailing newline, as the EOI case does not have a newline
                # now ask the same, just with a smaller length
                inst.write(cmd_test)                                
                r = inst.read_bytes(len(r)-5).decode("ascii") # read one less character than the previous read, to test that the read stops at EOI
                expected = expected_reply[:-5] # remove the last 5 characters from the expected reply, to match the read length
                if r != expected:
                    self.logger.error(f"instrument nr {inst_nr}: length test failed: expected \"{expected}\", got \"{r}\"")
                    return False
            except Exception as e:
                self.logger.error(f"instrument nr {inst_nr}: length test raised an exception: {e}")
                return False

            old_timeout = inst.timeout
            try:
                inst.timeout = 100 # set a short timeout to test that the read stops at EOI
                inst.read_raw()
            except Exception as e:
                self.logger.error(f"instrument nr {inst_nr}: flush raised an exception: {e}")
                return False
            finally:
                inst.timeout = old_timeout
        
        # Attributes for Read and Write:
        # VI_ATTR_TERMCHAR_EN
        # VI_ATTR_TERMCHAR
        # VI_ATTR_SEND_END_EN (not used by early pyvisa-py, but used by NI-VISA)
        
        # Attributes for Read
        # VI_ATTR_SUPPRESS_END_EN (not supported by early pyvisa-py nor early NI-Visa)
        
        return self.check_errors(inst_nr, context)

    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        ret = super().get_instrument_commands(inst_nr, idn, test)
        cmds_init = ret["cmds_init"][:]  # make a copy of the list, so that I only overwrite the commands I want to change, and keep the rest of the commands from the base class
        cmd_goto_eos = ""  # leave empty for instruments that do not support EOS, so that the test will be skipped for those instruments
        cmd_goto_eoi = ""  # may be empty. EOI tests will always be done.
        cmd_test = ""
        expected_reply = ""
        if "ieee488_device" in idn:
            cmd_goto_eos = "EOS 10"
            cmd_goto_eoi = "EOS"
            expected_len = 2000
            cmd_test, expected_reply = ieee488_device_longrd_query(expected_len)
        if "IDN-SGLT-PRI" in idn: # dummy device tests
            cmd_goto_eos = ""
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "IDN-SGLT-PRI SDG0000X"
        if "66332A" in idn:
            cmd_goto_eos = ""
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "HEWLETT-PACKARD,66332A,0,A.01.03"
        if "6634B" in idn:
            cmd_goto_eos = ""
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "HEWLETT-PACKARD,6634B,0,A.01.04"
        if "HP859" in idn:
            cmd_goto_eos = ""
            cmd_goto_eoi = ""
            cmd_test = "*ID?"
            expected_reply = "HP8594E"
        if test == 0 and (len(cmd_test) == 0 or len(cmd_goto_eos) == 0):
            # self.logger.warning(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for EOS test")
            return {}
        if len(cmd_test) == 0:
            # self.logger.warning(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for end condition tests")
            return {}
        ret.update({
                    "cmds_init": cmds_init, 
                    "cmd_goto_eos": cmd_goto_eos, 
                    "cmd_goto_eoi": cmd_goto_eoi, 
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

