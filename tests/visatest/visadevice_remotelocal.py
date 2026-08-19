import logging

import pyvisa
import pyvisa.constants
import time
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

class visadevice_remotelocal(visadevice_base.visadevice_base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Remote/Local"]
    
    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        inst = context["inst"]
        # Supported operations:
        # NI-VISA:
        # - address_gtl, deassert_gtl: both send DEVICE_LOCAL
        # - asrt_address, asrt_address_llo: both send DEVICE_REMOTE
        # - all others are not supported
        # pyvisa-py:
        # - nothing is supported in 0.8.1
        
        # Problem: I have no real way of testing this via a script, as I cannot read the remote status of the instruments
        # The moment I ask something, it is remote.
        
        rv = True
        try:
            inst.control_ren(pyvisa.constants.RENLineOperation.asrt_address_llo) # remote
            self.logger.debug(f"{testname} instrument nr {inst_nr}: Controlled REN: asrt_address_llo")
        except Exception as e:
            self.logger.warning(f"{testname} instrument nr {inst_nr}: Failed to control REN: asrt_address_llo: {e}")
            self.skipped += 1
            return True
        
        sleeptime = 2
        self.logger.info(f"{testname} instrument nr {inst_nr}: should be remote now for {sleeptime} seconds")
        time.sleep(sleeptime)    
        try:
            inst.control_ren(pyvisa.constants.RENLineOperation.address_gtl) # local, for that instrument
            self.logger.debug(f"{testname} instrument nr {inst_nr}: Controlled REN: address_gtl")
        except Exception as e:
            self.logger.error(f"{testname} instrument nr {inst_nr}: Failed to control REN: address_gtl: {e}")
            rv = False

        sleeptime = 1
        self.logger.info(f"{testname} instrument nr {inst_nr}: should be local now")
        time.sleep(sleeptime)
        # NO check for errors, as that makes it remote again
        return rv
