import logging

import vxi11_2_base
from vxi11_2_helpers import ieee488_device_longwr_query

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

class VXI11_2_longwr(vxi11_2_base.VXI11_2_Base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Long write"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests
        cmd_write = context["cmd_write"]
        check_function = context["check_function"]
        check_len = context["check_len"]
        expected_reply = context.get("expected_reply", "")      
        
        self.logger.debug(f"{testname}: instrument nr {inst_nr}: sending command: {cmd_write}")
        if check_function is not None:
            if not check_function(inst_nr, context, cmd_write, check_len, expected_reply):
                self.logger.error(f"{testname}: instrument nr {inst_nr}: reply check failed for command: {cmd_write}")
                return False
        else:
            self.logger.error(f"{testname}: instrument nr {inst_nr}: no check_function defined")
            return False
        return self.check_errors(inst_nr, context)

    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        ret = super().get_instrument_commands(inst_nr, idn, test)
        cmds_init = ret["cmds_init"][:]  # make a copy of the list, so that I only overwrite the commands I want to change, and keep the rest of the commands from the base class
        cmd_write = ""
        check_function = None
        check_len = 0
        expected_reply = ""
        if "ieee488_device" in idn:
            datalen = 500000
            cmd_write, expected_reply = ieee488_device_longwr_query(datalen)
            check_function = self.check_ieee488_device
            check_len = datalen

        if check_function is None:
            # self.logger.warning(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for long write test")
            return {}
        
        ret.update({"cmds_init": cmds_init, "cmd_write": cmd_write, "check_function": check_function, "check_len": check_len, "expected_reply": expected_reply})
        return ret
    
#endregion
#region private helpers
    ###############################################################################################

#endregion
#region the tests
    ###############################################################################################
            
    def check_ieee488_device(self, inst_nr: int, context: dict, cmd_write: str, expected_len: int, expected_reply: str) -> bool:
        """Check the ieee488_device instrument.
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_write: The command to be sent to the instrument to write the data
        :type cmd_write: str
        :param expected_len: The expected length
        :type expected_len: int
        :param expected_reply: The expected reply from the instrument
        :type expected_reply: str
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # A custom message with a length of expected_len is sent to the instrument
        # The custom message is a string of sequential characters, 
        # starting with '0' and wrapping around to '0' after '~'
        # The reply must be '{expected_len},48,""'
        inst = context["inst"]
        # allow 1ms per character in device debug mode
        # else: 100us per character on a atmega4809, with overhead
        # timeout is in ms
        inst.timeout = expected_len * 0.15  # non-debug mode
        if self.options.get("auto_chunk_size", False):
            if expected_len > 20000:
                inst.chunk_size = expected_len + 1000  # increase chunk size for large reads
                self.logger.debug(f"instrument nr {inst_nr}: auto chunk size enabled, set chunk size to {inst.chunk_size} for expected length {expected_len}")                
        try:
            reply = inst.query(cmd_write)
            self.logger.debug(f"instrument nr {inst_nr}: received reply: {reply}")
            reply = reply.strip()
            if reply != expected_reply:
                self.logger.error(f"Reply \"{reply}\" does not match expected reply \"{expected_reply}\"")
                return False
            return True
        except ValueError as e:
            self.logger.error(f"Failed to parse reply: {e}")
            return False
    
#endregion

