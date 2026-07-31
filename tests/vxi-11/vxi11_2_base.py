from typing import Optional
import sys
import os
import pyvisa
import pyvisa.constants
import logging

#region General setup
#resource_managers: the below plus default
# On MacOS, only pyvisa-py and NI-VISA are supported, but the latter only as "default"
# and so far, I haven't found a way to see what is the default.
RESOURCE_MANAGERS = ['py', 'ni', 'keysight', 'rs']

# Choose one of the 2
# EVENT_MECH = pyvisa.constants.EventMechanism.queue
EVENT_MECH = pyvisa.constants.EventMechanism.handler

SRQ_WAIT_TIME = 2.0 # seconds to wait for SRQ to be received, after emitting it

DEFAULT_GATEWAY_IP = "192.168.7.116"
DEFAULT_INST = 5
DEFAULT_VISA_PROVIDER = "py" # default, py, ni, keysight, rs

#endregion
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

#region connection setup


def get_ressource_manager(visa_provider: Optional[str]) -> tuple[pyvisa.ResourceManager, str]:
    """
    Get a PyVISA ResourceManager for the specified VISA provider.
    
    :param visa_provider: The VISA provider name ('py', 'ni', 'keysight', 'rs', or None).
                          None, or any string other than the recognized providers,  ('py', 'ni', 'keysight', 'rs') 
                          all designate the system default provider.
    
    :returns: A tuple of (ResourceManager, provider_name) where provider_name is the resolved provider.
    :raises Exception: If the specified provider is not available on the current platform.
    """
    if visa_provider is None:
        visa_provider = ''
    if not isinstance(visa_provider, str):
        visa_provider = str(visa_provider)
    visa_provider = visa_provider.strip().lower()
        
    if visa_provider == RESOURCE_MANAGERS[0]:
        return pyvisa.ResourceManager('@py'), visa_provider
    if visa_provider == RESOURCE_MANAGERS[1]:
        if sys.platform == 'win32' or sys.platform == 'win64':
            libpath = 'C:/Windows/System32/nivisa64.dll'
            if os.path.exists(libpath):
                return pyvisa.ResourceManager(libpath), visa_provider
            else:
                raise Exception(f"NI VISA cannot be found: no '{libpath}' on this system")
        else:
            raise Exception(f"NI VISA cannot be selected (yet) on {sys.platform}")
    if visa_provider == RESOURCE_MANAGERS[2]:
        if sys.platform == 'win32' or sys.platform == 'win64':
            libpath = 'C:/Program Files (x86)/IVI Foundation/VISA/WinNT/ktvisa/ktbin/visa32.dll'
            if os.path.exists(libpath):
                os.add_dll_directory("C:/Program Files/Keysight/IO Libraries Suite/bin")
                os.add_dll_directory("C:/Program Files (x86)/Keysight/IO Libraries Suite/bin")
                return pyvisa.ResourceManager(libpath), visa_provider
            else:
                raise Exception(f"Keysight VISA cannot be found: no '{libpath}' on this system")
        else:
            raise Exception(f"Keysight VISA cannot be selected (yet) on {sys.platform}")
    if visa_provider == RESOURCE_MANAGERS[3]:
        if sys.platform == 'win32' or sys.platform == 'win64':
            libpath = 'C:/Program Files (x86)/IVI Foundation/VISA/WinNT/RsVisa/bin/visa32.dll'
            if os.path.exists(libpath):
                return pyvisa.ResourceManager(libpath), visa_provider
            else:
                raise Exception(f"R&S VISA cannot be found: no '{libpath}' on this system")
        else:
            raise Exception(f"R&S VISA cannot be selected (yet) on {sys.platform}")
    else:
        return pyvisa.ResourceManager(''), 'default' # TODO: find out what visa this really is
#endregion

