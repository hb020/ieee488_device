import logging
import pyvisa
import pyvisa.constants

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
        return ["End conditions: EOS/EOI/Count"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests

        inst = context["inst"]
        cmd_goto_eos = context["cmd_goto_eos"]
        cmd_goto_eoi = context["cmd_goto_eoi"]
        cmd_test = context["cmd_test"]
        cmd_expected_reply = context["cmd_expected_reply"]
        
        
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
        
        # EOS case
        inst.write(cmd_goto_eos)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, True)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR, ord("\n"))
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SEND_END_EN, False)
        try:
            inst.write(cmd_test)
            r = inst.read_raw().decode("ascii")
            expected = cmd_expected_reply + "\n"
            if r != expected:
                self.logger.error(f"instrument nr {inst_nr}: EOS test failed: expected \"{expected}\", got \"{r}\"")
                return False
        except Exception as e:
            self.logger.error(f"instrument nr {inst_nr}: EOS test raised an exception: {e}")
            return False
        
        # normal case (EOI)
        try:
            inst.write(cmd_goto_eoi)
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, False)
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SEND_END_EN, True)
            inst.write(cmd_test)
            r = inst.read_raw().decode("ascii")
            expected = cmd_expected_reply
            r = r.rstrip() # remove the trailing newline, as the EOI case does not have a newline
            if r != expected:
                self.logger.error(f"instrument nr {inst_nr}: EOI test failed: expected \"{expected}\", got \"{r}\"")
                return False
        except Exception as e:
            self.logger.error(f"instrument nr {inst_nr}: EOI test raised an exception: {e}")
            return False
        
        try:
            inst.write(cmd_test)
            r = inst.read_bytes(len(r)-5).decode("ascii") # read one less character than the previous read, to test that the read stops at EOI
            expected = cmd_expected_reply[:-5] # remove the last 5 characters from the expected reply, to match the read length
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
        
        # The following are read/write attributes
        # VI_ATTR_TERMCHAR_EN
        # VI_ATTR_TERMCHAR
        # VI_ATTR_SEND_END_EN (not used by pyvisa-py, but used by NI-VISA)
        
        # VI_ATTR_SUPPRESS_END_EN is not valid for TCPIP INSTR resources (only Serial INSTR, TCPIP SOCKET, USB RAW, VXI INSTR)    

        # TODO: work on https://github.com/pyvisa/pyvisa-py/issues/609
        # TODO: add length check
        # TODO: add EOS characher check when using EOS
        
        return self.check_errors(inst_nr, context)

    def make_instrument_context(self, inst_nr: int, idn: str) -> dict:
        """Create and initialize the instrument context for the given instrument number.

        It must return at least a dict with the following keys:
        - "cmds_init": a list of commands to initialize the instrument for testing. This is not allowed to be empty.

        :param inst_nr: The instrument number
        :type inst_nr: int
        :param idn: The identification string of the instrument
        :type idn: str
        :return: a dict with all test specific information for the instrument
        :rtype: dict
        """
        ret = super().make_instrument_context(inst_nr, idn)
        cmds_init = ret["cmds_init"][:]  # make a copy of the list, so that I only overwrite the commands I want to change, and keep the rest of the commands from the base class
        cmd_goto_eos = ""
        cmd_goto_eoi = ""
        cmd_test = ""
        cmd_expected_reply = ""
        if "ieee488_device" in idn:
            cmd_goto_eos = "EOS 10"
            cmd_goto_eoi = "EOS"
            cmd_test = "LONGRD? 10"
            cmd_expected_reply = "0123456789"
        if "IDN-SGLT-PRI" in idn: # dummy device tests
            cmd_goto_eos = "EOS 10"
            cmd_goto_eoi = "EOS"
            cmd_test = "*IDN?"
            cmd_expected_reply = "IDN-SGLT-PRI SDG0000X"
        if len(cmd_test) == 0 or len(cmd_goto_eos) == 0 or len(cmd_goto_eoi) == 0:
            self.logger.error(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for end condition tests")
            return {}
        ret.update({
                    "cmds_init": cmds_init, 
                    "cmd_goto_eos": cmd_goto_eos, 
                    "cmd_goto_eoi": cmd_goto_eoi, 
                    "cmd_test": cmd_test, 
                    "cmd_expected_reply": cmd_expected_reply
                    })
        return ret
    
#endregion
#region private helpers
    ###############################################################################################

#endregion
#region the tests
    ###############################################################################################
            

    
#endregion

