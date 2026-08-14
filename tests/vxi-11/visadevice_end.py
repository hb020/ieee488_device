import logging
import pyvisa
import pyvisa.constants
from visadevice_helpers import ieee488_device_longrd_query, str_diff

import visadevice_base

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

class visadevice_end(visadevice_base.visadevice_base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["End conditions: EOS", "End conditions: EOI", "End conditions: Count", "End conditions: Chunk size"]

    def set_instrument_to_eos(self, inst, cmd_goto_eos):
        if len(cmd_goto_eos) > 0:
            inst.write(cmd_goto_eos)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, True)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR, ord("\n"))
        if self.visa_type != "socket":
            # socket does not support the SEND_END_EN attribute, so skip it for those backends
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SEND_END_EN, False)
        try:
            inst.set_visa_attribute(pyvisa.constants.VI_ATTR_SUPPRESS_END_EN, True)
        except:
            # some backends do not support this attribute, so ignore the error
            pass

    def set_instrument_to_eoi(self, inst, cmd_goto_eoi):
        if (len(cmd_goto_eoi) > 0):
            inst.write(cmd_goto_eoi)
        inst.set_visa_attribute(pyvisa.constants.VI_ATTR_TERMCHAR_EN, False)
        if self.visa_type != "socket":
            # socket does not support the SEND_END_EN attribute, so skip it for those backends
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

        inst.write_terminator = "\n"
        inst.read_terminator = "\n"
        
        if test == 0:
            # EOS case
            rv = True
            try:
                self.set_instrument_to_eos(inst, cmd_goto_eos)
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii")
                status = inst.last_status
                if status != pyvisa.constants.VI_SUCCESS_TERM_CHAR:
                    if status != pyvisa.constants.VI_SUCCESS:
                        self.logger.error(f"{testname} instrument nr {inst_nr}: expected VI_SUCCESS_TERM_CHAR, got {status}")
                        return False
                    else:
                        self.logger.warning(f"{testname} instrument nr {inst_nr}: expected VI_SUCCESS_TERM_CHAR, got VI_SUCCESS. This may be a backend that does not support the EOS attribute reporting, or the device may not support EOS.")
                expected = expected_reply + "\n"
                if r != expected:
                    if not (len(r) > 0 and len(r) > len(expected_reply) and r.startswith(expected_reply) and expected_reply.endswith(",")):
                        self.logger.error(f"{testname} instrument nr {inst_nr}: expected \"{expected}\", got \"{r}\"")
                        rv = False
            except Exception as e:
                self.logger.error(f"{testname} instrument nr {inst_nr}: exception: {e}")
                rv = False
                
            self.set_instrument_to_eoi(inst, cmd_goto_eoi)
            return rv

        if test == 1:
            # normal case (EOI)
            try:
                self.set_instrument_to_eoi(inst, cmd_goto_eoi)
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii")
                status = inst.last_status
                if status != pyvisa.constants.VI_SUCCESS:
                    if status == pyvisa.constants.VI_SUCCESS_TERM_CHAR:
                        self.logger.warning(f"{testname} instrument nr {inst_nr}: expected VI_SUCCESS, got VI_SUCCESS_TERM_CHAR. This may be a backend that does not support the EOI attribute reporting, or the device may not support EOI.")
                    else:
                        self.logger.error(f"{testname} instrument nr {inst_nr}: expected VI_SUCCESS, got {status}")
                        return False
                r = r.rstrip() # remove the trailing newline, as the EOI case does not have a newline
                if r != expected_reply:
                    if not (len(r) > 0 and len(r) > len(expected_reply) and r.startswith(expected_reply) and expected_reply.endswith(",")):
                        self.logger.error(f"{testname} instrument nr {inst_nr}: expected \"{expected_reply}\", got \"{r}\"")
                        return False          
                
            except Exception as e:
                self.logger.error(f"{testname} instrument nr {inst_nr}: exception: {e}")
                return False
            
        if test == 2:
            rv = True
            try:
                self.set_instrument_to_eoi(inst, cmd_goto_eoi)    
                # do it once to get the normal length
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii").rstrip() # remove the trailing newline, as the EOI case does not have a newline
                if r != expected_reply:
                    if not (len(r) > 0 and len(r) > len(expected_reply) and r.startswith(expected_reply) and expected_reply.endswith(",")):
                        self.logger.error(f"{testname} instrument nr {inst_nr}: expected \"{expected_reply}\", got \"{r}\"")
                        return False
                # now force the expected to be what I just read, in case the real reply is longer
                expected_reply = r
                # now ask the same, just with a smaller length
                inst.write(cmd_test)
                take_off = min(5, len(r)-1) # take off at most 5 characters, and leave at least 1. If I make it 0, pyvisa will request all.
                expected = expected_reply[:-take_off] # remove the last characters from the expected reply, to match the read length
                self.logger.debug(f"{testname} instrument nr {inst_nr}: reading {len(r)-take_off} characters, expected reply: \"{expected}\"")
                r = inst.read_bytes(len(r)-take_off).decode("ascii") # read less characters than the previous read, to test that the read stops at count
                status = inst.last_status
                if status != pyvisa.constants.VI_SUCCESS_MAX_CNT:
                    self.logger.error(f"{testname} instrument nr {inst_nr}: expected VI_SUCCESS_MAX_CNT, got {status}")
                    return False                
                expected = expected_reply[:-take_off] # remove the last characters from the expected reply, to match the read length
                if r != expected:
                    self.logger.error(f"{testname} instrument nr {inst_nr}: expected \"{expected}\", got \"{r}\"")
                    rv = False
            except Exception as e:
                self.logger.error(f"{testname} instrument nr {inst_nr}: exception: {e}")
                rv = False
                
            old_timeout = inst.timeout
            try:
                # flush out the remaining characters, so that the next test can start with a clean buffer
                inst.timeout = 100 # set a short timeout to test that the read stops at EOI
                inst.read_raw()
            except Exception as e:
                self.logger.error(f"{testname} instrument nr {inst_nr}: flush raised an exception: {e}")
                rv = False
            finally:
                inst.timeout = old_timeout
            return rv                
                
        if test == 3:
            rv = True
            try:
                self.set_instrument_to_eoi(inst, cmd_goto_eoi)    
                # do it once to get the normal length
                inst.write(cmd_test)
                r = inst.read_raw().decode("ascii").rstrip() # remove the trailing newline, as the EOI case does not have a newline
                if r != expected_reply:
                    if not (len(r) > 0 and len(r) > len(expected_reply) and r.startswith(expected_reply) and expected_reply.endswith(",")):
                        self.logger.error(f"{testname} instrument nr {inst_nr}: EOI test failed: expected \"{expected_reply}\", got \"{r}\"")
                        return False
                # now force the expecte dto be what I just read, in case the real reply is longer
                expected_reply = r
                expected_len = len(r)
                
                chunk_size = 1
                while chunk_size < expected_len + 4:
                    # do a series of number of bytes less and more
                    inst.chunk_size = chunk_size
                    inst.write(cmd_test)
                    try:
                        inst.read_raw()
                    except Exception as e:
                        self.logger.error(f"{testname} instrument nr {inst_nr}: read_raw() with chunk_size {inst.chunk_size} failed: {e}")
                        rv = False
                        break
                    chunk_size += 1
                    # do the first and last 10 chunk sizes
                    if expected_len > 20 and chunk_size == 11:
                        chunk_size = expected_len - 10

            except Exception as e:
                self.logger.error(f"{testname} instrument nr {inst_nr}: exception: {e}")
                rv = False                     

        # Attributes for Read and Write:
        # VI_ATTR_TERMCHAR_EN
        # VI_ATTR_TERMCHAR
        # VI_ATTR_SEND_END_EN (not used by early pyvisa-py, but used by NI-VISA, not supported on socket, but supported on VXI-11 and hislip)
        
        # Attributes for Read
        # VI_ATTR_SUPPRESS_END_EN (not supported by early pyvisa-py, not supported by NI-Visa on VXI-11, but supported on other types)
        
        return self.check_errors(inst_nr, context)

    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        ret = super().get_instrument_commands(inst_nr, idn, test)
        cmds_init = ret["cmds_init"][:]  # make a copy of the list, so that I only overwrite the commands I want to change, and keep the rest of the commands from the base class
        cmd_goto_eos = ""  # leave empty for instruments that do not support EOS, so that the test will be skipped for those instruments
        cmd_goto_eoi = ""  # may be empty. EOI tests will always be done.
        cmd_test = ""
        expected_reply = ""  # can be shorter than the real reply, in which case the test will check that the reply starts with the expected reply and ends with a comma, to indicate that the reply is longer than the expected reply.
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
        if "34465A" in idn:
            cmd_goto_eos = "*CLS" # fake it
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "Keysight Technologies,34465A,"
        if "DMM6500" in idn:
            cmd_goto_eos = "*CLS" # fake it
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "KEITHLEY INSTRUMENTS,MODEL DMM6500,"
        if "SDM3055" in idn:
            cmd_goto_eos = "*CLS" # fake it
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "Siglent Technologies,SDM3055,"
        if "SDS824X" in idn:
            cmd_goto_eos = "*CLS" # fake it
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "Siglent Technologies,SDS824X HD,"
        if "DG992" in idn:
            cmd_goto_eos = "*CLS" # fake it
            cmd_goto_eoi = ""
            cmd_test = "*IDN?"
            expected_reply = "Rigol Technologies,DG992,"

        if test == 0 and (len(cmd_test) == 0 or len(cmd_goto_eos) == 0):
            # no EOS command?
            # hislip does not support EOS control
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

