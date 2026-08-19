import logging

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
        self.skipped += 1
        self.logger.warning(f"{testname}: instrument nr {inst_nr}: is not implemented yet")
        return True  # Not implemented yet