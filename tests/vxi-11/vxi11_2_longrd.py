import logging

import vxi11_2_base
from vxi11_2_helpers import ieee488_device_longrd_query, str_diff

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

class VXI11_2_longrd(vxi11_2_base.VXI11_2_Base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Long read"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests
        cmd_read = context["cmd_read"]
        check_function = context["check_function"]
        check_len = context["check_len"]
        expected_reply = context.get("expected_reply", "")
        
        self.logger.debug(f"{testname}: instrument nr {inst_nr}: sending command: {cmd_read}")
        if check_function is not None:
            if not check_function(inst_nr, context, cmd_read, check_len, expected_reply):
                self.logger.error(f"{testname}: instrument nr {inst_nr}: reply check failed for command: {cmd_read}")
                return False
        else:
            self.logger.error(f"{testname}: instrument nr {inst_nr}: no check_function defined")
            return False
        return self.check_errors(inst_nr, context)

    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        ret = super().get_instrument_commands(inst_nr, idn, test) 
        cmds_init = ret["cmds_init"][:]  # make a copy of the list, so that I only overwrite the commands I want to change, and keep the rest of the commands from the base class
        cmd_read = ""
        check_function = None  # mandatory
        check_len = 0          # only needed when the check_function needs it.
        expected_reply = ""    # only needed when the check_function needs it.
        if "66332A" in idn:
            datalen = 800
            cmds_init = ["*CLS", "INIT:CONT:SEQ OFF", "SENS:FUNC \"VOLT\"", "TRIG:ACQ:SOUR BUS", "SENS:SWE:TINT 15.6E-6", f"SENSE:SWEEP:POINTS {datalen}"]
            cmd_read = "MEAS:ARRAY:VOLT?"
            check_function = self.check_66332A
            check_len = datalen
        if "HP859" in idn:
            cmd_read = "USTATE?"
            check_function = self.check_hp8590
            check_len = 0           
        if "ieee488_device" in idn:
            datalen = 500000
            cmd_read, expected_reply = ieee488_device_longrd_query(datalen)
            check_function = self.check_ieee488_device
            check_len = datalen

        if check_function is None:
            # self.logger.warning(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for long read test")
            return {}
        
        ret.update({"cmds_init": cmds_init, "cmd_read": cmd_read, "check_function": check_function, "check_len": check_len, "expected_reply": expected_reply})
        return ret
    
#endregion
#region private helpers
    ###############################################################################################

#endregion
#region the tests
    ###############################################################################################
    
    def check_66332A(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int, expected_reply: str) -> bool:
        """Check the 66332A instrument.

        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :param expected_reply: The expected reply from the instrument
        :type expected_reply: str
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # The reply should be a comma-separated list of floats, with length equal to expected_len
        try:
            context["inst"].timeout = expected_len * 4  # this can be slow
            reply = context["inst"].query_ascii_values(cmd_read)
            self.logger.debug(f"instrument nr {inst_nr}: received reply: {reply}")
            if len(reply) != expected_len:
                self.logger.error(f"Reply length {len(reply)} does not match expected length {expected_len}")
                return False
            return True
        except ValueError as e:
            self.logger.error(f"Failed to parse reply: {e}")
            return False
        
    def check_ieee488_device(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int, expected_reply: str) -> bool:
        """Check the ieee488_device instrument.
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :param expected_reply: The expected reply from the instrument
        :type expected_reply: str
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # The reply should be a custom sequential list of length expected_len
        inst = context["inst"]
        # allow 1ms per character in device debug mode
        # else: 140us per character on a atmega4809, with overhead
        # timeout is in ms
        inst.timeout = expected_len * 0.2  # non-debug mode
        expected_len = len(expected_reply)
        if self.options.get("auto_chunk_size", False):
            if expected_len > 20000:
                inst.chunk_size = expected_len + 1000  # increase chunk size for large reads
                self.logger.debug(f"instrument nr {inst_nr}: auto chunk size enabled, set chunk size to {inst.chunk_size} for expected length {expected_len}")
        try:
            reply = inst.query(cmd_read)
            self.logger.debug(f"instrument nr {inst_nr}: received reply: {reply}")
            reply = reply.strip()
            if not reply or len(reply) == 0:
                self.logger.error(f"Reply is empty")
                return False
            if len(reply) != expected_len:
                self.logger.error(f"Reply length {len(reply)} does not match expected length {expected_len}")
                return False
            if reply != expected_reply:
                self.logger.error(f"Reply does not match expected reply. {str_diff(expected_reply, reply)}")
                return False
            return True
        except ValueError as e:
            self.logger.error(f"Failed to parse reply: {e}")
            return False
    
    def check_hp8590(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int, expected_reply: str) -> bool:
        """Check the HP8590 instrument.
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :param expected_reply: The expected reply from the instrument
        :type expected_reply: str
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # Implement the actual check for HP8590 replies here
        inst = context["inst"]
        try:
            readresult = inst.query_binary_values(cmd_read, datatype="B", header_fmt="hp", is_big_endian=True, expect_termination=True)
            # TODO we might add timing check and length check
        except Exception as e:
            self.logger.error(f"instrument nr {inst_nr}: failed to read binary values: {e}")
            return False
        return True
#endregion

