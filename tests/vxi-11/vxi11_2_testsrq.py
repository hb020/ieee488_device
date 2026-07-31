from typing import Optional
import sys
import os
from time import sleep
import pyvisa
import pyvisa.constants
import logging

import vxi11_2_base

# Choose one of the 2
# EVENT_MECH = pyvisa.constants.EventMechanism.queue
EVENT_MECH = pyvisa.constants.EventMechanism.handler

SRQ_WAIT_TIME = 2.0 # seconds to wait for SRQ to be received, after emitting it

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

srq_called = {}

def handle_event(resource, event, srq_user_handle):
    global srq_called
    if isinstance(srq_user_handle, int):
        instr_nr = srq_user_handle
    else:
        instr_nr = srq_user_handle.value
    logger.info(f"Event handler: SRQ received for instrument {instr_nr}")
    if event.event_type == pyvisa.constants.EventType.service_request:
        if instr_nr not in srq_called:
            logger.error(f"Event handler: Unexpected srq_user_handle={instr_nr}")
        else:
            srq_called[instr_nr] = srq_called.get(instr_nr, 0) + 1
    else:
        logger.error(f"Event handler: Unexpected event {event.event_type} (should be {pyvisa.constants.EventType.service_request}), srq_user_handle={instr_nr}")



class VXI11_2_testssrq(vxi11_2_base.VXI11_2_Base):
    
    def prepare_instrument_context(self) -> None:
        super().prepare_instrument_context()
        self.srq_event_handler = {}
        self.user_handle = {}
        self.srq_enabled = {}
        for inst_nr in range(self.start_instrument, self.end_instrument + 1):
            srq_called[inst_nr] = 0

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
        cmds_init = []
        cmds_srq_enable = []
        cmds_srq_provoke = ""
        if "STMEthernet2GPIB" in idn:
            cmds_init = ["*CLS", "*SRE 0", "*CLS", "*RST"]
            cmds_srq_enable = ["*ESE 254", "*SRE 4"]
            cmds_srq_provoke = "bla"
        if "66332A" in idn:
            cmds_init = ["*CLS", "*SRE 0", "*CLS", "*RST"]
            cmds_srq_enable = ["*ESE 254", "*SRE 32"]
            cmds_srq_provoke = "bla"
        if "6634B" in idn:
            cmds_init = ["*CLS", "*SRE 0", "*CLS", "*RST"]
            cmds_srq_enable = ["*ESE 254", "*SRE 32"]
            cmds_srq_provoke = "bla"
        if "34465A" in idn:
            cmds_init = ["*CLS", "*SRE 0", "*RST"]
            cmds_srq_enable = ["*SRE 36"]
            cmds_srq_provoke = "bla"
        if "DMM6500" in idn:
            cmds_init = ["*CLS", "*SRE 0", "*RST", "SYST:CLE", "STAT:CLE"]
            cmds_srq_enable = ["*SRE 36"]
            cmds_srq_provoke = "bla"
        if "HP859" in idn:
            cmds_init = ["CLS", "CMDERRQ?"]
            cmds_srq_enable = ["RQS 40"]
            cmds_srq_provoke = "SRQ 8"
        if "ieee488_device" in idn:
            cmds_init = ["*CLS"]
            cmds_srq_enable = []
            cmds_srq_provoke = "SRQ"

        return {"cmds_init": cmds_init, "cmds_srq_enable": cmds_srq_enable, "cmds_srq_provoke": cmds_srq_provoke}
    
    def prepare_to_listen_for_srq(self, inst_nr: int, context: dict) -> bool:
        cmds = context["cmds_init"] + context["cmds_srq_enable"]
        inst = context["inst"]
        resource_name = context["resource_name"]
        
        for cmd in cmds:
            # logger.debug(f"Sending {cmd} to {resource_name}, 0x{inst.read_stb():02X}")
            if cmd.endswith("?"):
                resp = inst.query(cmd)
                # logger.debug(f"Query {cmd} returned {resp.strip()} from {resource_name}, STB=0x{inst.read_stb():02X}")
            else:
                inst.write(cmd)
            sleep(0.1)
            
        stb = inst.read_stb()
        if stb != 0:
            logger.error(f"Programming error: STB not 0 after init for {resource_name}, STB=0x{stb:02X}")
            return False
        
        if srq_called.get(inst_nr, 0) != 0:
            logger.error(f"Programming error: SRQ handler got called {srq_called.get(inst_nr, 0)} times BEFORE init for {resource_name}")
            return False
        srq_called[inst_nr] = 0
        return True

    def enable_listen_for_srq(self, inst_nr: int, context: dict) -> bool:
        logger.debug(f"Enabling SRQ listening for instrument {inst_nr}")
        inst = context["inst"]
        
        event_type = pyvisa.constants.EventType.service_request
        event_mech = EVENT_MECH
        srq_event_handler = inst.wrap_handler(handle_event)
        srq_user_handle = inst.install_handler(event_type, srq_event_handler, inst_nr)
        self.srq_event_handler[inst_nr] = srq_event_handler
        self.user_handle[inst_nr] = srq_user_handle
            
        inst.enable_event(event_type, event_mech, None)
        self.srq_enabled[inst_nr] = True
        return True

    def emit_srq(self, inst_nr: int, context: dict) -> bool:
        logger.debug(f"Emitting SRQ for instrument {inst_nr}")
        inst = context["inst"]
        cmds_srq_provoke = context["cmds_srq_provoke"]
        inst.write(cmds_srq_provoke)
        sleep(0.1)    
        return True

    def wait_srq_for_instr(self, inst_nr: int, context: dict) -> bool:
        if EVENT_MECH == pyvisa.constants.EventMechanism.handler:
            logger.debug(f"Waiting for SRQ for instrument {inst_nr} (handler mechanism)")
            for i in range(int(SRQ_WAIT_TIME * 10)):
                if srq_called.get(inst_nr, 0) != 0:
                    break
                sleep(0.1)
            logger.debug(f"Finished waiting for SRQ for instrument {inst_nr} (handler mechanism)")
            return True
        else:
            logger.debug(f"Waiting for SRQ for instrument {inst_nr} (queue mechanism)")
            inst = context["inst"]
            event_type = pyvisa.constants.EventType.service_request
            try:
                event = inst.wait_on_event(event_type, timeout=int(SRQ_WAIT_TIME * 1000))
                logger.debug(f"Finished waiting for SRQ for instrument {inst_nr} (queue mechanism), event={event}")
                srq_called[inst_nr] = srq_called.get(inst_nr, 0) + 1
            except pyvisa.VisaIOError as e:
                logger.debug(f"Failed to wait for SRQ for instrument {inst_nr}: {e}")
                return True
        return True

    def check_if_had_srq(self, inst_nr: int, expected_SRQ: bool, expected_calls: int, testname: str, context: dict) -> tuple[bool, bool, int]:
        # Return values: test failed, had SRQ in STB, number of times handler called
        inst = context["inst"]
        resource_name = context["resource_name"]
        # expected SRQ: must have been called once, and STB must have SRQ bit set
        # not expected SRQ: MAY have been called, and STB must not have SRQ bit set
        called_nr = srq_called.get(inst_nr, 0)

        ignore_stb = False
        if not ignore_stb:
            stb1 = inst.read_stb()
            stb2 = inst.read_stb()
        else:
            # old versions of pyvisa-py do not return the correct STB value, so we fake it
            stb1 = inst.read_stb()
            stb2 = stb1 & ~0x40
            if expected_SRQ:
                stb1 = stb1 | 0x40
        stb1_has_srq = stb1 & 0x40
        stb2_has_srq = stb2 & 0x40
        if expected_SRQ and stb1_has_srq == 0:
            logger.error(f"{testname}: STB did not have SRQ bit set on {resource_name}, returned STB=0x{stb1:02X}")
            return False, stb1_has_srq != 0, called_nr
        if expected_calls >= 0 and called_nr != expected_calls:
            logger.error(f"{testname}: SRQ received {called_nr} times for {resource_name}, expected {expected_calls} times")
            return False, stb1_has_srq != 0, called_nr
        if not expected_SRQ and stb1_has_srq != 0:
            logger.error(f"{testname}: STB had SRQ bit set on {resource_name}, returned STB=0x{stb1:02X}")
            return False, stb1_has_srq != 0, called_nr    
        if stb2_has_srq != 0:
            logger.error(f"{testname}: STB still has SRQ bit set after reading STB from {resource_name}, returned STB=0x{stb2:02X}")
            return False, stb1_has_srq != 0, called_nr
        return True, stb1_has_srq != 0, called_nr


    def close_instrument(self, inst_nr: int, context: dict) -> bool:
        resource_name = context["resource_name"]
        inst = context["inst"]
        if not context["opened"]:
            return super().close_instrument(inst_nr, context)
        
        event_type = pyvisa.constants.EventType.service_request
        if self.srq_enabled.get(inst_nr, False):
            self.srq_enabled[inst_nr] = False
            event_mech = EVENT_MECH       
            inst.disable_event(event_type, event_mech)
        srq_event_handler = self.srq_event_handler.get(inst_nr, None)
        srq_user_handle = self.user_handle.get(inst_nr, None)    
        if (srq_event_handler is not None and srq_user_handle is not None):
            inst.uninstall_handler(event_type, srq_event_handler, srq_user_handle)
            self.srq_event_handler[inst_nr] = None
            self.user_handle[inst_nr] = None
            
        cmds = context["cmds_init"]
        for cmd in cmds:
            # logger.debug(f"Sending {cmd} to {resource_name}, 0x{inst.read_stb():02X}")
            if cmd.endswith("?"):
                resp = inst.query(cmd)
                # logger.debug(f"Query {cmd} returned {resp.strip()} from {resource_name}, STB=0x{inst.read_stb():02X}")
            else:
                inst.write(cmd)
            sleep(0.1)
        try:
            inst.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
        except Exception as e:
            logger.debug(f"Failed to set REN line for {resource_name}: {e}")
            
        return super().close_instrument(inst_nr, context)

    ###############################################################################################

    # Test SRQ handling for a range of instruments on the bus, one after the other
    # This fully replaces "run()" from the base class
    def test_individual_srqs(self, early_enable: bool) -> bool:
        retvalue = True
        test_type = "early enable" if early_enable else "late enable"
        
        if self.start_instrument == self.end_instrument:
            rangestr = f"instrument {self.start_instrument}"
        else:
            rangestr = f"instruments {self.start_instrument} to {self.end_instrument}"
        
        global_testname = f"Individual SRQ {test_type} test for {rangestr} on gateway {self.gateway_ip}"
        logger.info(f"{global_testname}: Starting")
        
        self.prepare_instrument_context()
                
        for i in range(self.start_instrument, self.end_instrument + 1):
            context = self._inst_contexts[i]
            
            this_instrument_ok = True
            resource_name = self.get_resourcename_for_instrument(i)
            testname = f"Individual SRQ {test_type} test"
            if not self.setup_instrument(resource_name, i):
                logger.error(f"{testname}: Failed to open {resource_name}")
                retvalue = False
                continue;

            if this_instrument_ok:
                if not self.prepare_to_listen_for_srq(i, context):
                    logger.error(f"{testname}: Failed to prepare for SRQ listening for {resource_name}")
                    this_instrument_ok = False
            if this_instrument_ok:
                if early_enable:
                    if not self.enable_listen_for_srq(i, context):
                        logger.error(f"{testname}: Failed to enable SRQ listening for {resource_name}")
                        this_instrument_ok = False
            if this_instrument_ok:
                if not self.emit_srq(i, context):
                    logger.error(f"{testname}: Failed to emit SRQ for {resource_name}")
                    this_instrument_ok = False
            if this_instrument_ok:
                if not early_enable:
                    if not self.enable_listen_for_srq(i, context):
                        logger.error(f"{testname}: Failed to enable SRQ listening for {resource_name}")
                        this_instrument_ok = False
            if this_instrument_ok:
                if not self.wait_srq_for_instr(i, context):
                    logger.error(f"{testname}: Failed to wait for SRQ for {resource_name}")
                    this_instrument_ok = False
            if this_instrument_ok:
                this_instrument_ok, _, _ = self.check_if_had_srq(i, True, 1, testname, context)
            self.close_instrument(i, context)
            if this_instrument_ok:
                logger.info(f"{testname}: {resource_name} is OK")
            else:
                retvalue = False
        logger.info(f"{global_testname}: {'OK' if retvalue else 'FAILED'}")
        return retvalue

    # Test SRQ handling for a single emitting instrument on the bus, while other instruments are present on the bus, 
    # listening for events, but not emitting SRQ
    def test_one_emitting_srq(self) -> bool:
        test_instrument = self.start_instrument
        
        if (self.start_instrument == self.end_instrument):
            logger.warning(f"Single emitter SRQ test: only one instrument ({self.start_instrument}) on the test, cannot test multiple listeners")
            return True
        
        retvalue = True
        
        global_testname = f"Single emitter SRQ test for instruments {self.start_instrument} to {self.end_instrument} on gateway {self.gateway_ip}"
        logger.info(f"{global_testname}: Starting")
        
        self.prepare_instrument_context()
        testcontext = self._inst_contexts[test_instrument]
        
        testname = "Single emitter SRQ test"
        # set up all instruments, all listening
        for i in range(self.start_instrument, self.end_instrument + 1):
            context = self._inst_contexts[i]
            
            this_instrument_ok = True
            resource_name = self.get_resourcename_for_instrument(i)
            if not self.setup_instrument(resource_name, i):
                logger.error(f"{testname}: Failed to open {resource_name}")
                retvalue = False
                continue;

            if this_instrument_ok:
                if not self.prepare_to_listen_for_srq(i, context):
                    logger.error(f"{testname}: Failed to prepare for SRQ listening for {resource_name}")
                    this_instrument_ok = False
            if this_instrument_ok:
                if not self.enable_listen_for_srq(i, context):
                    logger.error(f"{testname}: Failed to enable SRQ listening for {resource_name}")
                    this_instrument_ok = False
            if not this_instrument_ok:
                retvalue = False
        
        # only 1 emitting instrument, the others are just listening
        if retvalue:
            resource_name = testcontext["resource_name"]
            if not self.emit_srq(test_instrument, testcontext):
                logger.error(f"{testname}: Failed to emit SRQ for {resource_name}")
                retvalue = False
                
        # Check results: only the emitting instrument should have had SRQ, the others should not have had SRQ
        if retvalue:
            # wait for SRQ for all instruments, so that the listening instrument can receive it
            for i in range(self.start_instrument, self.end_instrument + 1):
                self.wait_srq_for_instr(i, self._inst_contexts[i])
                
            # Then check counters
            for i in range(self.start_instrument, self.end_instrument + 1):
                # Check the false cases: MAY have had called, but STB must not have SRQ bit set
                context = self._inst_contexts[i]
                resource_name = context["resource_name"]
                if i == test_instrument:
                    ok, _, nr_intr = self.check_if_had_srq(i, True, 1, testname, self._inst_contexts[i])
                    if not ok:
                        retvalue = False
                else:
                    ok, _, nr_intr = self.check_if_had_srq(i, False, -1, testname, context)
                    if nr_intr > 0:
                        logger.warning(f"{testname}: SRQ handler called {nr_intr} times for {resource_name}, this gateway likely does not filter SRQ events")

        for i in range(self.start_instrument, self.end_instrument + 1):
            self.close_instrument(i, self._inst_contexts[i])
            
        logger.info(f"{global_testname}: {'OK' if retvalue else 'FAILED'}")
        return retvalue


    # Test SRQ handling for multiple instruments emitting SRQ on the bus, but only one listening for events
    def test_multiple_emitting_srq(self) -> bool:
        test_instrument = self.start_instrument
        
        if (self.start_instrument == self.end_instrument):
            logger.warning(f"Multiple emitter SRQ test: only one instrument ({self.start_instrument}) on the test, cannot test multiple emitters")
            return True
                
        retvalue = True
        global_testname = f"Multiple emitter SRQ test for instruments {self.start_instrument} to {self.end_instrument} on gateway {self.gateway_ip}"
        logger.info(f"{global_testname}: Starting")
        
        self.prepare_instrument_context()
        test_context = self._inst_contexts[test_instrument]
        
        testname = "Multiple emitter SRQ test"
        # set up all instruments, only one listening
        for i in range(self.start_instrument, self.end_instrument + 1):
            context = self._inst_contexts[i]
            
            this_instrument_ok = True
            resource_name = self.get_resourcename_for_instrument(i)
            
            if not self.setup_instrument(resource_name, i):
                logger.error(f"{testname}: Failed to open {resource_name}")
                retvalue = False
                continue;

            if this_instrument_ok:
                if not self.prepare_to_listen_for_srq(i, context):
                    logger.error(f"{testname}: Failed to prepare for SRQ listening for {resource_name}")
                    this_instrument_ok = False
            if this_instrument_ok:
                if i == test_instrument:
                    if not self.enable_listen_for_srq(i, context):
                        logger.error(f"{testname}: Failed to enable SRQ listening for {resource_name}")
                        this_instrument_ok = False
            if not this_instrument_ok:
                retvalue = False
                
        # All emit
        if retvalue:
            for i in range(self.start_instrument, self.end_instrument + 1):            
                resource_name = self._inst_contexts[test_instrument]["resource_name"]
                if not self.emit_srq(i, self._inst_contexts[i]):
                    logger.error(f"{testname}: Failed to emit SRQ for {resource_name}")
                    retvalue = False
        
        # All should have SRQ bit set, but only the listening instrument should have had SRQ call
        if retvalue:
            # wait for SRQ for all instruments, so that the listening instrument can receive it
            for i in range(self.start_instrument, self.end_instrument + 1):
                self.wait_srq_for_instr(i, self._inst_contexts[i])
            # Then check counters
            for i in range(self.start_instrument, self.end_instrument + 1):
                # Check the false cases: MAY have had called, but STB must not have SRQ bit set
                context = self._inst_contexts[i]
                resource_name = context["resource_name"]
                if i == test_instrument:
                    expected_calls = 1
                else:
                    expected_calls = 0
                ok, _, _ = self.check_if_had_srq(i, True, expected_calls, testname, context)
                if not ok:
                    retvalue = False

        for i in range(self.start_instrument, self.end_instrument + 1):
            self.close_instrument(i, self._inst_contexts[i])
        logger.info(f"{global_testname}: {'OK' if retvalue else 'FAILED'}")
        return retvalue