#region test base class
class VXI11_2_Base:
    """
    Base class for VXI-11 tests.
    """
    
    def __init__(self, visa_provider: Optional[str] = DEFAULT_VISA_PROVIDER, gateway_ip: str = DEFAULT_GATEWAY_IP, start_instrument: int = 0, end_instrument: int = 0):
        """Initialize the VXI-11.2 base test case.

        :param visa_provider: The VISA provider to use ('py', 'ni', 'keysight', 'rs', or None).
                          None, or any string other than the recognized providers,  ('py', 'ni', 'keysight', 'rs') 
                          all designate the system default provider. defaults to DEFAULT_VISA_PROVIDER
        :type visa_provider: str, optional
        :param gateway_ip: The IP address of the VXI-11.2 gateway, defaults to DEFAULT_GATEWAY_IP
        :type gateway_ip: str, optional
        :param start_instrument: The starting instrument number, defaults to 0
        :type start_instrument: int, optional
        :param end_instrument: The ending instrument number, defaults to 0
        :type end_instrument: int, optional
        """
        self.gateway_ip = gateway_ip
        self.start_instrument = start_instrument
        self.end_instrument = end_instrument
        self.visa_provider = visa_provider
        self._inst_contexts = {}
        self.rm, self.visa_provider = get_ressource_manager(visa_provider)
        logger.info(f"Using VISA provider: {self.visa_provider}")
        self.prepare_instrument_context()
    
    def prepare_instrument_context(self) -> None:
        """Initialize the instrument contexts for the specified range of instruments.
        """
        for inst_nr in range(self.start_instrument, self.end_instrument + 1):
            self._inst_contexts[inst_nr] = {}
            self._inst_contexts[inst_nr]["called"] = 0
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            self._inst_contexts[inst_nr]["resource_name"] = ""
            self._inst_contexts[inst_nr]["cmds_init"] = []
            
    def get_resourcename_for_instrument(self, inst_nr: int) -> str:
        """Get the VXI-11.2 VISA compatible resource name for the given instrument bus address.

        :param inst_nr: The instrument bus address
        :type inst_nr: int
        :return: The VXI-11.2 VISA compatible resource name
        :rtype: str
        """
        if self.visa_provider == "rs":
            return f"TCPIP::{self.gateway_ip}::inst{inst_nr}::INSTR"
        else:
            return f"TCPIP::{self.gateway_ip}::gpib0,{inst_nr}::INSTR"
        
    def setup_instrument(self, resource_name: str, inst_nr: int) -> bool:
        self._inst_contexts[inst_nr]["resource_name"] = resource_name
        try:
            inst = self.rm.open_resource(resource_name)
        except Exception as e:
            logger.error(f"Failed to open {resource_name}: {e}")
            return False
        inst.timeout = 1000
        if inst is None or not (
                isinstance(inst, pyvisa.resources.TCPIPInstrument) or 
                isinstance(inst, pyvisa.resources.GPIBInstrument) or
                isinstance(inst, pyvisa.resources.USBInstrument)):
            logger.error(f"Failed to open {resource_name}")
            return False
        
        self._inst_contexts[inst_nr]["opened"] = True
        self._inst_contexts[inst_nr]["inst"] = inst
        try:
            idn = inst.query("*IDN?").strip()
            if idn == "":
                idn = inst.query("*ID?").strip()
        except pyvisa.VisaIOError as e:
            logger.error(f"Failed to query IDN for {resource_name}: {e}")
            inst.close()
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            return False
        # logger.debug(f"IDN: {idn}")
        
        context = self.make_instrument_context(inst_nr, idn)

        if "cmds_init" not in context or len(context["cmds_init"]) == 0:
            logger.error(f"No initialization commands for {idn}, cannot run the tests")
            inst.close()
            self._inst_contexts[inst_nr]["opened"] = False
            self._inst_contexts[inst_nr]["inst"] = None
            return False

        for k,v in context.items():
            self._inst_contexts[inst_nr][k] = v
        return True
    
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
        return { "cmds_init": ["*CLS"]}
    
    def run(self) -> bool:
        """Run the tests for all instruments in the specified range.

        :return: True if all tests passed, False otherwise
        :rtype: bool
        """
        all_passed = True
        for inst_nr in range(self.start_instrument, self.end_instrument + 1):
            resource_name = self.get_resourcename_for_instrument(inst_nr)
            logger.info(f"Setting up instrument {inst_nr} at {resource_name}")
            if not self.setup_instrument(resource_name, inst_nr):
                logger.error(f"Failed to setup instrument {inst_nr} at {resource_name}")
                all_passed = False
                continue
            
            context = self._inst_contexts[inst_nr]
            inst = context["inst"]
            cmds_init = context["cmds_init"]
            logger.info(f"Initializing instrument {inst_nr} with commands: {cmds_init}")
            for cmd in cmds_init:
                try:
                    inst.write(cmd)
                except pyvisa.VisaIOError as e:
                    logger.error(f"Failed to write command '{cmd}' to instrument {inst_nr}: {e}")
                    all_passed = False
                    break
            
            if not self.test_instrument(inst_nr, context):
                all_passed = False
        
        return all_passed
    
    
    def test_instrument(self, inst_nr: int, context: dict) -> bool:
        """Run the actual tests for the given instrument number.
        
        This method should be overridden in a subclass to implement specific tests.
        
        The context parameter contains all test specific information for the instrument, 
        as created by the make_instrument_context method and populated in the setup_instrument method. 
        
        :param inst_nr: The instrument number
        :type inst_nr: int
        :param context: The instrument context for the given instrument. This context is created by the make_instrument_context method and contains all test specific information for the instrument.
        :type context: dict
        :return: True if all tests passed, False otherwise
        :rtype: bool
        """
        logger.info(f"Running tests for instrument {inst_nr}")
        # Implement specific tests here
        return True

    def close_instrument(self, inst_nr: int, context: dict) -> bool:
        inst = context["inst"]
        if inst is not None:
            if context["opened"]:
                inst.close()
                context["opened"] = False
                context["inst"] = None
        return True

#endregion