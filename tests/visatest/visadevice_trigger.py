import logging

from time import sleep
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

class visadevice_trigger(visadevice_base.visadevice_base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Trigger"]
    
    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        inst = context["inst"]
        if "check_function" in context:
            check_function = context["check_function"]
            return check_function(inst_nr, context, test, testname)
        
        inst.clear()
        inst.assert_trigger()
        self.logger.info(f"{testname}: Trigger sent, STB={inst.read_stb()}")
        return True
    
    def get_instrument_commands(self, inst_nr: int, idn: str, test: int) -> dict:
        if self.resource_type in ["socket"]:
            # cannot do trigger stuff over socket
            return { "reason": f"Triggering is not supported on {self.resource_type} instruments" }
        
        rv = super().get_instrument_commands(inst_nr, idn, test)
        
        KS663xxPSUs = ["66332A", "66312A", "6631B", "6632B", "6633B", "6634B", "6613C", "6614C"]
        if any(ks in idn for ks in KS663xxPSUs):
            rv["check_function"] = self.check_663XXPSU
        
        return rv

    def check_value(self, inst_nr: int, context: dict, test: int, testname: str, cmd: str, expected_value: float, margin: float = 2e-2) -> bool:
        inst = context["inst"]
        v = inst.query(cmd).strip()
        fv = float(v)
        self.logger.debug(f"{testname}: instrument nr {inst_nr}: {cmd} returned {fv:.2f}")
        if abs(fv - expected_value) > margin:
            self.logger.error(f"{testname}: instrument nr {inst_nr}: {cmd} returned {fv:.2f}, expected {expected_value:.2f} (margin {margin:.2f})")
            return False
        return True

    def check_663XXPSU(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        inst = context["inst"]
        inst.clear()
        expected_1 = 1.0
        expected_2 = 10.0
        for cmd in ["OUTP ON", f"VOLT {expected_1}", "INIT:NAME TRAN", "TRIG:SOUR BUS", f"VOLT:TRIG {expected_2}"]:
            self.logger.debug(f"Instrument nr {inst_nr}: sending command: {cmd}")
            inst.write(cmd)
        sleep(0.5) # wait for commands to be processed
        
        if not self.check_value(inst_nr, context, test, testname, "MEAS:VOLT?", expected_1, 2e-2):
            return False
        
        # send trigger and check if the trigger was received by the instrument, by checking the STB
        inst.assert_trigger()
        sleep(0.5)  # wait for commands to be processed
        
        if not self.check_value(inst_nr, context, test, testname, "MEAS:VOLT?", expected_2, 2e-2):
            return False        
        inst.write("OUTP OFF")
        
        return True
