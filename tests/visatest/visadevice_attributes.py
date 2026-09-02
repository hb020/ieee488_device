import logging

from time import sleep
import visadevice_base
import attributes
import pyvisa.constants


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

class visadevice_attributes(visadevice_base.visadevice_base):
#region overrides
    ###############################################################################################
    @classmethod
    def testmethods(cls) -> list[str]:
        return ["Attributes"]
    
    def test_instrument(self, inst_nr: int, context: dict, test: int, testname: str) -> bool:
        if self.resource_type == "socket":
            resource_type = "SOCKET:TCPIP"
        elif self.resource_type == "hislip":
            resource_type = "INSTR:TCPIP:HiSLIP"
        elif self.resource_type == "vxi11" or self.resource_type == "gateway":
            resource_type = "INSTR:TCPIP:VXI-11"
        else:
            resource_type = "RESOURCE_MANAGER"
            
        inst = context["inst"]
        print(f"Testing attributes for instrument {context['resource_name']}.")
        for attr,items in attributes.VISA_RESOURCE_TYPES_BY_ATTRIBUTE.items():
            if resource_type not in items:
                continue
            try:
                attribute = getattr(pyvisa.constants, attr)
                t = inst.get_visa_attribute(attribute)
                print(f"{attr}: {t}")
            except Exception as e:
                print(f"{attr}: ERR: {type(e).__name__}")
        return True
    
