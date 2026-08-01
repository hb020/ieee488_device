from typing import Optional
import sys
import os
from time import sleep
import pyvisa
import pyvisa.constants
import logging

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

class VXI11_2_longrd(vxi11_2_base.VXI11_2_Base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Long read"]

    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        # Do the tests
        cmd_read = context["cmd_read"]
        check_reply_function = context["check_reply_function"]
        check_reply_len = context["check_reply_len"]        
        
        logger.debug(f"{testname}: instrument nr {inst_nr}: sending command: {cmd_read}")
        if check_reply_function is not None:
            if not check_reply_function(inst_nr, context, cmd_read, check_reply_len):
                logger.error(f"{testname}: instrument nr {inst_nr}: reply check failed for command: {cmd_read}")
                return False
        else:
            logger.error(f"{testname}: instrument nr {inst_nr}: no check_reply_function defined")
            return False
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
        cmd_read = ""
        check_reply_function = None
        check_reply_len = 0
        if "66332A" in idn:
            datalen = 800
            cmds_init = ["*CLS", "INIT:CONT:SEQ OFF", "SENS:FUNC \"VOLT\"", "TRIG:ACQ:SOUR BUS", "SENS:SWE:TINT 15.6E-6", f"SENSE:SWEEP:POINTS {datalen}"]
            cmd_read = "MEAS:ARRAY:VOLT?"
            check_reply_function = self.check_reply_66332A
            check_reply_len = datalen
        if "HP859" in idn:
            cmd_read = "USTATE?"
            check_reply_function = self.check_reply_hp8590
            check_reply_len = 0           
        if "ieee488_device" in idn:
            datalen = 500000
            cmd_read = f"LONGRD? {datalen}"
            check_reply_function = self.check_reply_ieee488_device
            check_reply_len = datalen

        if check_reply_function is None:
            logger.error(f"instrument nr {inst_nr}: idn \"{idn}\" is not supported for long read test")
            return {}
        
        ret.update({"cmds_init": cmds_init, "cmd_read": cmd_read, "check_reply_function": check_reply_function, "check_reply_len": check_reply_len})
        return ret
    
#endregion
#region private helpers
    ###############################################################################################

#endregion
#region the tests
    ###############################################################################################
    
    def check_reply_66332A(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int) -> bool:
        """Check the reply from the 66332A instrument.

        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # The reply should be a comma-separated list of floats, with length equal to expected_len
        try:
            reply = context["inst"].query_ascii_values(cmd_read)
            logger.debug(f"instrument nr {inst_nr}: received reply: {reply}")
            if len(reply) != expected_len:
                logger.error(f"Reply length {len(reply)} does not match expected length {expected_len}")
                return False
            return True
        except ValueError as e:
            logger.error(f"Failed to parse reply: {e}")
            return False
        
    def check_reply_ieee488_device(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int) -> bool:
        """Check the reply from the ieee488_device instrument.
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :return: True if the reply is valid, False otherwise
        :rtype: bool        
        """
        # The reply should be a custom sequential list of length expected_len
        inst = context["inst"]
        # allow 1ms per character in device debug mode
        # else: 140us per character on a atmega4809, with overhead
        # timeout is in ms
        inst.timeout = expected_len * 0.2  # non-debug mode
        if expected_len > 20000:
            inst.chunk_size = expected_len + 1000  # increase chunk size for large reads
        try:
            reply = inst.query(cmd_read)
            logger.debug(f"instrument nr {inst_nr}: received reply: {reply}")
            reply = reply.strip()
            if not reply or len(reply) == 0:
                logger.error(f"Reply is empty")
                return False
            if len(reply) != expected_len:
                logger.error(f"Reply length {len(reply)} does not match expected length {expected_len}")
                return False
            if not reply.startswith("0"):
                logger.error(f"Reply does not start with '0': {reply}")
                return False
            
            # now check sequentiality
            for i in range(1, len(reply)):
                if (ord(reply[i]) < 0x30) or (ord(reply[i]) > 0x7E):
                    logger.error(f"Reply contains non-printable characters at index {i}: {reply[i]}")
                    return False
                if (ord(reply[i]) == 0x30):
                    if (ord(reply[i-1]) != 0x7E):
                        logger.error(f"Reply is not sequential at index {i}: {reply[i-1]} -> {reply[i]}")
                        return False
                else:
                    if ord(reply[i]) != (ord(reply[i-1]) + 1):
                        logger.error(f"Reply is not sequential at index {i}: {reply[i-1]} -> {reply[i]}")
                        return False
            return True
        except ValueError as e:
            logger.error(f"Failed to parse reply: {e}")
            return False
    
    def check_reply_hp8590(self, inst_nr: int, context: dict, cmd_read: str, expected_len: int) -> bool:
        """Check the reply from the HP8590 instrument.
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context
        :type context: dict
        :param cmd_read: The command to be sent to the instrument to read the data
        :type cmd_read: str
        :param expected_len: The expected length of the reply
        :type expected_len: int
        :return: True if the reply is valid, False otherwise
        :rtype: bool
        """
        # Implement the actual check for HP8590 replies here
        inst = context["inst"]
        try:
            readresult = inst.query_binary_values(cmd_read, datatype="B", header_fmt="hp", is_big_endian=True, expect_termination=True)
            # TODO we might add timing check and length check
        except Exception as e:
            logger.error(f"instrument nr {inst_nr}: failed to read binary values: {e}")
            return False
        return True
#endregion

